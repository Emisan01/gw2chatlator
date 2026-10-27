// local_mt_translator.cpp — the game talks to a separate inference process over bounded JSON lines.
#include "local_mt_translator.hpp"

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

class LocalMtTranslator final : public Translator {
public:
    LocalMtTranslator(std::wstring helper, std::wstring data, bool forceNllb = false)
        : helper_(std::move(helper)), data_(std::move(data)), forceNllb_(forceNllb) {}
    ~LocalMtTranslator() override { Stop(); }

    std::wstring Name() const override { return forceNllb_ ? Tr(L"NLLB-200 (local)") : Tr(L"OPUS-MT (local)"); }

    void StopForRemoval() {
        std::lock_guard<std::mutex> lock(mu_);
        Stop();
    }

    void SetBeamSize(int b) { beamSize_.store(b); }
    HelperInfo GetInfo() const { return info_; }

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
        if (forceNllb_) {
            const bool hasNllb = std::find(pairs.begin(), pairs.end(), "nllb-200") != pairs.end();
            pairs = hasNllb ? std::vector<std::string>{"nllb-200"} : std::vector<std::string>{};
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
        // One text through the route (a pivot language between, when there is no direct pair).
        const auto translateText = [&](std::wstring text) -> std::optional<std::wstring> {
            for (const std::string& pair : route) {
                const size_t dash = pair.find('-');
                std::string sourceCode = pair.substr(0, dash), targetCode = pair.substr(dash + 1);
                const std::string utf8 = Request(text, sourceCode, targetCode, result.error, result.errorKind);
                if (!result.error.empty()) return std::nullopt;
                const std::wstring translatedHop = CleanLocalMtOutput(text, FromUtf8(utf8));
                if (!IsPlausibleLocalMtOutput(text, translatedHop)) {
                    result.error = L"The local model returned an implausible translation";
                    result.errorKind = TranslateErrorKind::BadOutput;
                    return std::nullopt;
                }
                text = translatedHop;
            }
            return text;
        };
        // Sentence by sentence: the models are trained on single sentences – several at once, NLLB drops some. In a
        // sentence the protected parts in its middle stay in place, so the model sees the whole sentence; only when it
        // changed one of them, that sentence goes in pieces around them (the way all text went before 2.6.5).
        std::wstring translated;
        for (const SentenceSegments& sentence : SegmentsBySentence(segments)) {
            std::vector<Segment> pieces = SplitForLocalMt(sentence.segments);
            size_t first = 0, last = pieces.size();
            while (first < last && pieces[first].keep) ++first;
            while (last > first && pieces[last - 1].keep) --last;
            std::wstring lead, tail, middle;
            for (size_t k = 0; k < first; ++k) lead += pieces[k].text;
            for (size_t k = last; k < pieces.size(); ++k) tail += pieces[k].text;
            const std::vector<Segment> inner(pieces.begin() + static_cast<std::ptrdiff_t>(first),
                                             pieces.begin() + static_cast<std::ptrdiff_t>(last));
            if (!inner.empty()) {
                bool done = false;
                if (inner.size() > 1) {  // protected parts inside: the whole sentence first
                    const auto whole = translateText(JoinSegments(inner));
                    if (!whole && !result.error.empty() && result.errorKind != TranslateErrorKind::BadOutput) return result;
                    if (whole && MissingProtected(inner, *whole).empty()) {
                        middle = *whole;
                        done = true;
                    }
                    result.error.clear();
                    result.errorKind = TranslateErrorKind::None;
                }
                for (size_t k = 0; !done && k < inner.size(); ++k) {
                    if (inner[k].keep) {
                        middle += inner[k].text;
                        continue;
                    }
                    const auto piece = translateText(inner[k].text);
                    if (!piece) return result;
                    middle += *piece;
                }
            }
            translated += lead + middle + tail + sentence.gap;
        }
        if (!IsPlausibleLocalMtOutput(original, translated)) {
            result.error = L"The local model returned an implausible translation";
            result.errorKind = TranslateErrorKind::BadOutput;
            return result;
        }
        result.ok = true;
        result.text = std::move(translated);
        result.score = lastScore_;
        result.scored = lastScored_;
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
        if (forceNllb_ && !HasCap(ready, "nllb")) {
            Stop();
            info_.status = HelperStatus::Outdated;
            info_.outdatedReason = L"missing capability 'nllb'";
            error = L"The local translator helper lacks NLLB support: " + info_.outdatedReason;
            return false;
        }
        info_.status = HelperStatus::Ok;
        info_.outdatedReason.clear();
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
        const std::string model = forceNllb_ ? "nllb" : "";
        const std::string line = BuildLocalMtRequest(id, source, target, ToUtf8(text), beamSize_.load(), model) + "\n";
        if (line.size() > kMaxLocalMtJsonLine) {
            error = L"The local translation request is too large";
            errorKind = TranslateErrorKind::BadOutput;
            return {};
        }
        if (!Start(error)) {
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
        const ULONGLONG started = GetTickCount64();
        for (;;) {
            const DWORD elapsed = static_cast<DWORD>(GetTickCount64() - started);
            if (elapsed >= 5000) break;
            LocalMtMessage message;
            if (!ReadMessage(5000 - elapsed, message)) break;
            if (message.kind == LocalMtMessage::Kind::Loaded) continue;
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
                return message.text;
            }
            error = FromUtf8(message.error.empty() ? "local translation failed" : message.error);
            errorKind = TranslateErrorKind::Other;
            return {};
        }
        Stop();
        error = L"The local translation timed out or its helper stopped";
        errorKind = TranslateErrorKind::Timeout;
        return {};
    }

    void Stop() {
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
            CloseHandle(process_);
            process_ = nullptr;
        }
        if (job_) { CloseHandle(job_); job_ = nullptr; }
        if (read_) { CloseHandle(read_); read_ = nullptr; }
        pending_.clear();
    }

    const std::wstring helper_, data_;
    const bool forceNllb_ = false;
    HelperInfo info_;
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
};

}  // namespace

std::shared_ptr<Translator> MakeLocalMtTranslator(const std::wstring& helperExe, const std::wstring& dataDir,
                                                  bool forceNllb) {
    return std::make_shared<LocalMtTranslator>(helperExe, dataDir, forceNllb);
}

void StopLocalMtTranslator(const std::shared_ptr<Translator>& translator) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->StopForRemoval();
}

void SetLocalMtBeamSize(const std::shared_ptr<Translator>& translator, int beamSize) {
    if (auto local = std::dynamic_pointer_cast<LocalMtTranslator>(translator)) local->SetBeamSize(beamSize);
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
