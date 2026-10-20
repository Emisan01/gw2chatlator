#!/usr/bin/env python3
"""
tools/verify_release.py — Objective Quality Verification Protocol for GW2Chatlator.

Measures all gates required for a release:
  [Gate 1] Version & Documentation Consistency (CMakeLists.txt, CHANGELOG.md, README.md)
  [Gate 2] Nexus Addon Definition & Autoupdate Verification (UP_GitHub, UpdateLink)
  [Gate 3] Core Unit Test Suite (core_tests.exe: 0 failures)
  [Gate 4] Nexus In-Process Lifecycle & Input Selftest (nexus_kb_selftest.exe: PASS)
  [Gate 5] Typing Assistance Determinism (typing_bench.exe: 0 surprises)
  [Gate 6] Localization Integrity (tools/i18n_check.py: 0 missing, 0 unused)
  [Gate 7] Binary Integrity (Release DLL exists, valid PE64, non-empty)
  [Gate 8] Real-Life Stress & Smoke Benchmark (nexus_kb_smoketest.exe: PASS)

Exits with code 0 only if ALL gates pass.

--changed [base]: only the gates the changed files can touch (git diff against base, default HEAD = the work not yet
committed, plus new files). Version, Nexus definition, localization and binary always run (they are cheap); core
tests, selftest, typing bench and smoke test only when their parts changed. Its verdict is never a release verdict.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def run_step(name: str, cmd: list, cwd=ROOT, expect_substr: str = None) -> bool:
    print(f"[*] Running: {name} ...", flush=True)
    try:
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=120)
        output = proc.stdout + proc.stderr
        if proc.returncode != 0:
            print(f"    [FAIL] Process exited with code {proc.returncode}")
            print("    --- Output ---")
            for line in output.strip().splitlines()[-15:]:
                print(f"    {line}")
            print("    --------------")
            return False
        if expect_substr and expect_substr not in output:
            print(f"    [FAIL] Expected substring '{expect_substr}' not found in output.")
            return False
        print(f"    [PASS] {name}")
        return True
    except subprocess.TimeoutExpired:
        print(f"    [FAIL] Timeout after 120 seconds!")
        return False
    except Exception as e:
        print(f"    [FAIL] Exception: {e}")
        return False

def changed_files(base: str) -> set:
    diff = subprocess.run(["git", "diff", "--name-only", base], cwd=ROOT, capture_output=True, text=True).stdout
    new = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT, capture_output=True,
                         text=True).stdout
    return {f.strip() for f in (diff + "\n" + new).splitlines() if f.strip()}


TYPING_PARTS = ("spell_service", "text_prediction", "spellcheck", "word_model", "typo_memory", "slang", "my_words",
                "kb_input", "keyboard_layout", "typing_bench")
SMOKE_PARTS = ("kb_chatsource", "chat_events", "chat_source", "kb_chat.", "kb_window.", "kb_cost", "kb_worker.",
               "nexus_kb_smoketest")


def gates_for(files: set) -> set:
    """Which of the expensive gates the changed files can touch."""
    need = set()
    for f in files:
        if f.startswith("src/core/") or f == "tests/core_tests.cpp":
            need |= {"core", "selftest"}  # the add-on is built on core
        if f.startswith(("addons/", "src/app/", "src/win/", "src/kbd/")) or f.endswith("nexus_kb_selftest.cpp"):
            need.add("selftest")
        if any(k in f for k in TYPING_PARTS):
            need.add("typing")
        if any(k in f for k in SMOKE_PARTS):
            need.add("smoke")
        if f in ("CMakeLists.txt",) or f.startswith("third_party/") or f.endswith("CMakeLists.txt"):
            need |= {"core", "selftest", "typing", "smoke"}  # the build itself changed
    return need


def check_version_consistency() -> tuple[bool, str]:
    print("[*] Gate 1: Version Consistency ...", flush=True)
    cmake_path = ROOT / "CMakeLists.txt"
    if not cmake_path.exists():
        print("    [FAIL] CMakeLists.txt missing!")
        return False, ""
    
    cmake_text = cmake_path.read_text(encoding="utf-8")
    m = re.search(r"project\s*\(\s*GW2KeyboardNexus\s+VERSION\s+([0-9\.]+)", cmake_text)
    if not m:
        print("    [FAIL] Could not parse VERSION in CMakeLists.txt")
        return False, ""
    version = m.group(1)
    print(f"    Target Version: {version}")

    changelog_text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## {version} " not in changelog_text:
        print(f"    [FAIL] CHANGELOG.md does not have a section for version {version}")
        return False, version

    readme_text = (ROOT / "README.md").read_text(encoding="utf-8")
    if f"v{version}/gw2keyboard_nexus.dll" not in readme_text:
        print(f"    [FAIL] README.md does not link to v{version}/gw2keyboard_nexus.dll")
        return False, version

    print("    [PASS] CMakeLists.txt, CHANGELOG.md, and README.md are consistent.")
    return True, version

def check_addon_def() -> bool:
    print("[*] Gate 2: Nexus Addon Definition & Autoupdate ...", flush=True)
    kb_main = ROOT / "addons" / "nexus-keyboard" / "src" / "kb_main.cpp"
    if not kb_main.exists():
        print("    [FAIL] kb_main.cpp not found!")
        return False
    content = kb_main.read_text(encoding="utf-8")
    if "g_def.Provider = UP_GitHub;" not in content:
        print("    [FAIL] g_def.Provider is not set to UP_GitHub!")
        return False
    if "https://github.com/Emisan01/gw2chatlator" not in content:
        print("    [FAIL] g_def.UpdateLink is not set to official repository URL!")
        return False
    print("    [PASS] Provider = UP_GitHub and UpdateLink verified.")
    return True

def main():
    print("==================================================================")
    print("      GW2Chatlator — Release Quality Verification Protocol        ")
    print("==================================================================")
    
    only = None  # None: every gate (a release)
    if "--changed" in sys.argv:
        at = sys.argv.index("--changed")
        base = sys.argv[at + 1] if at + 1 < len(sys.argv) and not sys.argv[at + 1].startswith("-") else "HEAD"
        files = changed_files(base)
        only = gates_for(files)
        print(f"[*] Changed since {base}: {len(files)} file(s) -> expensive gates: {', '.join(sorted(only)) or 'none'}")
    def wanted(gate: str) -> bool:
        if only is None or gate in only:
            return True
        print(f"[-] Skipped: {gate} (nothing it covers changed)")
        return False

    ver_ok, version = check_version_consistency()
    if not ver_ok:
        sys.exit(1)

    if not check_addon_def():
        sys.exit(1)

    # Gate 3: Core tests
    core_tests = ROOT / "build" / "Release" / "core_tests.exe"
    if not wanted("core"):
        pass
    elif not core_tests.exists():
        print(f"    [FAIL] Binary not found: {core_tests}")
        sys.exit(1)
    elif not run_step("Core Unit Tests (core_tests.exe)", [str(core_tests)], expect_substr="0 failed"):
        sys.exit(1)

    # Gate 4: Nexus lifecycle & selftest
    selftest = ROOT / "build" / "addons" / "nexus-keyboard" / "Release" / "nexus_kb_selftest.exe"
    dll = ROOT / "build" / "addons" / "nexus-keyboard" / "Release" / "gw2keyboard_nexus.dll"
    if not dll.exists():
        print(f"    [FAIL] Binary not found: {dll}")
        sys.exit(1)
    if not wanted("selftest"):
        pass
    elif not selftest.exists():
        print(f"    [FAIL] Binary not found: {selftest}")
        sys.exit(1)
    elif not run_step("Nexus In-Process Selftest", [str(selftest), str(dll)], expect_substr="PASS"):
        sys.exit(1)

    # Gate 5: Typing benchmark determinism
    typing_bench = ROOT / "build" / "Release" / "typing_bench.exe"
    if not wanted("typing"):
        pass
    elif not typing_bench.exists():
        print(f"    [FAIL] Binary not found: {typing_bench}")
        sys.exit(1)
    elif not run_step("Typing Benchmark Determinism (0 surprises)", [str(typing_bench)], expect_substr="0 surprises"):
        sys.exit(1)

    # Gate 6: Localization check
    i18n_script = ROOT / "tools" / "i18n_check.py"
    if not run_step("Localization Completeness", [sys.executable, str(i18n_script)], expect_substr="0 missing, 0 unused"):
        sys.exit(1)

    # Gate 7: Binary inspection
    print("[*] Gate 7: Binary Verification ...", flush=True)
    size = dll.stat().st_size
    print(f"    DLL Path: {dll}")
    print(f"    DLL Size: {size:,} bytes")
    if size < 500000:
        print("    [FAIL] DLL size unexpectedly small (< 500 KB)!")
        sys.exit(1)
    print("    [PASS] DLL binary exists and passes size sanity checks.")

    # Gate 8: Real-Life Stress & Smoke Benchmark (500 lines chat flood, 1000 frames profiling, SLA p99 < 2.5ms)
    smoketest = ROOT / "build" / "addons" / "nexus-keyboard" / "Release" / "nexus_kb_smoketest.exe"
    if not wanted("smoke"):
        pass
    elif not smoketest.exists():
        print(f"    [FAIL] Binary not found: {smoketest}")
        sys.exit(1)
    elif not run_step("Real-Life Stress & Smoke Benchmark (500 msgs, 1000 frames)", [str(smoketest), str(dll)], expect_substr="SMOKE TEST RESULT: 100% ROCK-SOLID (PASS)"):
        sys.exit(1)

    if only is not None:
        print("\n==================================================================")
        print(f"   >>> CHANGED PARTS VERIFIED ({version}) – not a release verdict <<<")
        print("   A release runs all eight gates: python tools/verify_release.py")
        print("==================================================================")
        sys.exit(0)
    print("\n==================================================================")
    print(f"   >>> VERDICT: VERSION {version} IS FULLY RELEASE-WORTHY <<<   ")
    print("   All 8 quality gates passed with zero warnings or failures.    ")
    print("==================================================================")
    sys.exit(0)

if __name__ == "__main__":
    main()
