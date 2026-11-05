#!/usr/bin/env python3
"""
tools/pack_release.py — Packages GW2Chatlator release assets and generates manifests.

Outputs to build/dist:
  - GW2Chatlator.dll
  - GW2Keyboard-Nexus.zip
  - GW2Keyboard-LocalMT.zip
  - GW2Keyboard-GPU.zip (helper for NVIDIA cards, from build-localmt-gpu2)
  - manifest.json (for release tag vX.Y.Z)
  - extras-manifest.json (for release tag extras)
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
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GPU_EXE = ROOT / "build-localmt-gpu2" / "addons" / "nexus-keyboard" / "Release" / "GW2Keyboard" / "gw2kb_translate.exe"
CUDA_BIN = Path(r"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\bin")
CUDA_DLLS = ("cublas64_12.dll", "cublasLt64_12.dll", "cudart64_12.dll")
CUDA_EULA = CUDA_BIN.parent / "EULA.txt"
OPENMP_GLOB = "Microsoft Visual Studio/*/*/VC/Redist/MSVC/14.*/x64/Microsoft.VC14*.OpenMP/vcomp140.dll"

def find_openmp() -> Path:
    """The compiler's OpenMP runtime, redistributable: the helpers need it beside them."""
    found = sorted(Path(os.environ.get("ProgramFiles", r"C:\Program Files")).glob(OPENMP_GLOB))
    if not found:
        print("[FAIL] vcomp140.dll not found in the Visual Studio redistributables")
        sys.exit(1)
    return found[0]

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def inspect_zip(zip_path: Path):
    entries = []
    with zipfile.ZipFile(zip_path, "r") as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            data = z.read(info.filename)
            digest = hashlib.sha256(data).hexdigest()
            entries.append({
                "path": info.filename.replace("\\", "/"),
                "bytes": info.file_size,
                "sha256": digest
            })
    return entries

