// slang.cpp — keep these lists short and boring: every keep-word is a word
// the translator will never touch, so only add terms that read the same in
// every language. Players can extend the spell-check list themselves via
// the right-click menu (stored in gw2-woerter.txt).
#include "slang.hpp"

#include <initializer_list>
#include <iterator>

namespace gct {

namespace {

const wchar_t* const kKeep[] = {
    // content & modes
    L"lfg", L"lfm", L"lf", L"wvw", L"pvp", L"pve", L"gw2", L"hot", L"pof", L"eod", L"soto", L"jw", L"voe",
    L"lw", L"lws", L"cm", L"cms", L"t4", L"meta", L"zerg", L"pug", L"pugs",
    // combat
    L"dps", L"hps", L"cc", L"aoe", L"boon", L"boons", L"alac", L"alacrity", L"quickness", L"might", L"stab",
    // professions / specs, short forms
    L"ele", L"necro", L"engi", L"rev", L"mes", L"warri", L"chrono", L"scourge", L"fb", L"dh", L"hfb", L"qfb",
    // items & map
    L"wp", L"wps", L"ecto", L"ektos", L"li", L"ld",
    // chat
    L"afk", L"brb", L"gg", L"gz", L"gratz", L"ty", L"thx", L"np", L"wb", L"inc", L"rdy", L"lol", L"omg", L"xd",
};

const wchar_t* const kSpellOnly[] = {
    L"kommi", L"kommis", L"kommander", L"commander", L"squad", L"squads", L"taggen", L"stack", L"stacke",
    L"stacken", L"stackt", L"raid", L"raids", L"raiden", L"fraktal", L"fraktale", L"fractal", L"fractals",
    L"strike", L"strikes", L"metas", L"blob", L"loot", L"looten", L"farmen", L"gildi", L"gildis", L"carry",
    L"carryen", L"buff", L"buffs", L"buffen", L"heal", L"healer", L"heali", L"healen", L"condi", L"condis",
    L"kiten", L"rezz", L"rezzen", L"ressen", L"revive", L"reviven", L"mount", L"mounts", L"gems", L"ekto",
    L"golem", L"golems", L"legy", L"legi", L"legis", L"legendary", L"exo", L"exos", L"gear", L"dmg", L"deff",
    L"deffen", L"pushen", L"worldboss", L"worldbosse", L"dailies", L"daily", L"weekly", L"pls", L"plz", L"ok",
    L"wipe", L"wipen", L"gewiped", L"cd", L"cds", L"cooldown", L"skill", L"skills", L"mats", L"t5", L"t6",
    L"sigil", L"sigils", L"infusion", L"infusionen", L"karma", L"hp", L"stats", L"build", L"builds",
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

const WordSet& BuiltinKeepWords() {
    static const WordSet s = Make({kKeep}, {std::size(kKeep)});
    return s;
}

const WordSet& BuiltinSpellIgnore() {
    static const WordSet s = Make({kKeep, kSpellOnly}, {std::size(kKeep), std::size(kSpellOnly)});
    return s;
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
