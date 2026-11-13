#!/usr/bin/env python3
"""session_check.py [session-protocol.log] – reads the add-on's session protocol and says what happened.

The protocol is written while you play when the settings file holds

    [Debug]
    Protocol=1

(<GW2>\\addons\\GW2Keyboard\\session-protocol.log; states, codes and counts only – never a text, never a name).
For each session: the version, how long, what was set and when, every error, and three checks that used to be
looked at by hand in the game:

  1. views: a view's switch never changes your own parts (TESTPROTOCOL UI-23);
  2. input errors: each one names source and target language; one outside your send list stands out (UI-24, MT);
  3. whisper tabs: accounts learned and tabs folded, counted (CHAT-33).

Exit code 1 when a check fails. Without a path it reads the game folder's file (GW2_DIR, else C:\\Games\\Guild Wars 2).
"""
import os
import re
import sys
from pathlib import Path


def sessions(lines):
    cur = None
    for raw in lines:
        m = re.match(r"(\d\d:\d\d:\d\d)\.\d+ (.*)", raw.rstrip("\n"))
        if not m:
            continue
        clock, text = m.groups()
        if text.startswith("== session "):
            if cur:
                yield cur
            cur = {"head": text[len("== session "):], "start": clock, "end": clock, "lines": [], "closed": False}
            continue
        if cur is None:
            continue
        cur["end"] = clock
        if "== end of the session" in text:
            cur["closed"] = True
        cur["lines"].append((clock, text))
    if cur:
        yield cur


def check(session) -> bool:
    ok = True
    print(f"== {session['head']}  {session['start']} - {session['end']}"
          f"{'' if session['closed'] else '  (no end line: the game was closed hard, or it still runs)'}")
    states, errors, parts_before = [], [], None
    chat = sum(1 for _, t in session["lines"] if t.startswith("C line in"))
    runs = sum(1 for _, t in session["lines"] if t.startswith("T "))
    bound = sum(1 for _, t in session["lines"] if "whisper tab: account known" in t)
    folded = sum(1 for _, t in session["lines"] if "folded in" in t)
    for clock, text in session["lines"]:
        m = re.match(r"O state after (.*?): (.*)", text)
        if m:
            why, state = m.groups()
            states.append((clock, why, state))
            parts = re.search(r"own parts (\S+)", state)
            if parts:
                if why == "a view's switch" and parts_before is not None and parts.group(1) != parts_before:
                    print(f"   FAIL {clock}: a view's switch changed your own parts {parts_before} -> {parts.group(1)}")
                    ok = False
                parts_before = parts.group(1)
        elif text.startswith("O input error"):
            errors.append((clock, text[2:]))
            if not re.search(r"input error: \w+, \S+ > \S+", text):
                print(f"   FAIL {clock}: an input error without its languages: {text[2:]}")
                ok = False
        elif text.startswith("O options changed") or text.startswith("O window:"):
            print(f"   {clock} {text[2:]}")
        elif text.startswith("Q "):
            m_q = re.match(r"Q health:\s*(\w+)\s*(\d+)", text)
            if m_q:
                kind, sev = m_q.groups()
                sev = int(sev)
                if sev == 2:
                    print(f"   FAIL {clock}: health finding {kind} (severity {sev})")
                    ok = False
                else:
                    print(f"   {clock} health: {kind} (severity {sev})")
            else:
                print(f"   {clock} {text[2:]}")
    for clock, why, state in states:
        print(f"   {clock} after {why}: {state}")
    for clock, text in errors:
        print(f"   {clock} {text}")
    print(f"   {chat} chat lines in, {runs} translator runs, {len(errors)} input error(s), "
          f"{bound} whisper tab(s) bound to an account, {folded} folded")
    print(f"   views: {'own parts never changed by a switch' if ok else 'see FAIL above'}")
    return ok


def main() -> int:
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        path = Path(os.environ.get("GW2_DIR", r"C:\Games\Guild Wars 2")) / "addons" / "GW2Keyboard" / "session-protocol.log"
    if not path.exists():
        print(f"no protocol at {path} – set [Debug] Protocol=1 in gw2-chat-translator.ini and play")
        return 2
    found = list(sessions(path.read_text(encoding="utf-8", errors="replace").splitlines()))
    if not found:
        print("the protocol holds no session")
        return 2
    last = "--all" not in sys.argv
    good = True
    for s in (found[-1:] if last else found):
        good = check(s) and good
    if last and len(found) > 1:
        print(f"({len(found) - 1} earlier session(s) in the file: --all shows them)")
    print("session_check: PASS" if good else "session_check: FAIL")
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
