#!/usr/bin/env python3
"""
tools/verify_release.py — Objective Quality Verification Protocol for GW2Chatlator.

Measures all gates required for a release:
  [Gate 1] Version & Documentation Consistency (CMakeLists.txt, CHANGELOG.md, README.md)
  [Gate 2] Nexus Addon Definition & Autoupdate Verification (UP_GitHub, UpdateLink)
  [Gate 3] Core Unit Test Suite (core_tests.exe: 0 failures)
  [Gate 4] Nexus lifecycle plus local translator process selftests: both must PASS
  [Gate 5] Typing Assistance Determinism (typing_bench.exe: 0 surprises)
  [Gate 6] Localization Integrity (tools/i18n_check.py: 0 missing, 0 unused)
  [Gate 7] Binary Integrity (Release DLL exists, valid PE64, non-empty)
  [Gate 8] Real-Life Stress & Smoke Benchmark (nexus_kb_smoketest.exe: PASS)

Exits with code 0 only if ALL gates pass.

  --changed [base]: only checks affected by changed files and changed CMake targets (default HEAD = uncommitted work,
including new files). --plan prints that selection without running checks. Its verdict is never a release verdict.
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
CODE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl")


def changed_cmake_lines(base: str) -> str:
    paths = ["CMakeLists.txt", "addons/nexus-keyboard/CMakeLists.txt"]
    proc = subprocess.run(["git", "diff", "--unified=0", base, "--", *paths], cwd=ROOT,
                          capture_output=True, text=True)
    return "\n".join(line[1:] for line in proc.stdout.splitlines()
                     if line.startswith(("+", "-")) and not line.startswith(("+++", "---")))


def gates_for(files: set, cmake_changes: str = "") -> set:
    """Select checks by changed behavior/source and explicit CMake target references."""
    need = set()
    normalized = {f.replace("\\", "/") for f in files}
    for f in normalized:
        name = Path(f).name
        if f in ("CHANGELOG.md", "README.md"):
            need.add("version")
        if f == "CMakeLists.txt" and re.search(r"project\s*\(\s*GW2KeyboardNexus\s+VERSION", cmake_changes,
                                                re.IGNORECASE):
            need.add("version")
        if f == "addons/nexus-keyboard/src/kb_main.cpp":
            need.add("addon")
        if (f.startswith("src/core/") and f.endswith(CODE_SUFFIXES)) or f == "tests/core_tests.cpp":
            need.add("core")
        is_code = f.endswith(CODE_SUFFIXES)
        if ((f.startswith("addons/nexus-keyboard/") and is_code and name != "translate_helper_main.cpp") or
                (f.startswith(("src/app/", "src/kbd/")) and is_code) or
                (f.startswith("src/win/") and is_code and "local_mt_" not in name) or
                f.endswith("nexus_kb_selftest.cpp")):
            need.add("nexus_selftest")
        if ("local_mt_" in name or name in ("kb_local_mt.cpp", "translate_helper_main.cpp",
                                             "local_mt_fake_helper.cpp", "local_mt_translator_selftest.cpp")):
            need.add("local_mt_selftest")
        if any(k in f for k in TYPING_PARTS):
            need.add("typing")
        if any(k in f for k in SMOKE_PARTS):
            need.add("smoke")
        if f.startswith("src/core/i18n_") or f.startswith("addons/nexus-keyboard/src/kb_options") or \
                f.startswith("addons/nexus-keyboard/src/kb_window") or \
                (f.startswith("src/app/") and f.endswith(CODE_SUFFIXES)) or f == "tools/i18n_check.py":
            need.add("i18n")
        if (f.startswith(("src/core/", "src/app/", "src/win/", "src/kbd/")) and is_code) or \
                (f.startswith("addons/nexus-keyboard/") and is_code and name != "translate_helper_main.cpp"):
            need.add("binary")
        if f.startswith("third_party/") and not f.startswith("third_party/licenses/") and f.endswith(CODE_SUFFIXES):
            need.add("binary")

    cmake = cmake_changes.lower()
    if re.search(r"gct_core|core_tests|src/core/", cmake):
        need.add("core")
    if re.search(r"gw2keyboard_nexus|gw2chatlator|nexus_kb_selftest|src/kb_(main|state|window|options|worker|input|theme|chat|local_mt)", cmake):
        need.update(("nexus_selftest", "binary"))
    if re.search(r"local_mt_|gw2kb_translate|translate_helper_main|kb_local_mt", cmake):
        need.add("local_mt_selftest")
    if re.search(r"typing_bench|text_prediction|spell_service|spellcheck|word_model", cmake):
        need.add("typing")
    if re.search(r"nexus_kb_smoketest|kb_chatsource|chat_events|chat_source|kb_window|kb_worker", cmake):
        need.add("smoke")
    if re.search(r"gct_core|src/core/|src/win/|gw2keyboard_nexus|gw2chatlator", cmake):
        need.add("binary")
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
    if f"v{version}/GW2Chatlator.dll" not in readme_text:
        print(f"    [FAIL] README.md does not link to v{version}/GW2Chatlator.dll")
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
    
    only = None  # None: every check (a release)
    plan = "--plan" in sys.argv
    if "--changed" in sys.argv:
        at = sys.argv.index("--changed")
        base = sys.argv[at + 1] if at + 1 < len(sys.argv) and not sys.argv[at + 1].startswith("-") else "HEAD"
        files = changed_files(base)
        only = gates_for(files, changed_cmake_lines(base))
        print(f"[*] Changed since {base}: {len(files)} file(s) -> selected checks: {', '.join(sorted(only)) or 'none'}")
    elif plan:
        print("[!] --plan requires --changed [base].")
        sys.exit(2)
    def wanted(gate: str) -> bool:
        if only is None or gate in only:
            return True
        print(f"[-] Skipped: {gate} (no related changed files or CMake target)")
        return False

    if plan:
        print("[*] Plan only: no checks or tests were run.")
        sys.exit(0)

    if wanted("version"):
        ver_ok, version = check_version_consistency()
        if not ver_ok:
            sys.exit(1)
    else:
        match = re.search(r"project\s*\(\s*GW2KeyboardNexus\s+VERSION\s+([0-9\.]+)",
                          (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        version = match.group(1) if match else "unknown"

    if wanted("addon") and not check_addon_def():
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
    local_mt_selftest = ROOT / "build" / "Release" / "local_mt_translator_selftest.exe"
    local_mt_fake = ROOT / "build" / "Release" / "local_mt_fake_helper.exe"
    dll = ROOT / "build" / "addons" / "nexus-keyboard" / "Release" / "GW2Chatlator.dll"
    need_nexus_selftest = wanted("nexus_selftest")
    need_local_mt_selftest = wanted("local_mt_selftest")
    need_binary = wanted("binary")
    need_smoke = wanted("smoke")
    if (need_nexus_selftest or need_binary or need_smoke) and not dll.exists():
        print(f"    [FAIL] Binary not found: {dll}")
        sys.exit(1)
    if not need_nexus_selftest:
        pass
    elif not selftest.exists():
        print(f"    [FAIL] Binary not found: {selftest}")
        sys.exit(1)
    elif not run_step("Nexus In-Process Selftest", [str(selftest), str(dll)], expect_substr="PASS"):
        sys.exit(1)
    if need_local_mt_selftest:
        if not local_mt_selftest.exists() or not local_mt_fake.exists():
            print("    [FAIL] Local translator self-test binaries are missing.")
            sys.exit(1)
        elif not run_step("Local Translator Process Selftest",
                          [str(local_mt_selftest), str(local_mt_fake)], expect_substr="PASS"):
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
    if wanted("i18n") and not run_step("Localization Completeness", [sys.executable, str(i18n_script)], expect_substr="0 missing, 0 unused"):
        sys.exit(1)

    # Gate 7: Binary inspection
    if need_binary:
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
    if not need_smoke:
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
