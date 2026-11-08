// local_mt_translator.cpp — the game talks to a separate inference process over bounded JSON lines.
#include "local_mt_translator.hpp"

#include <list>
#include <functional>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/i18n.hpp"
#include "core/langs.hpp"
#include "core/local_mt_protocol.hpp"
#include "core/text.hpp"
#include "core/translation_check.hpp"

namespace gct {
namespace {
constexpr size_t kMaxLocalMtJsonLine = 1'048'576;
std::atomic<void (*)(const std::wstring&)> g_helperLog{nullptr};
void HelperLog(const std::wstring& line) {
    if (const auto log = g_helperLog.load()) log(line);
}

std::vector<std::string> InstalledPairs(const std::wstring& dataDir) {
    std::vector<std::string> pairs;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dataDir + L"\\translate\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return pairs;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != L'.')
            pairs.push_back(ToUtf8(fd.cFileName));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return pairs;
}

enum class LocalKind { Opus, Nllb, M2m };

class LocalMtTranslator final : public Translator {
public:
    LocalMtTranslator(std::wstring helper, std::wstring data, LocalKind kind = LocalKind::Opus)
        : helper_(std::move(helper)), data_(std::move(data)), kind_(kind) {}
    ~LocalMtTranslator() override { Stop(); }

    std::wstring Name() const override {
        return kind_ == LocalKind::Nllb ? Tr(L"NLLB-200 (local)") : kind_ == LocalKind::M2m ? Tr(L"M2M100 (local)") : Tr(L"OPUS-MT (local)");
    }

    void StopForRemoval() {
        std::lock_guard<std::mutex> lock(mu_);
        Stop();
    }

