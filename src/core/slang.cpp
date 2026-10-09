// slang.cpp — keep these lists short and boring: every keep-word is a word
// the translator will never touch, so only add terms that read the same in
// every language. Players can extend the spell-check list themselves via
// the right-click menu (stored in gw2-woerter.txt).
#include "slang.hpp"

#include <initializer_list>
#include <iterator>

namespace gct {

namespace {

// Kept in any spelling: abbreviations that are no word in any common language (terms from the GW2 wiki's list of
// abbreviations, chosen and checked by hand: no ordinary DE/EN/FR/ES/IT/PT word).
const wchar_t* const kKeep[] = {
    // content & modes
    L"lfg", L"lfm", L"lfr", L"lf", L"wvw", L"pvp", L"spvp", L"pve", L"pvx", L"gw2", L"pof", L"eod", L"soto", L"jw",
    L"voe", L"lw", L"lws", L"cms", L"t4", L"meta", L"zerg", L"pug", L"pugs", L"fotm", L"fracs", L"ebg", L"eotm",
    L"sab", L"jp", L"kp", L"teq", L"cof", L"coe", L"ibs", L"drm", L"wtb", L"wts",
    // raids, strikes, bosses
    L"vg", L"gors", L"kc", L"qtp", L"cmdr", L"comm", L"commi",
    // combat
    L"dps", L"cdps", L"pdps", L"bdps", L"adps", L"qdps", L"hps", L"cc", L"aoe", L"pbaoe", L"boon", L"boons",
    L"alac", L"alacrity", L"quickness", L"stab", L"icd", L"ooc",
    // professions / specs, short forms
    L"necro", L"engi", L"rev", L"warri", L"chrono", L"scourge", L"fb", L"dh", L"hfb", L"qfb", L"bsw", L"vindi",
    L"virt", L"holo", L"dudu", L"ren", L"mech",
    // items & map
    L"wp", L"wps", L"tp", L"ecto", L"ektos", L"ld", L"obby", L"mc", L"wv",
    // chat
    L"afk", L"brb", L"gg", L"gz", L"gratz", L"ty", L"thx", L"np", L"wb", L"inc", L"rdy", L"lol", L"omg", L"xd",
    L"gtg", L"g2g", L"rc", L"dc",
};

// Kept only when written like an abbreviation (two capitals or more: "LA", "HoT", "CoF"): the same letters are an
// ordinary word somewhere – "la casa", "it's hot", "de", "se", "cm" (centimetre).
const wchar_t* const kKeepCaps[] = {
    L"hot", L"la", L"de", L"se", L"cm", L"ac", L"ap", L"ar", L"bl", L"ca", L"co", L"cs", L"cw", L"dd", L"dr", L"ds",
    L"dt", L"li", L"mo", L"ms", L"mf", L"ow", L"sw", L"ta", L"tc", L"td", L"sb", L"sh", L"eb", L"ha", L"ls",
    L"oos", L"am",
};

const wchar_t* const kSpellOnly[] = {
    L"kommi", L"kommis", L"kommander", L"commander", L"squad", L"squads", L"taggen", L"stack", L"stacke",
    L"stacken", L"stackt", L"raid", L"raids", L"raiden", L"fraktal", L"fraktale", L"fractal", L"fractals",
    L"strike", L"strikes", L"metas", L"blob", L"loot", L"looten", L"farmen", L"farme", L"farmst", L"farmt", L"gefarmt", L"gildi", L"gildis", L"carry",
    L"carryen", L"buff", L"buffs", L"buffen", L"heal", L"healer", L"heali", L"healen", L"condi", L"condis",
    L"kiten", L"rezz", L"rezzen", L"ressen", L"revive", L"reviven", L"mount", L"mounts", L"gems", L"ekto",
    L"golem", L"golems", L"legy", L"legi", L"legis", L"legendary", L"exo", L"exos", L"gear", L"dmg", L"deff",
    L"deffen", L"pushen", L"worldboss", L"worldbosse", L"dailies", L"daily", L"weekly", L"pls", L"plz", L"ok",
    L"wipe", L"wipen", L"gewiped", L"cd", L"cds", L"cooldown", L"skill", L"skills", L"mats", L"t5", L"t6",
    L"sigil", L"sigils", L"infusion", L"infusionen", L"karma", L"hp", L"stats", L"build", L"builds",
    // GW2 short forms that are also words elsewhere ("mes amis", "ele" = he): not kept, only never marked wrong
    L"mes", L"ele", L"rez", L"res", L"rota", L"champ", L"vet", L"condi", L"zerk", L"mesmer",
    L"ohje", L"oje",
};

// Words for the word bar before much is learned, by the language you write in: GW2 terms everyone uses, plus the
// German or English names. Never English suggestions for German or the other way round.
const wchar_t* const kStarterAny[] = {
    L"LFG", L"LFM", L"Tequatl", L"Dungeon", L"Raid", L"Strike", L"Meta", L"Event", L"Champion", L"Commander",
    L"Squad", L"Party", L"Rata Sum", L"Hoelbrak", L"Drakkar", L"Quickness", L"Alacrity", L"Might", L"Boons",
    L"Healer", L"Heal", L"Tank", L"DPS", L"Condi", L"Revive", L"Stack", L"Breakbar", L"Wipe", L"Karma", L"Gold",
    L"Gems", L"Raptor", L"Sorry", L"Infusion",
};
const wchar_t* const kStarterDe[] = {
    L"Fraktal", L"Fraktale", L"Erfolg", L"Kommandeur", L"Trupp", L"Gruppe", L"Gilde", L"Gildenhalle", L"Weltboss",
    L"Löwenstein", L"Götterfels", L"Schwarzzitadelle", L"Wegmarke", L"Drachensturm", L"Silberwüste", L"Stacken",
    L"Legendär", L"Aufgestiegen", L"Exotisch", L"Mystische Schmiede", L"Ektoplasma", L"Edelsteine",
    L"Himmelsschuppe", L"Greif", L"Springer", L"Schakal", L"Käfer", L"Kriegsklaue", L"Danke", L"Bitte", L"Gerne",
    L"Willkommen",
};
const wchar_t* const kStarterEn[] = {
    L"Fractal", L"Fractals", L"Worldboss", L"Achievement", L"Dailies", L"Weekly", L"Guild", L"Guild Hall",
    L"Lion's Arch", L"Divinity's Reach", L"Black Citadel", L"Waypoint", L"Dragonstorm", L"Silverwastes",
    L"Legendary", L"Ascended", L"Exotic", L"Mystic Forge", L"Ectoplasm", L"Skyscale", L"Griffon",
    L"Jackal", L"Beetle", L"Warclaw", L"Thanks", L"Please", L"Welcome",
};

WordSet Make(std::initializer_list<const wchar_t* const*> lists, std::initializer_list<size_t> sizes) {
    WordSet s;
    auto size = sizes.begin();
    for (const wchar_t* const* list : lists) {
        for (size_t i = 0; i < *size; ++i) s.insert(CaseFold(list[i]));
        ++size;
    }
    return s;
}

}  // namespace

// The abbreviation-only entries go into the same set as "^" + word (see IsKeepWord).
void AddCaps(WordSet& s) {
    for (const wchar_t* w : kKeepCaps) s.insert(L"^" + CaseFold(w));
}

const WordSet& BuiltinKeepWords() {
    static const WordSet s = [] {
        WordSet x = Make({kKeep}, {std::size(kKeep)});
        AddCaps(x);
        return x;
    }();
    return s;
}

// The most common English words of a chat (written for this list, everyday vocabulary).
const wchar_t* const kChatEnglish[] = {
    L"a", L"about", L"after", L"again", L"all", L"almost", L"also", L"always", L"am", L"an", L"and", L"any",
    L"anyone", L"anything", L"are", L"around", L"as", L"ask", L"at", L"away", L"back", L"bad", L"be", L"because",
    L"been", L"before", L"being", L"best", L"better", L"big", L"boss", L"both", L"bring", L"busy", L"but", L"buy",
    L"by", L"call", L"came", L"can", L"cant", L"come", L"coming", L"could", L"cool", L"damn", L"day", L"did",
    L"didnt", L"do", L"does", L"doesnt", L"doing", L"done", L"dont", L"down", L"each", L"easy", L"else", L"enough",
    L"even", L"ever", L"every", L"everyone", L"fast", L"few", L"fine", L"first", L"for", L"found", L"free",
    L"friend", L"friends", L"from", L"fun", L"funny", L"get", L"gets", L"getting", L"give", L"go", L"goes", L"going",
    L"gone", L"good", L"got", L"great", L"guys", L"had", L"happy", L"has", L"have", L"having", L"he", L"hello",
    L"help", L"her", L"here", L"hey", L"hi", L"him", L"his", L"hold", L"home", L"how", L"i", L"if", L"im", L"in",
    L"into", L"is", L"isnt", L"it", L"its", L"just", L"keep", L"kill", L"kind", L"know", L"last", L"late", L"later",
    L"learn", L"leave", L"left", L"let", L"lets", L"like", L"little", L"long", L"look", L"looking", L"lost", L"lot",
    L"lots", L"love", L"made", L"make", L"many", L"maybe", L"me", L"mean", L"mine", L"more", L"most", L"much",
    L"must", L"my", L"need", L"never", L"new", L"next", L"nice", L"night", L"no", L"not", L"nothing", L"now", L"of",
    L"off", L"oh", L"ok", L"okay", L"old", L"on", L"once", L"one", L"only", L"open", L"or", L"other", L"our", L"out",
    L"over", L"party", L"people", L"play", L"player", L"players", L"please", L"point", L"pretty", L"quick", L"quite",
    L"ready", L"real", L"really", L"right", L"run", L"same", L"say", L"see", L"seem", L"send", L"sent", L"she",
    L"should", L"show", L"since", L"so", L"some", L"someone", L"something", L"soon", L"sorry", L"start", L"still",
    L"stop", L"stuff", L"sure", L"take", L"talk", L"team", L"tell", L"than", L"thank", L"thanks", L"that", L"thats",
    L"the", L"their", L"them", L"then", L"there", L"these", L"they", L"thing", L"things", L"think", L"this",
    L"those", L"though", L"thought", L"through", L"time", L"to", L"today", L"together", L"tomorrow", L"too", L"try",
    L"trying", L"turn", L"up", L"us", L"use", L"used", L"very", L"wait", L"want", L"wanted", L"was", L"way", L"we",
    L"well", L"went", L"were", L"what", L"whats", L"when", L"where", L"which", L"while", L"who", L"why", L"will",
    L"with", L"without", L"wont", L"work", L"would", L"yeah", L"yes", L"yet", L"you", L"your", L"yours", L"youre",
};

// Verb forms that take "s" for "es" in spoken German. "hat"/"war" are left out ("hats", "wars" are English words).
const wchar_t* const kContractionStems[] = {
    L"hab", L"geht", L"gibt", L"gab", L"wird", L"ist", L"mach", L"macht", L"kann", L"ging", L"sieht", L"klappt",
    L"passt", L"stimmt", L"wär", L"hätt", L"nimm", L"lohnt", L"braucht", L"dauert", L"kommt",
    L"gefällt", L"tut", L"hilft", L"reicht", L"schaff", L"glaub",
};

// The stem of a contraction ("Habs" -> "Hab", "geht's" -> "geht"), empty if `word` is none.
static std::wstring ContractionStem(const std::wstring& word) {
    size_t cut = 0;
    if (word.size() > 3 && (word[word.size() - 2] == L'\'' || word[word.size() - 2] == L'\u2019') &&
        (word.back() == L's' || word.back() == L'S'))
        cut = 2;
    else if (word.size() > 2 && (word.back() == L's' || word.back() == L'S'))
        cut = 1;
    if (!cut) return {};
    const std::wstring stem = word.substr(0, word.size() - cut);
    const std::wstring key = CaseFold(stem);
    for (const wchar_t* s : kContractionStems)
        if (key == s) return stem;
    return {};
}

bool IsGermanContraction(const std::wstring& word) { return !ContractionStem(word).empty(); }

std::wstring ExpandGermanContractions(const std::wstring& text) {
    std::wstring out;
    size_t i = 0;
    while (i < text.size()) {
        auto inWord = [&](size_t k) {
            return IsWordChar(text[k]) || ((text[k] == L'\'' || text[k] == L'\u2019') && k > 0 && IsWordChar(text[k - 1]));
        };
        if (!inWord(i)) {
            out += text[i++];
            continue;
        }
        size_t e = i;
        while (e < text.size() && inWord(e)) ++e;
        const std::wstring word = text.substr(i, e - i);
        const std::wstring stem = ContractionStem(word);
        out += stem.empty() ? word : stem + L" es";
        i = e;
    }
    return out;
}

const WordSet& CommonChatEnglish() {
    static const WordSet s = Make({kChatEnglish}, {std::size(kChatEnglish)});
    return s;
}

const WordSet& BuiltinSpellIgnore() {
    static const WordSet s = [] {
        WordSet x = Make({kKeep, kSpellOnly}, {std::size(kKeep), std::size(kSpellOnly)});
        AddCaps(x);
        return x;
    }();
    return s;
}

bool LooksLikeAbbreviation(const std::wstring& word) {
    size_t upper = 0, letters = 0;
    for (wchar_t c : word) {
        if (!IsWordChar(c) || (c >= L'0' && c <= L'9')) continue;
        ++letters;
        if (CaseFold(std::wstring(1, c)) != std::wstring(1, c)) ++upper;
    }
    return letters >= 2 && upper >= 2;
}

const wchar_t* const kGamerAbbrevs[] = {
    L"lf1m", L"lf2m", L"lf3m", L"lf4m", L"lf5m", L"lfm", L"lfg",
    L"f2p", L"p2w", L"b2b", L"b3b", L"w2w", L"g2g", L"gtg", L"c2c",
    L"h2h", L"r2r", L"d2d", L"o2o", L"p2p", L"m2m", L"1v1", L"2v2",
    L"3v3", L"4v4", L"5v5", L"10v10", L"wvw", L"eotm", L"pvp", L"spvp",
    L"pve", L"pvx", L"afk", L"brb", L"btw", L"gg", L"ty", L"np", L"yw",
    L"omw", L"rofl", L"lmao", L"ttyl", L"dps", L"cdps", L"pdps", L"hps",
};

bool IsGamerAbbreviation(const std::wstring& word) {
    static const WordSet s = [] {
        WordSet set;
        for (const wchar_t* w : kGamerAbbrevs) set.insert(CaseFold(w));
        return set;
    }();
    return s.count(CaseFold(word)) > 0;
}

bool IsKeepWord(const WordSet& keep, const std::wstring& word) {
    const std::wstring f = CaseFold(word);
    return keep.count(f) > 0 || (keep.count(L"^" + f) > 0 && LooksLikeAbbreviation(word)) ||
           keep.count(L"=" + word) > 0;  // a name taught by the user: kept only exactly as taught
}

const std::vector<std::wstring>& Gw2StarterWords(const std::wstring& lang) {
    std::wstring p = ToUpperAscii(Trim(lang));
    p = p.substr(0, p.find(L'-'));
    static const std::vector<std::wstring> any(std::begin(kStarterAny), std::end(kStarterAny));
    static const std::vector<std::wstring> de = [] {
        std::vector<std::wstring> v(std::begin(kStarterAny), std::end(kStarterAny));
        v.insert(v.end(), std::begin(kStarterDe), std::end(kStarterDe));
        return v;
    }();
    static const std::vector<std::wstring> en = [] {
        std::vector<std::wstring> v(std::begin(kStarterAny), std::end(kStarterAny));
        v.insert(v.end(), std::begin(kStarterEn), std::end(kStarterEn));
        return v;
    }();
    static const std::vector<std::wstring> all = [] {
        std::vector<std::wstring> v = de;
        v.insert(v.end(), std::begin(kStarterEn), std::end(kStarterEn));
        return v;
    }();
    if (p.empty()) return all;
    if (p == L"DE") return de;
    if (p == L"EN") return en;
    return any;
}

}  // namespace gct
