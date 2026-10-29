"""Official GW2 names in en/de/fr/es from the public GW2 API (no key) -> data/gw2-names.tsv.

One line per name that differs between the languages: key <TAB> en <TAB> de <TAB> fr <TAB> es.
Names that are the same in every language are left out (nothing to translate), and so is a second id with the
same names (story copies of a map). Run once per release; --check only compares with the file in the repo.
"""
import json
import sys
import urllib.request
from pathlib import Path

API = "https://api.guildwars2.com/v2/"
LANGS = ("en", "de", "fr", "es")
OUT = Path(__file__).resolve().parent.parent / "data" / "gw2-names.tsv"
# Map types a player names in chat: the open world, WvW borderlands and instances (dungeons, fractals, raids).
MAP_TYPES = {"Public", "Instance", "Center", "RedHome", "BlueHome", "GreenHome", "EdgeOfTheMists", "JumpPuzzle"}


def get(path):
    req = urllib.request.Request(API + path, headers={"User-Agent": "GW2Chatlator gen_names"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def fetch(endpoint, lang):
    if endpoint == "maps":  # no ids=all here: pages of 200
        out, page = [], 0
        while True:
            try:
                part = get(f"maps?page={page}&page_size=200&lang={lang}")
            except urllib.error.HTTPError:
                return out
            out += part
            page += 1
    return get(f"{endpoint}?ids=all&lang={lang}")


def collect():
    rows = {}
    for cat, endpoint in (("map", "maps"), ("prof", "professions"), ("spec", "specializations"),
                          ("mount", "mounts/types"), ("wvw", "wvw/objectives")):
        per_lang = {}
        for lang in LANGS:
            for item in fetch(endpoint, lang):
                if cat == "map" and item.get("type") not in MAP_TYPES:
                    continue
                name = (item.get("name") or "").strip()
                if name:
                    per_lang.setdefault(f"{cat}:{item['id']}", {})[lang] = name
        for key, names in per_lang.items():
            if all(l in names for l in LANGS):
                rows[key] = tuple(names[l] for l in LANGS)
    seen, lines = set(), []
    for key in sorted(rows, key=lambda k: (k.split(":")[0], int(k.split(":")[1]) if k.split(":")[1].isdigit() else 0, k)):
        names = rows[key]
        if len(set(n.casefold() for n in names)) == 1 or names in seen:
            continue
        seen.add(names)
        lines.append("\t".join((key,) + names))
    return lines


def main():
    head = "# Official GW2 names (GW2 API, public endpoints) - written by tools/gen_names.py, do not edit by hand\n"
    text = head + "\n".join(collect()) + "\n"
    if "--check" in sys.argv:
        same = OUT.exists() and OUT.read_text(encoding="utf-8").replace("\r\n", "\n") == text
        print("gen_names: data/gw2-names.tsv is current" if same else "gen_names: data/gw2-names.tsv differs from the API")
        return 0 if same else 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_names: {text.count(chr(10)) - 1} names, {len(text.encode('utf-8'))} bytes -> {OUT.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
