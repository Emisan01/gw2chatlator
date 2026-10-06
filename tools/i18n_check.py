#!/usr/bin/env python3
"""Lists UI texts (Tr / TrF / tables in the code) that have no German or Arabic
entry yet, and entries that are no longer used.

    python3 tools/i18n_check.py            # report
    python3 tools/i18n_check.py --missing  # only the missing English keys, one per line
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
# Adjacent L"..." L"..." literals inside Tr(...) / TrF(...) are joined like the compiler does.
CALL = re.compile(r'\bTrF?\(\s*((?:L"(?:[^"\\]|\\.)*"\s*)+)')
# Arrays of English texts that are passed through Tr() later (tab names, page titles, script names).
EXTRA = re.compile(r'Tr\((\w+)\[')
LIT = re.compile(r'L"((?:[^"\\]|\\.)*)"')

def unescape(s):
    s = re.sub(r'\\u([0-9a-fA-F]{4})', lambda m: chr(int(m.group(1), 16)), s)
    s = re.sub(r'\\U([0-9a-fA-F]{8})', lambda m: chr(int(m.group(1), 16)), s)
    return s.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace('\\\\', '\\')

# Texts translated at run time without a literal Tr() call (default tab names).
EXTRA_KEYS = ["Chat"]

def used_keys():
    keys = {k: "default tab names" for k in EXTRA_KEYS}
    for p in SRC.rglob("*.cpp"):
        if p.name.startswith("i18n_"):
            continue
        text = p.read_text(encoding="utf-8")
        for m in CALL.finditer(text):
            key = "".join(unescape(x) for x in LIT.findall(m.group(1)))
            keys.setdefault(key, f"{p.relative_to(ROOT)}")
    # Texts kept in tables and translated with Tr(table[i]) / Tr(r.name).
    tables = {
        "src/core/gw2_text.cpp": r'\{0x[0-9A-Fa-f]+,\s*0x[0-9A-Fa-f]+,\s*L"([^"]+)"\}',
        "src/app/settings_dialog.cpp": r'(?:names\[\]|titles\[\])\s*=\s*\{([^}]*)\}',
    }
    for path, pat in tables.items():
        text = (ROOT / path).read_text(encoding="utf-8")
        for m in re.finditer(pat, text, re.S):
            for lit in LIT.findall(m.group(0)) if '{' in m.group(0)[:2] or 'names' in m.group(0) or 'titles' in m.group(0) else [m.group(1)]:
                keys.setdefault(unescape(lit), path)
    # Local model offers {L"gemma3:1b", L"summary" L"..."}: the summary goes through Tr().
    path = "src/win/online_translators.cpp"
    text = (ROOT / path).read_text(encoding="utf-8")
    for m in re.finditer(r'\{L"[^"]+",\s*((?:L"(?:[^"\\]|\\.)*"\s*)+)\}', text):
        keys.setdefault("".join(unescape(x) for x in LIT.findall(m.group(1))), path)
    return keys

def table(lang):
    text = (SRC / "core" / f"i18n_{lang}.cpp").read_text(encoding="utf-8")
    pairs = re.findall(r'\{\s*((?:L"(?:[^"\\]|\\.)*"\s*)+),\s*((?:L"(?:[^"\\]|\\.)*"\s*)+)\}', text)
    out = {}
    for en, tr in pairs:
        out["".join(unescape(x) for x in LIT.findall(en))] = "".join(unescape(x) for x in LIT.findall(tr))
    return out

def main():
    keys = used_keys()
    if "--missing" in sys.argv:
        de, ar = table("de"), table("ar")
        for k in sorted(keys):
            if k not in de or k not in ar:
                print(k.encode("unicode_escape").decode("ascii"))
        return 0
    bad = 0
    for lang in ("de", "ar"):
        t = table(lang)
        missing = [k for k in keys if k not in t]
        unused = [k for k in t if k not in keys]
        print(f"{lang}: {len(t)} entries, {len(missing)} missing, {len(unused)} unused")
        for k in sorted(missing):
            print(f"  missing: {k!r}  ({keys[k]})")
        for k in sorted(unused):
            print(f"  unused:  {k!r}")
        bad += len(missing)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
