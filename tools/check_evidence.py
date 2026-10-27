"""The working modes' evidence (AGENTS.md): a behaviour change – a commit of type feat, fix, perf or refactor – carries an
"Evidence:" trailer in its message (Gaudí, Formula, Speed lane, Better, Lem, Proof). Docs, data, i18n, build, ci and
chore commits need none.

  python tools/check_evidence.py                       the commits since the rule came (EVIDENCE_SINCE..HEAD)
  python tools/check_evidence.py --message-file <f>    one message (the commit-msg hook: tools/hooks/commit-msg)
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EVIDENCE_SINCE = "493249e"  # the commit that brought the working modes into AGENTS.md
NEEDS = {"feat", "fix", "perf", "refactor"}


def needs_evidence(subject: str) -> bool:
    m = re.match(r"^(\w+)(?:\([^)]*\))?!?:", subject.strip())
    return bool(m) and m.group(1).lower() in NEEDS


def has_evidence(body: str) -> bool:
    return re.search(r"^Evidence:", body, re.M) is not None


def check_message(text: str) -> str:
    lines = [l for l in text.splitlines() if not l.startswith("#")]
    subject = next((l for l in lines if l.strip()), "")
    if needs_evidence(subject) and not has_evidence("\n".join(lines)):
        return subject
    return ""


def check_range(rev_range: str) -> list:
    out = subprocess.run(["git", "log", "--format=%h%x1f%s%x1f%B%x1e", rev_range], cwd=ROOT, capture_output=True,
                         text=True, encoding="utf-8", errors="replace")
    if out.returncode != 0:
        return [f"git log {rev_range} failed: {out.stderr.strip()}"]
    missing = []
    for rec in out.stdout.split("\x1e"):
        parts = rec.strip("\n").split("\x1f")
        if len(parts) < 3:
            continue
        sha, subject, body = parts[0], parts[1], parts[2]
        if needs_evidence(subject) and not has_evidence(body):
            missing.append(f"{sha} {subject[:90]}")
    return missing


def main() -> int:
    if "--message-file" in sys.argv:
        path = sys.argv[sys.argv.index("--message-file") + 1]
        subject = check_message(pathlib.Path(path).read_text(encoding="utf-8", errors="replace"))
        if subject:
            print(f"A behaviour change needs an 'Evidence:' trailer (AGENTS.md, Working Modes): {subject}")
            return 1
        return 0
    missing = check_range(f"{EVIDENCE_SINCE}..HEAD")
    if missing:
        print("Behaviour changes without an 'Evidence:' trailer (AGENTS.md, Working Modes):")
        print("\n".join(f"  {m}" for m in missing))
        return 1
    print("ok: every behaviour change since the working modes carries its evidence")
    return 0


if __name__ == "__main__":
    sys.exit(main())
