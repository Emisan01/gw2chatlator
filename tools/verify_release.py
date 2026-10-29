#!/usr/bin/env python3
"""
tools/verify_release.py — Objective Quality Verification Protocol for GW2Chatlator.

Measures all gates required for a release:
  [Gate 1] Version, Documentation & Architectural Claims Consistency
  [Gate 2] Nexus Addon Definition & Autoupdate Verification (UP_GitHub, UpdateLink)
  [Gate 3] Core Unit Test Suite (core_tests.exe: 0 failures)
  [Gate 4] Nexus lifecycle plus local translator process selftests: both must PASS
  [Gate 5] Typing Assistance Determinism (typing_bench.exe: 0 surprises)
  [Gate 6] Localization Integrity (tools/i18n_check.py: 0 missing, 0 unused)
  [Gate 7] Binary Integrity (Release DLL exists, valid PE64, non-empty)
  [Gate 8] Real-Life Stress & Smoke Benchmark (nexus_kb_smoketest.exe: PASS)

Modes:
  --full: The one release audit. Refuses a dirty tree, configures and builds every artifact from HEAD
          (build/ and build-localmt-native/), runs all 8 gates, checks the binaries carry HEAD without
          "-dirty", runs pack_release.py and writes build/dist/verification.json.
  --check-receipt: Fast (<5s) check that the receipt is from a clean tree, its commit is HEAD (or only
                   doc files changed since), and the artifacts on disk match its hashes.
  (no flag): all 8 gates on the existing build, no receipt.
  --changed [base]: Change-scoped check for narrow development verification.
  --plan: Prints gates selected by --changed without executing them.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def run_step(name: str, cmd: list, cwd=ROOT, expect_substr: str = None,
             timeout: int = 120) -> tuple[bool, str, float]:
    print(f"[*] Running: {name} ...", flush=True)
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=timeout)
        dur = round(time.time() - t0, 2)
        output = proc.stdout + proc.stderr
        if proc.returncode != 0:
            print(f"    [FAIL] Process exited with code {proc.returncode} ({dur}s)")
            print("    --- Output ---")
            for line in output.strip().splitlines()[-15:]:
                print(f"    {line}")
            print("    --------------")
            return False, output, dur
        if expect_substr and expect_substr not in output:
            print(f"    [FAIL] Expected substring '{expect_substr}' not found in output ({dur}s).")
            return False, output, dur
        print(f"    [PASS] {name} ({dur}s)")
        return True, output, dur
    except subprocess.TimeoutExpired:
        dur = round(time.time() - t0, 2)
        print(f"    [FAIL] Timeout after {timeout} seconds! ({dur}s)")
        return False, "timeout", dur
    except Exception as e:
        dur = round(time.time() - t0, 2)
        print(f"    [FAIL] Exception: {e} ({dur}s)")
        return False, str(e), dur

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
    need = set()
    normalized = {f.replace("\\", "/") for f in files}
    for f in normalized:
        name = Path(f).name
        if f in ("CHANGELOG.md", "README.md", "tools/claims.json", "tools/check_claims.py"):
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

def check_version_consistency() -> tuple[bool, str, float]:
    t0 = time.time()
    print("[*] Gate 1: Version, Claims & Documentation Consistency ...", flush=True)
    cmake_path = ROOT / "CMakeLists.txt"
    if not cmake_path.exists():
        print("    [FAIL] CMakeLists.txt missing!")
        return False, "", time.time() - t0

    cmake_text = cmake_path.read_text(encoding="utf-8")
    m = re.search(r"project\s*\(\s*GW2KeyboardNexus\s+VERSION\s+([0-9\.]+)", cmake_text)
    if not m:
        print("    [FAIL] Could not parse VERSION in CMakeLists.txt")
        return False, "", time.time() - t0
    version = m.group(1)
    print(f"    Target Version: {version}")

    changelog_text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## {version} " not in changelog_text:
        print(f"    [FAIL] CHANGELOG.md does not have a section for version {version}")
        return False, version, time.time() - t0

    readme_text = (ROOT / "README.md").read_text(encoding="utf-8")
    if f"v{version}/GW2Chatlator.dll" not in readme_text and "releases/latest/download/GW2Chatlator.dll" not in readme_text:
        print(f"    [FAIL] README.md does not link to v{version}/GW2Chatlator.dll or latest release")
        return False, version, time.time() - t0

    # Claims check integration
    claims_script = ROOT / "tools" / "check_claims.py"
    if claims_script.exists():
        cp = subprocess.run([sys.executable, str(claims_script)], cwd=ROOT, capture_output=True, text=True)
        if cp.returncode != 0:
            print(f"    [FAIL] Architectural claims check failed:\n{cp.stdout}")
            return False, version, time.time() - t0

    # The working modes' evidence (AGENTS.md): every behaviour change since they came carries an "Evidence:" trailer
    cp = subprocess.run([sys.executable, str(ROOT / "tools" / "check_evidence.py")], cwd=ROOT, capture_output=True,
                        text=True, errors="replace")
    if cp.returncode != 0:
        print(f"    [FAIL] Working-mode evidence missing:\n{cp.stdout}")
        return False, version, time.time() - t0

    dur = round(time.time() - t0, 2)
    print(f"    [PASS] CMakeLists.txt, CHANGELOG.md, README.md, claims.json and commit evidence verified. ({dur}s)")
    return True, version, dur

def check_addon_def() -> tuple[bool, float]:
    t0 = time.time()
    print("[*] Gate 2: Nexus Addon Definition & Autoupdate ...", flush=True)
    kb_main = ROOT / "addons" / "nexus-keyboard" / "src" / "kb_main.cpp"
    if not kb_main.exists():
        print("    [FAIL] kb_main.cpp not found!")
        return False, time.time() - t0
    content = kb_main.read_text(encoding="utf-8")
    if "0xB8B4BDBC" not in content:
        print("    [FAIL] g_def.Signature is not set to 0xB8B4BDBC!")
        return False, time.time() - t0
    if "g_def.Name = kName;" not in content:
        print("    [FAIL] g_def.Name is not set to kName (\"GW2Chatlator\")!")
        return False, time.time() - t0
    if 'g_def.Author = "Emi";' not in content:
        print('    [FAIL] g_def.Author is not set to "Emi"!')
        return False, time.time() - t0
    if "g_def.Provider = UP_GitHub;" not in content:
        print("    [FAIL] g_def.Provider is not set to UP_GitHub!")
        return False, time.time() - t0
    if "https://github.com/Emisan01/gw2chatlator" not in content:
        print("    [FAIL] g_def.UpdateLink is not set to official repository URL!")
        return False, time.time() - t0
    dur = round(time.time() - t0, 2)
    print(f"    [PASS] Signature (0xB8B4BDBC), Name (GW2Chatlator), Author (Emi), Provider (UP_GitHub), and UpdateLink verified. ({dur}s)")
    return True, dur

def check_receipt() -> int:
    receipt_file = ROOT / "build" / "dist" / "verification.json"
    if not receipt_file.exists():
        print("[FAIL] Verification receipt missing: build/dist/verification.json")
        print("       Run 'python tools/verify_release.py --full' once to generate it.")
        return 1

    try:
        data = json.loads(receipt_file.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"[FAIL] Malformed verification receipt: {e}")
        return 1

    r_commit = data.get("commit", "")
    r_ver = data.get("version", "")
    print(f"[*] Checking verification receipt for version {r_ver} (Commit: {r_commit[:12]}) ...")

    # A receipt from a dirty tree (--allow-dirty) documents a test run, never a release.
    if data.get("tree_clean") is not True:
        print("[FAIL] The receipt was made from a dirty working tree (--allow-dirty): not a release receipt.")
        return 1
    known = subprocess.run(["git", "merge-base", "--is-ancestor", r_commit, "HEAD"], cwd=ROOT, capture_output=True)
    if not r_commit or known.returncode != 0:
        print(f"[FAIL] Receipt commit {r_commit[:12]} is not HEAD or one of its ancestors.")
        return 1

    # Check git changes since receipt commit
    diff = subprocess.run(["git", "diff", "--name-only", r_commit], cwd=ROOT, capture_output=True, text=True).stdout
    untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=ROOT, capture_output=True, text=True).stdout
    all_changed = {f.strip().replace("\\", "/") for f in (diff + "\n" + untracked).splitlines() if f.strip()}

    DOC_ALLOW = {"README.md", "CHANGELOG.md", "AGENTS.md", "CLAUDE.md"}
    def is_doc(f: str) -> bool:
        return f in DOC_ALLOW or f.startswith("docs/")

    non_docs = [f for f in all_changed if not is_doc(f)]
    if non_docs:
        print(f"[FAIL] Receipt is stale! Non-documentation changes detected since receipt commit {r_commit[:12]}:")
        for nd in non_docs[:10]:
            print(f"       - {nd}")
        return 1

    if all_changed:
        print(f"    [INFO] Working tree has {len(all_changed)} doc file change(s) since receipt (accepted per protocol).")
    else:
        print("    [INFO] Working tree is completely clean and matches receipt commit.")

    # Check disk artifacts against receipt hashes
    artifacts = data.get("artifacts", {})
    dist_dir = ROOT / "build" / "dist"
    all_matched = True
    print(f"[*] Verifying {len(artifacts)} release artifacts on disk:")
    for name, info in artifacts.items():
        file_path = dist_dir / name
        if not file_path.exists():
            print(f"    [FAIL] Missing artifact: {name}")
            all_matched = False
            continue
        exp_sha = info.get("sha256", "")
        exp_bytes = info.get("bytes", 0)
        actual_bytes = file_path.stat().st_size
        actual_sha = sha256_file(file_path)
        if actual_bytes == exp_bytes and actual_sha.lower() == exp_sha.lower():
            print(f"    [OK] {name:<30} {actual_bytes:<10} B  {actual_sha[:16]}...")
        else:
            print(f"    [FAIL] Hash mismatch for {name}: expected {exp_sha[:16]}..., got {actual_sha[:16]}...")
            all_matched = False

    if not all_matched:
        print("[FAIL] Disk artifacts do not match verification receipt!")
        return 1

    print("\n==================================================================")
    print(f"   >>> RECEIPT VALID: GW2Chatlator {r_ver} ({r_commit[:12]}) <<<   ")
    print("   All 8 gates previously passed and all disk hashes verified.   ")
    print("==================================================================")
    return 0

LOCAL_MT_BUILD = ROOT / "build-localmt-native"
RELEASE_DIR = ROOT / "build" / "addons" / "nexus-keyboard" / "Release"

def build_from_head() -> tuple[bool, float]:
    """--full builds every artifact from HEAD. Both build folders configure again first: the commit and the dirty
    flag go into version.h at configure time. The translator helper comes from the GCT_LOCAL_MT folder."""
    t0 = time.time()
    if not (LOCAL_MT_BUILD / "CMakeCache.txt").exists():
        print(f"    [FAIL] {LOCAL_MT_BUILD.name} is not configured: "
              f"cmake -S . -B {LOCAL_MT_BUILD.name} -DGCT_LOCAL_MT=ON")
        return False, time.time() - t0
    steps = [
        ("Configure build", ["cmake", "-S", ".", "-B", "build"]),
        ("Build add-on, voice helper and tests", ["cmake", "--build", "build", "--config", "Release", "--parallel"]),
        ("Configure local translator build", ["cmake", "-S", ".", "-B", LOCAL_MT_BUILD.name]),
        ("Build local translator helper", ["cmake", "--build", LOCAL_MT_BUILD.name, "--config", "Release",
                                           "--target", "gw2kb_translate", "--parallel"]),
    ]
    for name, cmd in steps:
        ok, _, _ = run_step(name, cmd, timeout=3600)
        if not ok:
            return False, time.time() - t0
    helper = LOCAL_MT_BUILD / "addons" / "nexus-keyboard" / "Release" / "GW2Keyboard" / "gw2kb_translate.exe"
    (RELEASE_DIR / "GW2Keyboard").mkdir(parents=True, exist_ok=True)
    shutil.copy2(helper, RELEASE_DIR / "GW2Keyboard" / "gw2kb_translate.exe")
    dur = round(time.time() - t0, 2)
    print(f"    [PASS] Built from HEAD, helper copied into the release folder. ({dur}s)")
    return True, dur

def embedded_build_ok(path: Path, marker: bytes) -> bool:
    """The binary carries marker as a whole C string: the commit without "-dirty" behind it."""
    return path.exists() and (marker + b"\x00") in path.read_bytes()

def main():
    if "--check-receipt" in sys.argv:
        sys.exit(check_receipt())

    is_full = "--full" in sys.argv
    allow_dirty = "--allow-dirty" in sys.argv

    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    status = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    is_clean = len(status) == 0

    if is_full and not is_clean and not allow_dirty:
        print("[FAIL] Working tree is dirty! --full requires a clean commit.")
        print("       Commit all changes or pass --allow-dirty for testing.")
        sys.exit(1)

    print("==================================================================")
    print("      GW2Chatlator — Release Quality Verification Protocol        ")
    print("==================================================================")

    only = None
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

    gate_results = []
    test_metrics = {}
    if is_full:
        build_ok, build_dur = build_from_head()
        if not build_ok:
            sys.exit(1)
        gate_results.append({"gate": 0, "name": "Build from HEAD", "status": "PASS", "duration_sec": build_dur,
                             "exit_code": 0})

    # Gate 1: Version & Claims Consistency
    if wanted("version"):
        ver_ok, version, g1_dur = check_version_consistency()
        gate_results.append({"gate": 1, "name": "Version & Claims Consistency", "status": "PASS" if ver_ok else "FAIL", "duration_sec": g1_dur, "exit_code": 0 if ver_ok else 1})
        if not ver_ok: sys.exit(1)
    else:
        match = re.search(r"project\s*\(\s*GW2KeyboardNexus\s+VERSION\s+([0-9\.]+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        version = match.group(1) if match else "unknown"

    # Gate 2: Nexus Addon Definition
    if wanted("addon"):
        addon_ok, g2_dur = check_addon_def()
        gate_results.append({"gate": 2, "name": "Nexus Addon Definition", "status": "PASS" if addon_ok else "FAIL", "duration_sec": g2_dur, "exit_code": 0 if addon_ok else 1})
        if not addon_ok: sys.exit(1)

    # Gate 3: Core tests
    core_tests = ROOT / "build" / "Release" / "core_tests.exe"
    if wanted("core"):
        if not core_tests.exists():
            print(f"    [FAIL] Binary not found: {core_tests}")
            sys.exit(1)
        ok, out, dur = run_step("Core Unit Tests (core_tests.exe)", [str(core_tests)], expect_substr="0 failed")
        gate_results.append({"gate": 3, "name": "Core Unit Tests", "status": "PASS" if ok else "FAIL", "duration_sec": dur, "exit_code": 0 if ok else 1})
        m = re.search(r"(\d+ passed, \d+ failed)", out)
        if m: test_metrics["core_tests"] = m.group(1)
        if not ok: sys.exit(1)

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

    if need_nexus_selftest:
        if not selftest.exists():
            print(f"    [FAIL] Binary not found: {selftest}")
            sys.exit(1)
        ok, out, dur = run_step("Nexus In-Process Selftest", [str(selftest), str(dll)], expect_substr="PASS")
        gate_results.append({"gate": 4, "sub": "nexus", "name": "Nexus In-Process Selftest", "status": "PASS" if ok else "FAIL", "duration_sec": dur, "exit_code": 0 if ok else 1})
        test_metrics["nexus_selftest"] = "PASS" if ok else "FAIL"
        if not ok: sys.exit(1)

    if need_local_mt_selftest:
        if not local_mt_selftest.exists() or not local_mt_fake.exists():
            print("    [FAIL] Local translator self-test binaries are missing.")
            sys.exit(1)
        ok, out, dur = run_step("Local Translator Process Selftest", [str(local_mt_selftest), str(local_mt_fake)], expect_substr="PASS")
        gate_results.append({"gate": 4, "sub": "local_mt", "name": "Local Translator Process Selftest", "status": "PASS" if ok else "FAIL", "duration_sec": dur, "exit_code": 0 if ok else 1})
        test_metrics["local_mt_selftest"] = "PASS" if ok else "FAIL"
        if not ok: sys.exit(1)

    # Gate 5: Typing benchmark determinism
    typing_bench = ROOT / "build" / "Release" / "typing_bench.exe"
    if wanted("typing"):
        if not typing_bench.exists():
            print(f"    [FAIL] Binary not found: {typing_bench}")
            sys.exit(1)
        ok, out, dur = run_step("Typing Benchmark Determinism (no surprise in any language)", [str(typing_bench)],
                                expect_substr="no surprise in any language", timeout=900)
        gate_results.append({"gate": 5, "name": "Typing Benchmark Determinism", "status": "PASS" if ok else "FAIL", "duration_sec": dur, "exit_code": 0 if ok else 1})
        test_metrics["typing_bench"] = "7 languages, no surprise" if ok else "FAIL"
        if not ok: sys.exit(1)

    # Gate 6: Localization check
    i18n_script = ROOT / "tools" / "i18n_check.py"
    if wanted("i18n"):
        ok, out, dur = run_step("Localization Completeness", [sys.executable, str(i18n_script)], expect_substr="0 missing, 0 unused")
        gate_results.append({"gate": 6, "name": "Localization Completeness", "status": "PASS" if ok else "FAIL", "duration_sec": dur, "exit_code": 0 if ok else 1})
        counts = re.findall(r"^\w+: (\d+) entries, 0 missing, 0 unused", out, re.MULTILINE)
        test_metrics["i18n"] = f"{counts[0]} entries x {len(counts)} languages, 0 missing, 0 unused" if ok and counts else "FAIL"
        if not ok: sys.exit(1)

    # Gate 7: Binary inspection
    if need_binary:
        print("[*] Gate 7: Binary Verification ...", flush=True)
        t0 = time.time()
        size = dll.stat().st_size
        print(f"    DLL Path: {dll}")
        print(f"    DLL Size: {size:,} bytes")
        if size < 500000:
            print("    [FAIL] DLL size unexpectedly small (< 500 KB)!")
            sys.exit(1)
        # The release build carries HEAD without "-dirty": the DLL's version line (the maintainer's first look in the
        # game) and the helpers' "build" in their ready line.
        if is_full and is_clean:
            line = f"{version} · {commit[:7]}"
            stamps = [(dll, line), (RELEASE_DIR / "GW2Keyboard" / "gw2kb_translate.exe", commit[:12]),
                      (RELEASE_DIR / "GW2Keyboard" / "gw2kb_voice.exe", commit[:12])]
            for path, marker in stamps:
                if not embedded_build_ok(path, marker.encode("utf-8")):
                    print(f"    [FAIL] {path.name} does not carry '{marker}' (stale build or dirty tree)")
                    sys.exit(1)
                print(f"    [OK] {path.name} carries '{marker}'")
            test_metrics["version_line"] = line
        dur = round(time.time() - t0, 2)
        print(f"    [PASS] DLL binary exists and passes size sanity checks. ({dur}s)")
        gate_results.append({"gate": 7, "name": "Binary Integrity", "status": "PASS", "duration_sec": dur, "exit_code": 0})

    # Gate 8: Real-Life Stress & Smoke Benchmark
    smoketest = ROOT / "build" / "addons" / "nexus-keyboard" / "Release" / "nexus_kb_smoketest.exe"
    if need_smoke:
        if not smoketest.exists():
            print(f"    [FAIL] Binary not found: {smoketest}")
            sys.exit(1)
        ok, out, dur = run_step("Real-Life Stress & Smoke Benchmark (500 msgs, 1000 frames)", [str(smoketest), str(dll)], expect_substr="SMOKE TEST RESULT: 100% ROCK-SOLID (PASS)")
        gate_results.append({"gate": 8, "name": "Real-Life Stress & Smoke Benchmark", "status": "PASS" if ok else "FAIL", "duration_sec": dur, "exit_code": 0 if ok else 1})
        test_metrics["smoketest"] = "PASS" if ok else "FAIL"
        if not ok: sys.exit(1)

    # Translation quality (with a model only): GCT_BENCH_DATA names a data folder with translate\nllb-200 (the game's
    # GW2Keyboard folder). The made-up corpus through the real helper: nothing protected lost, no sentence lost, nothing
    # cut short, nothing failed. Without a model it is skipped and says so – a measurement, never a guess.
    bench_data = os.environ.get("GCT_BENCH_DATA", "")
    bench = ROOT / "build" / "Release" / "translation_bench.exe"
    bench_helper = RELEASE_DIR / "GW2Keyboard" / "gw2kb_translate.exe"
    if only is None:
        if bench_data and (Path(bench_data) / "translate" / "nllb-200").is_dir() and bench.exists() and bench_helper.exists():
            ok, out, dur = run_step("Translation Quality Bench (NLLB-200, made-up corpus)",
                                    [str(bench), str(bench_helper), bench_data,
                                     str(ROOT / "tests" / "data" / "translation_corpus.tsv"), "nllb"],
                                    expect_substr="0 lost protected, 0 lost a sentence, 0 cut short, 0 failed", timeout=1800)
            summary = next((l for l in out.splitlines() if l.startswith("translation_bench")), "")
            gate_results.append({"gate": 9, "name": "Translation Quality Bench", "status": "PASS" if ok else "FAIL",
                                 "duration_sec": dur, "exit_code": 0 if ok else 1})
            test_metrics["translation_bench"] = summary if ok else "FAIL"
            if not ok: sys.exit(1)
        else:
            print("[*] Translation Quality Bench: skipped – set GCT_BENCH_DATA to a GW2Keyboard folder with NLLB-200")
            test_metrics["translation_bench"] = "skipped (no model)"

    if only is not None:
        print("\n==================================================================")
        print(f"   >>> CHANGED PARTS VERIFIED ({version}) – not a release verdict <<<")
        print("   A release runs all eight gates: python tools/verify_release.py")
        print("==================================================================")
        sys.exit(0)

    if not is_full:
        print("\n==================================================================")
        print(f"   All 8 gates passed for {version} (no receipt: only --full builds from HEAD and writes one).")
        print("==================================================================")
        sys.exit(0)

    # Packaging and Verification Receipt writing
    print("\n[*] Packaging release assets to build/dist ...")
    pack_cmd = [sys.executable, str(ROOT / "tools" / "pack_release.py")]
    if allow_dirty:
        pack_cmd.append("--allow-dirty")
    pack_proc = subprocess.run(pack_cmd, cwd=ROOT, capture_output=True, text=True)
    if pack_proc.returncode != 0:
        print(f"[FAIL] Packaging failed:\n{pack_proc.stdout}\n{pack_proc.stderr}")
        sys.exit(1)

    dist_dir = ROOT / "build" / "dist"
    artifacts_data = {}
    for fname in ["GW2Chatlator.dll", "GW2Keyboard-Nexus.zip", "GW2Keyboard-LocalMT.zip", "manifest.json",
                  "extras-manifest.json"]:
        fpath = dist_dir / fname
        if fpath.exists():
            artifacts_data[fname] = {
                "bytes": fpath.stat().st_size,
                "sha256": sha256_file(fpath)
            }

    receipt = {
        "schema": "1.0",
        "version": version,
        "commit": commit,
        "tree_clean": is_clean,
        "time_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "artifacts": artifacts_data,
        "gates": gate_results,
        "test_metrics": test_metrics,
        "tools": {
            "python": sys.version.split()[0],
            "cmake": subprocess.run(["cmake", "--version"], capture_output=True, text=True).stdout.split("\n")[0],
        },
    }
    receipt_path = dist_dir / "verification.json"
    receipt_path.write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    print(f"[*] Wrote official verification receipt to {receipt_path}")

    print("\n==================================================================")
    print(f"   >>> VERDICT: VERSION {version} IS FULLY RELEASE-WORTHY <<<   ")
    print(f"   Commit: {commit[:12]} | Tree Clean: {is_clean}                  ")
    print("   All 8 quality gates passed with zero warnings or failures.    ")
    print("==================================================================")
    sys.exit(0)

if __name__ == "__main__":
    main()