    void SetBeamSize(int b) { beamSize_.store(b); }
    void SetRaw(bool r) { raw_.store(r); }
    // A length or coverage penalty moves the score (coverage 0.20: 105 of 106 lines below the "unsure" mark), so with
    // them it says nothing about the model's certainty and marks no line.
    bool ScoreSaysCertainty() const {
        const LocalMtDecode d = Decode();
        return d.lengthPenalty == 100 && d.coveragePenalty == 0;
    }
    uint64_t HelperCpu() const {
        const HANDLE process = cpuProcess_.load();
        FILETIME created, ended, kernel, user;
        if (!process || !GetProcessTimes(process, &created, &ended, &kernel, &user)) return 0;
        return (static_cast<uint64_t>(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
               (static_cast<uint64_t>(user.dwHighDateTime) << 32 | user.dwLowDateTime);
    }
    void SetVersions(int versions) { versions_.store(std::clamp(versions, 1, 5)); }
    int Versions() const { return versions_.load(); }
    void SetOneVersion(bool one) { oneVersion_.store(one); }
    void SetDecode(const LocalMtDecode& decode) {
        std::lock_guard<std::mutex> lock(decodeMu_);
        decode_ = decode;
    }
    LocalMtDecode Decode() const {
        std::lock_guard<std::mutex> lock(decodeMu_);
        return decode_;
    }
    HelperInfo GetInfo() const { return info_; }
    LocalMtCard GetCard() const {
        std::lock_guard<std::mutex> lock(cardMu_);
        return card_;
    }

    TranslateResult Translate(const std::vector<Segment>& segments, const std::wstring& sourceLang,
                              const std::wstring& targetLang) override {
        TranslateResult result;
        const std::wstring original = JoinSegments(segments);
        std::wstring source = PrimaryLang(sourceLang);
        if (source.empty()) source = GuessLanguageByLetters(original);
        if (source.empty()) {
            result.error = L"The local translator could not identify the source language";
            result.errorKind = TranslateErrorKind::UnsupportedPair;
            return result;
        }
        const std::wstring target = PrimaryLang(targetLang);
        if (target.empty()) {
            result.error = L"The local translator needs a target language";
            result.errorKind = TranslateErrorKind::UnsupportedPair;
            return result;
        }
        if (source == target) {
            result.ok = true;
            result.text = original;
            result.detectedSource = source;
            result.errorKind = TranslateErrorKind::None;
            return result;
        }
        std::vector<std::string> pairs = CachedInstalledPairs();
        if (kind_ == LocalKind::Opus) {
            pairs.erase(std::remove(pairs.begin(), pairs.end(), "m2m100"), pairs.end());
        } else {  // only this model
            const std::string only = kind_ == LocalKind::Nllb ? "nllb-200" : "m2m100";
            const bool has = std::find(pairs.begin(), pairs.end(), only) != pairs.end();
            pairs = has ? std::vector<std::string>{only} : std::vector<std::string>{};
        }
        const auto route = LocalMtRoute(source, target, pairs);
        if (route.empty()) {
            result.error = TrF(L"No local language pack for {1} > {2} - install it: Options > Translator",
                               {source, target});
            result.errorKind = TranslateErrorKind::UnsupportedPair;
            return result;
        }

        const std::vector<Segment> parts = SplitForLocalMt(segments);
        if (std::none_of(parts.begin(), parts.end(), [](const Segment& p) { return !p.keep; })) {
            result.ok = true;  // nothing but symbols, names, codes: nothing for the model
            result.text = original;
            result.detectedSource = source;
            result.errorKind = TranslateErrorKind::None;
            return result;
        }

        std::lock_guard<std::mutex> lock(mu_);
        lastScored_ = false;  // the model's certainty, gathered over this message's requests
        run_ = {};
        lastAlts_.clear();
        // One text through the route (a pivot language between, when there is no direct pair).
        const auto translateText = [&](std::wstring text) -> std::optional<std::wstring> {
            for (const std::string& pair : route) {
                const size_t dash = pair.find('-');
                std::string sourceCode = pair.substr(0, dash), targetCode = pair.substr(dash + 1);
                lastHopText_ = text;
                const std::string utf8 = Request(text, sourceCode, targetCode, result.error, result.errorKind);
                if (!result.error.empty()) return std::nullopt;
                const std::wstring translatedHop = CleanLocalMtOutput(text, FromUtf8(utf8), targetCode);
                if (!IsPlausibleLocalMtOutput(text, translatedHop)) {
                    result.error = L"The local model returned an implausible translation";
                    result.errorKind = TranslateErrorKind::BadOutput;
                    return std::nullopt;
                }
                text = translatedHop;
            }
            return text;
        };
        if (raw_.load()) {  // raw (Technical tab): the parts as written, the model's results as they come
            std::wstring out;
            for (const Segment& s : segments) {
                if (s.keep || std::none_of(s.text.begin(), s.text.end(), IsWordChar)) {
                    out += s.text;
                    continue;
                }
                std::wstring text = Trim(s.text);
                for (const std::string& pair : route) {
                    const size_t dash = pair.find('-');
                    const std::string utf8 =
                        Request(text, pair.substr(0, dash), pair.substr(dash + 1), result.error, result.errorKind);
                    if (!result.error.empty()) return result;
                    text = Trim(FromUtf8(utf8));
                }
                const size_t b = s.text.find_first_not_of(L' '), e = s.text.find_last_not_of(L' ');
                out += s.text.substr(0, b) + text + s.text.substr(e + 1);  // the spaces around it as they were
            }
            result.ok = true;
            result.text = std::move(out);
            result.score = lastScore_;
            result.scored = lastScored_ && ScoreSaysCertainty();
            result.run = run_;
            result.detectedSource = source;
            result.errorKind = TranslateErrorKind::None;
            return result;
        }
        // Sentence by sentence: the models are trained on single sentences – several at once, NLLB drops some. In a
        // sentence its protected words stay in place, so the model sees the whole sentence; when it changed one of
        // them, once more with the keep-words written like names ("Rez", copied where "rez" became "Gebet"), then with
        // placeholders X1, X2 … ("Lion's Arch" became "Löwenbogen", "Wir treffen uns am Wegpunkt in X1" is copied);
        // only then the sentence goes in pieces around them (the way all text went before 2.6.5). Every try is
        // checked the same way; one that is not taken does not count for the score.
        const std::string targetCode = route.back().substr(route.back().find('-') + 1);
        // The model's other versions of a sentence: cleaned and checked like the best one; those that keep every
        // protected part stay. `alts` came with the text `from`; `make` turns one into the whole sentence ("": no).
        const auto sentenceVersions = [&](const std::vector<std::string>& alts, const std::wstring& from,
                                          const std::function<std::wstring(const std::wstring&)>& make) {
            std::vector<std::wstring> out;
            for (const std::string& alt : alts) {
                const std::wstring hop = CleanLocalMtOutput(from, FromUtf8(alt), targetCode);
                if (!IsPlausibleLocalMtOutput(from, hop)) continue;
                const std::wstring whole = make(hop);
                if (!whole.empty()) out.push_back(whole);
            }
            return out;
        };
        struct SentenceDone {
            std::wstring best, gap;
            std::vector<std::wstring> others;
        };
        std::vector<SentenceDone> sentencesDone;
        std::wstring translated;
        for (const SentenceSegments& sentence : SegmentsBySentence(segments)) {
            std::vector<std::wstring> others;
            // A sentence seen before with the same settings: as it came then. While you type, only the sentence you
            // are writing goes to the model.
            std::wstring sentenceKey = source + L">" + target + L"|" + std::to_wstring(beamSize_.load()) + L"|" +
                                       std::to_wstring(versions_.load()) + L"|" + std::to_wstring(oneVersion_.load()) + L"|" + DecodeKey() + L"|";
            for (const Segment& s : sentence.segments) sentenceKey += (s.keep ? L"\x01" : L"\x02") + s.text;
            if (const SentenceKept* kept = FindSentence(sentenceKey)) {
                ++run_.sentencesKept;
                if (kept->scored) {
                    lastScore_ = lastScored_ ? std::min(lastScore_, kept->score) : kept->score;
                    lastScored_ = true;
                }
                translated += kept->text + sentence.gap;
                sentencesDone.push_back({kept->text, sentence.gap, kept->others});
                continue;
            }
            const double scoreBefore = lastScore_;
            const bool scoredBefore = lastScored_;
            lastScored_ = false;
            std::vector<Segment> pieces = SplitForLocalMt(sentence.segments);
            const PieceRange range = WholeSentenceRange(pieces);
            std::wstring lead, tail, middle;
            for (size_t k = 0; k < range.from; ++k) lead += pieces[k].text;
            for (size_t k = range.to; k < pieces.size(); ++k) tail += pieces[k].text;
            const std::vector<Segment> inner(pieces.begin() + static_cast<std::ptrdiff_t>(range.from),
                                             pieces.begin() + static_cast<std::ptrdiff_t>(range.to));
            if (!inner.empty()) {
                bool done = false;
                if (inner.size() > 1) {  // protected parts in it: the whole sentence first
                    std::vector<Segment> named = inner;
                    const bool asNames = KeepWordsAsNames(named);
                    const Placeholders held = WithPlaceholders(inner);
                    // An official name of several words ("Lion's Arch", "[Shipwreck Peak Waypoint]") is translated by
                    // the model in the whole sentence and as a name too – the placeholders keep it: they go first.
                    const bool severalWords = std::any_of(sentence.segments.begin(), sentence.segments.end(), [](const Segment& s) {
                        const std::wstring k = Trim(s.text);
                        return s.keep && k.find(L' ') != std::wstring::npos && std::any_of(k.begin(), k.end(), IsWordChar);
                    });
                    const int order[3] = {severalWords ? 2 : 0, severalWords ? 0 : 1, severalWords ? 1 : 2};
                    for (int step = 0; !done && step < 3; ++step) {
                        const int t = order[step];
                        if ((t == 1 && !asNames) || (t == 2 && held.parts.empty())) continue;
                        const double score = lastScore_;
                        const bool scored = lastScored_;
                        const auto whole =
                            translateText(t == 0 ? JoinSegments(inner) : t == 1 ? JoinSegments(named) : held.text);
                        if (!whole && !result.error.empty() && result.errorKind != TranslateErrorKind::BadOutput)
                            return result;
                        std::wstring filled;
                        const bool complete = whole && (t < 2 ? (filled = *whole, true)
                                                              : FillPlaceholders(*whole, held.parts, filled));
                        // the protected parts themselves ("Tequatl"), not the pieces with the marks around them
                        // ("Tequatl ?" – the model wrote "Tequatl-Meta … ?")
                        const std::vector<std::string> alts = lastAlts_;
                        const std::wstring altsFrom = lastHopText_;
                        if (complete && MissingProtected(sentence.segments, lead + filled + tail).empty()) {
                            middle = RestoreKeepSpelling(filled, sentence.segments);
                            done = true;
                            others = sentenceVersions(alts, altsFrom, [&](const std::wstring& hop) {
                                std::wstring f = hop;
                                if (t == 2 && !FillPlaceholders(hop, held.parts, f)) return std::wstring();
                                if (!MissingProtected(sentence.segments, lead + f + tail).empty()) return std::wstring();
                                return lead + RestoreKeepSpelling(f, sentence.segments) + tail;
                            });
                        } else {
                            lastScore_ = score;
                            lastScored_ = scored;
                        }
                        result.error.clear();
                        result.errorKind = TranslateErrorKind::None;
                    }
                }
                int asked = 0;  // one piece asked of the model: its other versions fit in; of several they would not add up
                size_t pieceAt = 0, pieceLength = 0;
                std::vector<std::string> pieceAlts;
                std::wstring pieceFrom;
                for (size_t k = 0; !done && k < inner.size(); ++k) {
                    if (inner[k].keep) {
                        middle += inner[k].text;
                        continue;
                    }
                    const auto piece = translateText(inner[k].text);
                    if (!piece) return result;
                    if (++asked == 1) {
                        pieceAt = middle.size();
                        pieceLength = piece->size();
                        pieceAlts = lastAlts_;
                        pieceFrom = lastHopText_;
                    }
                    middle += *piece;
                }
                if (asked == 1)
                    others = sentenceVersions(pieceAlts, pieceFrom, [&](const std::wstring& hop) {
                        return lead + middle.substr(0, pieceAt) + hop + middle.substr(pieceAt + pieceLength) + tail;
                    });
            }
            KeepSentence(sentenceKey, {lead + middle + tail, others, lastScore_, lastScored_});
            if (scoredBefore) {
                lastScore_ = lastScored_ ? std::min(lastScore_, scoreBefore) : scoreBefore;
                lastScored_ = true;
            }
            translated += lead + middle + tail + sentence.gap;
            sentencesDone.push_back({lead + middle + tail, sentence.gap, std::move(others)});
        }
        translated = TidyPunctuation(translated, targetCode);
        // The whole message's versions: each sentence's n-th other version, its best where it has fewer.
        for (int n = 0; n + 1 < versions_.load() && !oneVersion_.load(); ++n) {
            std::wstring version;
            for (const SentenceDone& s : sentencesDone)
                version += (static_cast<size_t>(n) < s.others.size() ? s.others[static_cast<size_t>(n)] : s.best) + s.gap;
            version = TidyPunctuation(version, targetCode);
            if (version != translated && IsPlausibleLocalMtOutput(original, version) &&
                std::find(result.variants.begin(), result.variants.end(), version) == result.variants.end())
                result.variants.push_back(std::move(version));
        }
        if (!IsPlausibleLocalMtOutput(original, translated)) {
            result.error = L"The local model returned an implausible translation";
            result.errorKind = TranslateErrorKind::BadOutput;
            return result;
        }
        result.ok = true;
        result.text = std::move(translated);
        result.score = lastScore_;
        result.scored = lastScored_ && ScoreSaysCertainty();
        result.run = run_;
        result.detectedSource = source;
        result.errorKind = TranslateErrorKind::None;
        return result;
    }

private:
    std::vector<std::string> CachedInstalledPairs() {
        std::lock_guard<std::mutex> lock(pairsMu_);
        const ULONGLONG now = GetTickCount64();
        if (pairsCheckedAt_ == 0 || now - pairsCheckedAt_ >= 1000) {
            installedPairs_ = InstalledPairs(data_);
            pairsCheckedAt_ = now;
        }
        return installedPairs_;
    }

    bool Start(std::wstring& error) {
        if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) return true;
        Stop();
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE childIn = nullptr, childOut = nullptr;
        if (!CreatePipe(&childIn, &write_, &sa, 0) || !CreatePipe(&read_, &childOut, &sa, 0)) {
            if (childIn) CloseHandle(childIn);
            if (childOut) CloseHandle(childOut);
            Stop();
            error = L"Could not create the local translator pipes";
            return false;
        }
        SetHandleInformation(write_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(read_, HANDLE_FLAG_INHERIT, 0);
        HANDLE nullOut = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
                CloseHandle(job);
                job = nullptr;
            }
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = childIn;
        si.hStdOutput = childOut;
        si.hStdError = nullOut;
        std::wstring command = L"\"" + helper_ + L"\" --data \"" + data_ + L"\"";
        PROCESS_INFORMATION pi{};
        const DWORD creationFlags = CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS | (job ? CREATE_SUSPENDED : 0);
        const BOOL created = CreateProcessW(helper_.c_str(), command.data(), nullptr, nullptr, TRUE,
                                             creationFlags, nullptr, nullptr, &si, &pi);
        CloseHandle(childIn);
        CloseHandle(childOut);
        if (nullOut != INVALID_HANDLE_VALUE) CloseHandle(nullOut);
        if (!created) {
            if (job) CloseHandle(job);
            Stop();
            info_.status = HelperStatus::Missing;
            error = L"The local translator helper is not installed";
            return false;
        }
        if (job) {
            if (AssignProcessToJobObject(job, pi.hProcess)) {
                job_ = job;
            } else {
                // Some hosts disallow nested jobs; keep the explicit-stop fallback in that case.
                CloseHandle(job);
                job = nullptr;
            }
            if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
                TerminateProcess(pi.hProcess, ERROR_PROCESS_ABORTED);
                WaitForSingleObject(pi.hProcess, 1000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                if (job_) { CloseHandle(job_); job_ = nullptr; }
                if (write_) { CloseHandle(write_); write_ = nullptr; }
                if (read_) { CloseHandle(read_); read_ = nullptr; }
                error = L"The local translator helper could not be started";
                return false;
            }
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        cpuProcess_.store(process_);
        HelperLog(L"helper started: " + Name());
        LocalMtMessage ready;
        if (!ReadMessage(15000, ready) || ready.kind != LocalMtMessage::Kind::Ready) {
            Stop();
            info_.status = HelperStatus::Failed;
            error = L"The local translator helper did not start correctly";
            return false;
        }
        constexpr int kMinHelperProto = 1;
        info_.helper = ready.helper;
        info_.proto = ready.proto;
        info_.caps = ready.caps;
        info_.build = ready.build;

        if (ready.proto < kMinHelperProto) {
            Stop();
            info_.status = HelperStatus::Outdated;
            info_.outdatedReason = L"protocol version " + std::to_wstring(ready.proto) + L" < " + std::to_wstring(kMinHelperProto);
            error = L"The local translator helper is outdated: " + info_.outdatedReason;
            return false;
        }
        if (kind_ != LocalKind::Opus && !HasCap(ready, kind_ == LocalKind::Nllb ? "nllb" : "m2m")) {
            Stop();
            info_.status = HelperStatus::Outdated;
            info_.outdatedReason = kind_ == LocalKind::Nllb ? L"missing capability 'nllb'" : L"missing capability 'm2m'";
            error = (kind_ == LocalKind::Nllb ? L"The local translator helper lacks NLLB support: "
                                              : L"The local translator helper lacks M2M100 support: ") + info_.outdatedReason;
            return false;
        }
        // One from before the threads came still translates, on one core: the options offer the update.
        const bool older = !HasCap(ready, "threads");
        info_.status = older ? HelperStatus::Outdated : HelperStatus::Ok;
        info_.outdatedReason = older ? L"it runs on one core only" : L"";
        HelperLog(L"helper ready: proto " + std::to_wstring(ready.proto) + L", build " + FromUtf8(ready.build.substr(0, 7)));
        return true;
    }

    bool ReadLine(DWORD timeout, std::string& line) {
        const ULONGLONG deadline = GetTickCount64() + timeout;
        for (;;) {
            const size_t nl = pending_.find('\n');
            if (nl != std::string::npos) {
                line.assign(pending_, 0, nl);
                pending_.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            if (!process_ || WaitForSingleObject(process_, 0) != WAIT_TIMEOUT || GetTickCount64() >= deadline) return false;
            DWORD available = 0;
            if (!PeekNamedPipe(read_, nullptr, 0, nullptr, &available, nullptr)) return false;
            if (!available) { Sleep(5); continue; }
            char buffer[4096];
            DWORD n = 0;
            if (!ReadFile(read_, buffer, std::min<DWORD>(available, sizeof(buffer)), &n, nullptr) || !n) return false;
            pending_.append(buffer, n);
            if (pending_.size() > kMaxLocalMtJsonLine) return false;
        }
    }

    bool ReadMessage(DWORD timeout, LocalMtMessage& message) {
        std::string line;
        return ReadLine(timeout, line) && ParseLocalMtMessage(line, message);
    }

    std::string Request(const std::wstring& text, const std::string& source, const std::string& target,
                        std::wstring& error, TranslateErrorKind& errorKind) {
        const int id = ++nextId_;
        const std::string model = kind_ == LocalKind::Nllb ? "nllb" : kind_ == LocalKind::M2m ? "m2m" : "";
        LocalMtDecode decode = Decode();
        decode.hypotheses = oneVersion_.load() ? 1 : versions_.load();
        const std::string line = BuildLocalMtRequest(id, source, target, ToUtf8(text), beamSize_.load(), model, decode) + "\n";
        if (line.size() > kMaxLocalMtJsonLine) {
            error = L"The local translation request is too large";
            errorKind = TranslateErrorKind::BadOutput;
            return {};
        }
        if (!Start(error) && (!LeaveCard("start") || !Start(error))) {
            errorKind = (error.find(L"not installed") != std::wstring::npos) ? TranslateErrorKind::HelperMissing : TranslateErrorKind::HelperCrashed;
            return {};
        }
        DWORD written = 0;
        if (!WriteFile(write_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) ||
            written != line.size()) {
            Stop();
            error = L"The local translator helper stopped unexpectedly";
            errorKind = TranslateErrorKind::HelperCrashed;
            return {};
        }
        // A higher beam takes longer, the first request also has to load the model (its file's size tells about how
        // long): neither is a stalled helper.
        ULONGLONG started = GetTickCount64();
        const DWORD working = 5000 + 2500 * static_cast<DWORD>(std::max(0, beamSize_.load() - 2));
        DWORD limit = working + (modelLoaded_ ? 0 : LoadAllowanceMs(source, target));
        for (;;) {
            const DWORD elapsed = static_cast<DWORD>(GetTickCount64() - started);
            if (elapsed >= limit) break;
            LocalMtMessage message;
            if (!ReadMessage(limit - elapsed, message)) break;
            if (message.kind == LocalMtMessage::Kind::Device) {
                LocalMtCard card;
                card.known = true;
                card.onCard = message.onCard;
                card.name = message.cardName;
                card.driver = message.cardDriver;
                card.why = message.cardWhy;
                card.detail = message.cardDetail;
                card.freeMb = message.cardFreeMb;
                card.totalMb = message.cardTotalMb;
                card.needMb = message.cardNeedMb;
                {
                    std::lock_guard<std::mutex> lock(cardMu_);
                    card_ = card;
                }
                HelperLog((message.onCard ? L"on the card: " : L"on the CPU (" + FromUtf8(message.cardWhy) + L"): ") + FromUtf8(message.cardName) + L", " +
                          std::to_wstring(message.cardFreeMb) + L" MB free, needs " + std::to_wstring(message.cardNeedMb) + L" MB");
                started = GetTickCount64();  // reloading on the CPU is no stalled helper
                limit = working + LoadAllowanceMs(source, target);
                continue;
            }
            if (message.kind == LocalMtMessage::Kind::Loaded) {
                run_.loadMs = static_cast<int>(message.milliseconds);
                HelperLog(L"model loaded: " + FromUtf8(message.pair) + L" in " + std::to_wstring(message.milliseconds) + L" ms");
                modelLoaded_ = true;
                started = GetTickCount64();
                limit = working;
                continue;
            }
            if (message.id != id) {
                Stop();
                error = L"The local translator returned a mismatched response";
                errorKind = TranslateErrorKind::BadOutput;
                return {};
            }
            if (message.kind == LocalMtMessage::Kind::Result) {
                if (message.scored) {  // the least sure request of a message speaks for it
                    lastScore_ = lastScored_ ? std::min(lastScore_, message.score) : message.score;
                    lastScored_ = true;
                }
                modelLoaded_ = true;
                {
                    wchar_t answer[120];
                    swprintf_s(answer, L"answer %d %hs>%hs: beam %d, %d thr, %d ms, %d>%d tok, score %.2f, %d other versions", id, source.c_str(), target.c_str(),
                               message.beam, message.threads, message.modelMs, message.tokensIn, message.tokensOut, message.score, static_cast<int>(message.alts.size()));
                    HelperLog(answer);
                }
                TranslateRun one;
                one.requests = 1;
                one.beam = message.beam;
                one.threads = message.threads;
                one.cores = message.cores;
                one.tokensIn = message.tokensIn;
                one.tokensOut = message.tokensOut;
                one.modelMs = message.modelMs;
                run_.Add(one);
                lastAlts_ = std::move(message.alts);
                return message.text;
            }
            error = FromUtf8(message.error.empty() ? "local translation failed" : message.error);
            HelperLog(L"helper error: " + error);
            errorKind = TranslateErrorKind::Other;
            return {};
        }
        // Ended with 0: the helper's own end after its idle minutes, met by this line – no crash, the card stays (it starts
        // again with the next line). Anything else is a crash: from now on the CPU's helper.
        DWORD exitCode = 1;
        const bool died = process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0 &&
                          !(GetExitCodeProcess(process_, &exitCode) && exitCode == 0);
        Stop();
        if (died) LeaveCard("crash");
        error = L"The local translation timed out or its helper stopped";
        HelperLog(L"helper timed out after " + std::to_wstring(limit) + L" ms");
        errorKind = TranslateErrorKind::Timeout;
        return {};
    }

    // The helper for the graphics card does not start or died (a missing file, a driver crash): from now on the CPU's.
    bool LeaveCard(const char* why) {
        static const std::wstring kCard = L"gw2kb_translate_gpu.exe";
        if (helper_.size() < kCard.size() || helper_.compare(helper_.size() - kCard.size(), kCard.size(), kCard) != 0) return false;
        helper_.replace(helper_.size() - kCard.size(), kCard.size(), L"gw2kb_translate.exe");
        {
            std::lock_guard<std::mutex> lock(cardMu_);
            card_ = {};
            card_.known = true;
            card_.why = why;
        }
        HelperLog(L"the card's helper failed, the CPU's takes over");
        return true;
    }

    struct SentenceKept {
        std::wstring text;
        std::vector<std::wstring> others;
        double score = 0;
        bool scored = false;
    };
    std::wstring DecodeKey() const {
        const LocalMtDecode d = Decode();
        return std::to_wstring(d.patience) + L"," + std::to_wstring(d.lengthPenalty) + L"," + std::to_wstring(d.coveragePenalty) + L"," +
               std::to_wstring(d.repetitionPenalty) + L"," + std::to_wstring(d.noRepeatNgram) + L"," + std::to_wstring(d.disableUnk);
    }
    const SentenceKept* FindSentence(const std::wstring& key) {
        for (auto it = sentences_.begin(); it != sentences_.end(); ++it) {
            if (it->first != key) continue;
            sentences_.splice(sentences_.begin(), sentences_, it);  // the newest first
            return &sentences_.front().second;
        }
        return nullptr;
    }
    void KeepSentence(const std::wstring& key, SentenceKept kept) {
        sentences_.emplace_front(key, std::move(kept));
        if (sentences_.size() > 64) sentences_.pop_back();  // memory only, a few lines' worth
    }

    DWORD LoadAllowanceMs(const std::string& source, const std::string& target) const {
        const std::string id = kind_ == LocalKind::Nllb ? "nllb-200" : kind_ == LocalKind::M2m ? "m2m100" : source + "-" + target;
        WIN32_FILE_ATTRIBUTE_DATA file;
        if (!GetFileAttributesExW((data_ + L"\\translate\\" + FromUtf8(id) + L"\\model.bin").c_str(), GetFileExInfoStandard, &file)) return 0;
        const uint64_t bytes = static_cast<uint64_t>(file.nFileSizeHigh) << 32 | file.nFileSizeLow;
        return static_cast<DWORD>(std::min<uint64_t>(bytes / 40000, 60000));  // at 40 MB/s, a minute at most
    }

    void Stop() {
        modelLoaded_ = false;
        if (write_) {
            const char quit[] = "{\"cmd\":\"quit\"}\n";
            DWORD n = 0;
            WriteFile(write_, quit, sizeof(quit) - 1, &n, nullptr);
            CloseHandle(write_);
            write_ = nullptr;
        }
        if (process_) {
            if (WaitForSingleObject(process_, 1000) != WAIT_OBJECT_0) {
                if (job_) TerminateJobObject(job_, 0);
                else TerminateProcess(process_, 0);
            }
            if (job_) { CloseHandle(job_); job_ = nullptr; }
            WaitForSingleObject(process_, 1000);
            cpuProcess_.store(nullptr);
            HelperLog(L"helper stopped");
            CloseHandle(process_);
            process_ = nullptr;
        }
        if (job_) { CloseHandle(job_); job_ = nullptr; }
        if (read_) { CloseHandle(read_); read_ = nullptr; }
        pending_.clear();
    }

    std::wstring helper_;  // the CPU's after the card's failed
    const std::wstring data_;
    const LocalKind kind_ = LocalKind::Opus;
    mutable std::mutex decodeMu_;  // its own: mu_ is held for a whole translation
    LocalMtDecode decode_;
    HelperInfo info_;
    mutable std::mutex cardMu_;  // never held while waiting for the helper
    LocalMtCard card_;
    std::list<std::pair<std::wstring, SentenceKept>> sentences_;  // the sentence cache, newest first (mu_)
    TranslateRun run_;      // what the model did for the message being translated (mu_)
    std::vector<std::string> lastAlts_;  // the other versions of the last request (mu_)
    std::wstring lastHopText_;           // what that request translated (mu_)
    bool modelLoaded_ = false;  // this helper has answered or loaded (mu_)
    std::atomic<HANDLE> cpuProcess_{nullptr};  // process_, for the live view (which never takes mu_)
    std::atomic<int> versions_{1};
    std::atomic<bool> oneVersion_{false};
    double lastScore_ = 0;  // lowest score of the message being translated (mu_)
    bool lastScored_ = false;
    HANDLE process_ = nullptr, write_ = nullptr, read_ = nullptr, job_ = nullptr;
    std::mutex mu_;
    std::mutex pairsMu_;
    std::vector<std::string> installedPairs_;
    ULONGLONG pairsCheckedAt_ = 0;
    std::string pending_;
    std::atomic<int> nextId_{0};
    std::atomic<int> beamSize_{0};
    std::atomic<bool> raw_{false};
};

}  // namespace

std::shared_ptr<Translator> MakeLocalMtTranslator(const std::wstring& helperExe, const std::wstring& dataDir,
                                                  bool forceNllb) {
    return std::make_shared<LocalMtTranslator>(helperExe, dataDir, forceNllb ? LocalKind::Nllb : LocalKind::Opus);
}

std::shared_ptr<Translator> MakeM2mTranslator(const std::wstring& helperExe, const std::wstring& dataDir) {
    return std::make_shared<LocalMtTranslator>(helperExe, dataDir, LocalKind::M2m);
}

void SetLocalMtLog(void (*log)(const std::wstring& line)) { g_helperLog.store(log); }

void SetLocalMtVersions(const std::shared_ptr<Translator>& translator, int versions) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->SetVersions(versions);
}

void SetLocalMtOneVersion(const std::shared_ptr<Translator>& translator, bool one) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->SetOneVersion(one);
}

uint64_t LocalMtHelperCpu(const std::shared_ptr<Translator>& translator) {
    auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator);
    return local ? local->HelperCpu() : 0;
}

int LocalMtVersions(const std::shared_ptr<Translator>& translator) {
    auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator);
    return local ? local->Versions() : 1;
}