def get_version() -> str:
    cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\s*\(\s*GW2KeyboardNexus\s+VERSION\s+([0-9\.]+)", cmake_text)
    return m.group(1) if m else "2.6.3"

def helper_ready(exe: Path) -> dict:
    """Starts the translator helper once and returns its parsed "ready" line ({} when it gives none)."""
    with tempfile.TemporaryDirectory() as data:
        proc = subprocess.Popen([str(exe.resolve()), "--data", data], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            line = proc.stdout.readline().decode("utf-8", "replace").strip()
            proc.stdin.write(b'{"cmd":"quit"}\n')
            proc.stdin.close()
            proc.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()
            line = ""
    try:
        ready = json.loads(line)
    except json.JSONDecodeError:
        return {}
    return ready if isinstance(ready, dict) and ready.get("evt") == "ready" else {}

def get_git_info():
    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    status = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    return commit, len(status) == 0

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build",
                        help="CMake build directory (default: build)")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="Allow packaging from a dirty git tree (default: aborts)")
    args = parser.parse_args()

    commit, is_clean = get_git_info()
    if not is_clean and not args.allow_dirty:
        print("[FAIL] Git working tree is dirty! Aborting release packaging.")
        print("       Commit all changes or pass --allow-dirty for local scratch builds.")
        sys.exit(1)

    version = get_version()
    build_rel = args.build_dir / "addons" / "nexus-keyboard" / "Release"
    dist_dir = args.build_dir / "dist"
    dll = build_rel / "GW2Chatlator.dll"
    voice_exe = build_rel / "GW2Keyboard" / "gw2kb_voice.exe"
    voice_dir = build_rel / "GW2Keyboard" / "voice"
    local_mt_exe = build_rel / "GW2Keyboard" / "gw2kb_translate.exe"

    if not dll.exists():
        print(f"[FAIL] Release DLL not found: {dll}")
        sys.exit(1)

    dist_dir.mkdir(parents=True, exist_ok=True)
    out_chatlator = dist_dir / "GW2Chatlator.dll"
    shutil.copy2(dll, out_chatlator)
    legacy_dll = dist_dir / "gw2keyboard_nexus.dll"
    if legacy_dll.exists():
        legacy_dll.unlink()
    print(f"[*] Copied standalone DLL to {out_chatlator} ({out_chatlator.stat().st_size:,} bytes)")

    out_zip = dist_dir / "GW2Keyboard-Nexus.zip"
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(dll, "GW2Chatlator.dll")
        if voice_exe.exists():
            z.write(voice_exe, "GW2Keyboard/gw2kb_voice.exe")
        if voice_dir.exists():
            for f in sorted(voice_dir.glob("*.dll")):
                z.write(f, f"GW2Keyboard/voice/{f.name}")
        notices = ROOT / "THIRD_PARTY_NOTICES.txt"
        if notices.exists():
            z.write(notices, "GW2Keyboard/THIRD_PARTY_NOTICES.txt")
        readme_text = (
            "GW2Chatlator for Nexus\n"
            "Put GW2Chatlator.dll in your Guild Wars 2\\addons folder.\n"
            "The GW2Keyboard folder contains the optional voice helper and whisper libraries.\n"
            "Nothing is downloaded by itself: Events: Chat and a voice model are offered in the add-on, on your click.\n"
        )
        z.writestr("GW2Keyboard/README.txt", readme_text)
    print(f"[*] Packaged {out_zip} ({out_zip.stat().st_size:,} bytes)")

    out_local_mt = None
    if local_mt_exe.exists():
        out_local_mt = dist_dir / "GW2Keyboard-LocalMT.zip"
        licenses = ROOT / "third_party" / "licenses"
        local_notice = ROOT / "THIRD_PARTY_NOTICES_LOCAL_MT.txt"
        required = {
            "LICENSE-CTranslate2-MIT.txt": licenses / "CTranslate2-MIT.txt",
            "LICENSE-oneDNN-Apache-2.0.txt": licenses / "oneDNN-Apache-2.0.txt",
            "LICENSE-Apache-2.0.txt": licenses / "Apache-2.0.txt",
        }
        missing = [str(p) for p in [local_notice, *required.values()] if not p.is_file()]
        if missing:
            print("[FAIL] Local MT license files missing: " + ", ".join(missing))
            sys.exit(1)
        with zipfile.ZipFile(out_local_mt, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(local_mt_exe, "GW2Keyboard/gw2kb_translate.exe")
            z.write(find_openmp(), "GW2Keyboard/vcomp140.dll")
            z.write(local_notice, "GW2Keyboard/THIRD_PARTY_NOTICES_LOCAL_MT.txt")
            for archive_name, path in required.items():
                z.write(path, f"GW2Keyboard/{archive_name}")
        print(f"[*] Packaged {out_local_mt} ({out_local_mt.stat().st_size:,} bytes)")
    else:
        print("[-] Local MT helper absent; no GW2Keyboard-LocalMT.zip created.")

    # The helper for NVIDIA cards: only NVIDIA's files its licence lists as redistributable (EULA 2.6, Attachment A), unmodified.
    out_gpu = None
    if GPU_EXE.exists():
        out_gpu = dist_dir / "GW2Keyboard-GPU.zip"
        gpu_notice = ROOT / "THIRD_PARTY_NOTICES_GPU.txt"
        missing = [str(p) for p in [gpu_notice, CUDA_EULA, *(CUDA_BIN / d for d in CUDA_DLLS)] if not p.is_file()]
        if missing:
            print("[FAIL] Graphics card package files missing: " + ", ".join(missing))
            sys.exit(1)
        with zipfile.ZipFile(out_gpu, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(GPU_EXE, "GW2Keyboard/gw2kb_translate_gpu.exe")
            for d in CUDA_DLLS:
                z.write(CUDA_BIN / d, f"GW2Keyboard/{d}")
            z.write(find_openmp(), "GW2Keyboard/vcomp140.dll")
            z.write(gpu_notice, "GW2Keyboard/THIRD_PARTY_NOTICES_GPU.txt")
            z.write(CUDA_EULA, "GW2Keyboard/NVIDIA-CUDA-EULA.txt")
        print(f"[*] Packaged {out_gpu} ({out_gpu.stat().st_size:,} bytes)")
    else:
        print("[-] Graphics card helper absent; no GW2Keyboard-GPU.zip created.")

    now_utc = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    # 1. Manifest for Version Tag vX.Y.Z
    manifest_data = {
        "schema": "1.0",
        "version": version,
        "commit": commit,
        "dirty": not is_clean,
        "built": now_utc,
        "target": f"v{version}",
        "artifacts": [
            {
                "name": "GW2Chatlator.dll",
                "role": "addon_dll",
                "bytes": out_chatlator.stat().st_size,
                "sha256": sha256_file(out_chatlator),
                "target": f"v{version}"
            }
        ]
    }
    manifest_path = dist_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest_data, indent=2), encoding="utf-8")
    print(f"[*] Wrote version manifest to {manifest_path}")

    # 2. Manifest for Extras Container
    extras_artifacts = [
        {
            "name": "GW2Keyboard-Nexus.zip",
            "role": "voice_package",
            "bytes": out_zip.stat().st_size,
            "sha256": sha256_file(out_zip),
            "proto": 1,
            "caps": ["whisper"],
            "target": "extras",
            "files": inspect_zip(out_zip)
        }
    ]
    if out_local_mt and out_local_mt.exists():
        # proto, caps and build come from the helper's own "ready" line, not from this script
        ready = helper_ready(local_mt_exe)
        if ready.get("proto") is None:
            print(f"[FAIL] {local_mt_exe.name} gave no ready line with proto: {ready}")
            sys.exit(1)
        if is_clean and ready.get("build") != commit[:12]:
            print(f"[FAIL] {local_mt_exe.name} was built from {ready.get('build')}, not from HEAD {commit[:12]}")
            sys.exit(1)
        extras_artifacts.append({
            "name": "GW2Keyboard-LocalMT.zip",
            "role": "helper_package",
            "bytes": out_local_mt.stat().st_size,
            "sha256": sha256_file(out_local_mt),
            "proto": ready["proto"],
            "caps": ready.get("caps", []),
            "build": ready.get("build", ""),
            "target": "extras",
            "files": inspect_zip(out_local_mt)
        })

    if out_gpu and out_gpu.exists():
        env = dict(os.environ, PATH=str(CUDA_BIN) + os.pathsep + os.environ.get("PATH", ""))
        os.environ["PATH"] = env["PATH"]  # helper_ready starts it: its libraries lie in the toolkit here
        ready = helper_ready(GPU_EXE)
        if ready.get("proto") is None:
            print(f"[FAIL] the graphics card helper gave no ready line with proto: {ready}")
            sys.exit(1)
        if is_clean and ready.get("build") != commit[:12]:
            print(f"[FAIL] the graphics card helper was built from {ready.get('build')}, not from HEAD {commit[:12]}")
            sys.exit(1)
        extras_artifacts.append({
            "name": "GW2Keyboard-GPU.zip",
            "role": "gpu_package",
            "bytes": out_gpu.stat().st_size,
            "sha256": sha256_file(out_gpu),
            "proto": ready["proto"],
            "caps": ready.get("caps", []),
            "build": ready.get("build", ""),
            "target": "extras",
            "files": inspect_zip(out_gpu)
        })

    # Include existing model packages if found in dist_dir
    for model_zip in sorted(dist_dir.glob("GW2Keyboard-*.zip")):
        if model_zip.name not in ("GW2Keyboard-Nexus.zip", "GW2Keyboard-LocalMT.zip", "GW2Keyboard-GPU.zip"):
            extras_artifacts.append({
                "name": model_zip.name,
                "role": "model_package",
                "bytes": model_zip.stat().st_size,
                "sha256": sha256_file(model_zip),
                "target": "extras",
                "files": inspect_zip(model_zip)
            })

    extras_manifest_data = {
        "schema": "1.0",
        "version": version,
        "commit": commit,
        "dirty": not is_clean,
        "built": now_utc,
        "target": "extras",
        "artifacts": extras_artifacts
    }
    extras_manifest_path = dist_dir / "extras-manifest.json"
    extras_manifest_path.write_text(json.dumps(extras_manifest_data, indent=2), encoding="utf-8")
    print(f"[*] Wrote extras manifest to {extras_manifest_path}")

    print("[PASS] Packaging completed successfully.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
