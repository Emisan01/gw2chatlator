#!/usr/bin/env python3
"""
tools/gen_pois.py - Writes the names of all waypoints and points of interest from GW2's public API.

The API cannot answer a single point by the number in its chat code: only whole floors of a continent hold them. So the
table is made here once per release, data/poi/poi-<lang>.tsv, one line per point: id, type, name, map, continent, floor, x,
y. The add-on fetches
the table of the reading language from the extras release when the first such code is in view.

All floors are read once in English to learn which of them add anything; the other languages read only those.
"""

import json
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
API = "https://api.guildwars2.com/v2/"
LANGS = ("en", "de", "fr", "es", "zh")  # all the API knows; ChatCodeApiLang in src/core/chat_code.cpp maps to them
CONTINENTS = (1, 2)
OUT = ROOT / "data" / "poi"


def get(path):
    for attempt in range(5):
        try:
            request = urllib.request.Request(API + path, headers={"User-Agent": "GW2Chatlator-gen-pois"})
            with urllib.request.urlopen(request, timeout=120) as reply:
                return json.loads(reply.read())
        except Exception:  # the API has moments: wait and ask again, the last failure goes up
            if attempt == 4:
                raise
            time.sleep(3 * (attempt + 1))
    return None


def clean(text):
    return " ".join(str(text).split())


def read_floor(continent, floor, lang, points):
    """Adds the floor's named points; returns how many were new."""
    new = 0
    for region in get(f"continents/{continent}/floors/{floor}?lang={lang}").get("regions", {}).values():
        for game_map in region.get("maps", {}).values():
            for point in game_map.get("points_of_interest", {}).values():
                name = clean(point.get("name", ""))
                if not name or point["id"] in points:
                    continue
                coord = point.get("coord", [0, 0])
                points[point["id"]] = (point.get("type", ""), name, clean(game_map.get("name", "")), continent,
                                       point.get("floor", floor), round(coord[0], 1), round(coord[1], 1))
                new += 1
    return new


def write(lang, points):
    OUT.mkdir(parents=True, exist_ok=True)
    lines = ["\t".join(str(part) for part in (key, *value)) + "\n" for key, value in sorted(points.items())]
    (OUT / f"poi-{lang}.tsv").write_text("".join(lines), encoding="utf-8", newline="\n")
    print(f"poi-{lang}.tsv: {len(lines)} points, {sum(len(l.encode('utf-8')) for l in lines):,} bytes", flush=True)


def main():
    useful = []
    english = {}
    for continent in CONTINENTS:
        for floor in get(f"continents/{continent}/floors"):
            if read_floor(continent, floor, "en", english):
                useful.append((continent, floor))
    print(f"{len(useful)} floors hold new points: {useful}", flush=True)
    write("en", english)
    for lang in LANGS:
        if lang == "en":
            continue
        points = {}
        for continent, floor in useful:
            read_floor(continent, floor, lang, points)
        write(lang, points)
    return 0


if __name__ == "__main__":
    sys.exit(main())