void SetLocalMtDecode(const std::shared_ptr<Translator>& translator, const LocalMtDecode& decode) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->SetDecode(decode);
}

std::wstring LocalMtDecodeNote(const std::shared_ptr<Translator>& translator) {
    auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator);
    if (!local) return {};
    const LocalMtDecode d = local->Decode();
    const LocalMtDecode neutral;
    const auto hundredths = [](int v) {
        wchar_t b[16];
        swprintf_s(b, L"%d.%02d", v / 100, v % 100);
        return std::wstring(b);
    };
    std::wstring note;
    if (d.patience != neutral.patience) note += L" · patience " + hundredths(d.patience);
    if (d.lengthPenalty != neutral.lengthPenalty) note += L" · length " + hundredths(d.lengthPenalty);
    if (d.coveragePenalty != neutral.coveragePenalty) note += L" · coverage " + hundredths(d.coveragePenalty);
    if (d.repetitionPenalty != neutral.repetitionPenalty) note += L" · rep " + hundredths(d.repetitionPenalty);
    if (d.noRepeatNgram != neutral.noRepeatNgram) note += L" · ngram " + std::to_wstring(d.noRepeatNgram);
    if (d.disableUnk) note += L" · no unk";
    if (local->Versions() > 1) note += L" · versions " + std::to_wstring(local->Versions());
    return note;
}

void StopLocalMtTranslator(const std::shared_ptr<Translator>& translator) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->StopForRemoval();
}

void SetLocalMtBeamSize(const std::shared_ptr<Translator>& translator, int beamSize) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->SetBeamSize(beamSize);
}

void SetLocalMtRaw(const std::shared_ptr<Translator>& translator, bool raw) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->SetRaw(raw);
}

bool IsLocalMtTranslator(const std::shared_ptr<Translator>& translator) {
    return std::dynamic_pointer_cast<LocalMtTranslator>(translator) != nullptr;
}

LocalMtCard GetLocalMtCard(const std::shared_ptr<Translator>& translator) {
    auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator);
    return local ? local->GetCard() : LocalMtCard{};
}

HelperInfo GetLocalMtHelperInfo(const std::shared_ptr<Translator>& translator) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) {
        return local->GetInfo();
    }
    return {};
}

HelperInfo ProbeLocalMtHelper(const std::wstring& helperExe, const std::wstring& dataDir, bool forceNllb) {
    HelperInfo info;
    if (GetFileAttributesW(helperExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        info.status = HelperStatus::Missing;
        return info;
    }
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE childIn = nullptr, write = nullptr;
    HANDLE read = nullptr, childOut = nullptr;
    if (!CreatePipe(&childIn, &write, &sa, 0)) {
        info.status = HelperStatus::Failed;
        return info;
    }
    if (!CreatePipe(&read, &childOut, &sa, 0)) {
        CloseHandle(childIn);
        CloseHandle(write);
        info.status = HelperStatus::Failed;
        return info;
    }
    SetHandleInformation(write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childIn;
    si.hStdOutput = childOut;
    si.hStdError = childOut;

    std::wstring command = L"\"" + helperExe + L"\" --data \"" + dataDir + L"\"";
    PROCESS_INFORMATION pi{};
    const BOOL created = CreateProcessW(helperExe.c_str(), command.data(), nullptr, nullptr, TRUE,
                                         CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi);
    CloseHandle(childIn);
    CloseHandle(childOut);
    if (!created) {
        CloseHandle(write);
        CloseHandle(read);
        info.status = HelperStatus::Missing;
        return info;
    }
    CloseHandle(pi.hThread);

    std::string pending;
    char buffer[2048];
    const ULONGLONG deadline = GetTickCount64() + 5000;
    LocalMtMessage ready;
    bool gotReady = false;
    while (GetTickCount64() < deadline && !gotReady) {
        DWORD available = 0;
        if (!PeekNamedPipe(read, nullptr, 0, nullptr, &available, nullptr)) break;
        if (!available) { Sleep(5); continue; }
        DWORD n = 0;
        if (!ReadFile(read, buffer, std::min<DWORD>(available, sizeof(buffer)), &n, nullptr) || !n) break;
        pending.append(buffer, n);
        size_t nl = pending.find('\n');
        if (nl != std::string::npos) {
            std::string line = pending.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (ParseLocalMtMessage(line, ready) && ready.kind == LocalMtMessage::Kind::Ready) {
                gotReady = true;
                break;
            }
        }
    }

    const char quit[] = "{\"cmd\":\"quit\"}\n";
    DWORD n = 0;
    WriteFile(write, quit, sizeof(quit) - 1, &n, nullptr);
    CloseHandle(write);
    if (WaitForSingleObject(pi.hProcess, 1000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 0);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(read);

    if (!gotReady) {
        info.status = HelperStatus::Failed;
        return info;
    }

    constexpr int kMinHelperProto = 1;
    info.helper = ready.helper;
    info.proto = ready.proto;
    info.caps = ready.caps;
    info.build = ready.build;

    if (ready.proto < kMinHelperProto) {
        info.status = HelperStatus::Outdated;
        info.outdatedReason = L"protocol version " + std::to_wstring(ready.proto) + L" < " + std::to_wstring(kMinHelperProto);
        return info;
    }
    if (forceNllb && !HasCap(ready, "nllb")) {
        info.status = HelperStatus::Outdated;
        info.outdatedReason = L"missing capability 'nllb'";
        return info;
    }
    info.status = HelperStatus::Ok;
    return info;
}

}  // namespace gct
