#!/usr/bin/env python3
"""
tools/pack_release.py — Packages GW2Chatlator release assets.

Outputs to build/dist:
  - GW2Chatlator.dll
  - GW2Keyboard-Nexus.zip
  - GW2Keyboard-LocalMT.zip (when GCT_LOCAL_MT was built)
"""

import os
import argparse
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build",
                        help="CMake build directory (default: build)")
    args = parser.parse_args()
    build_rel = args.build_dir / "addons" / "nexus-keyboard" / "Release"
    dist_dir = args.build_dir / "dist"
    dll = build_rel / "GW2Chatlator.dll"
    if not dll.exists():
        dll = build_rel / "gw2keyboard_nexus.dll"
    voice_exe = build_rel / "GW2Keyboard" / "gw2kb_voice.exe"
    voice_dir = build_rel / "GW2Keyboard" / "voice"
    local_mt_exe = build_rel / "GW2Keyboard" / "gw2kb_translate.exe"

    if not dll.exists():
        print(f"[FAIL] Release DLL not found: neither GW2Chatlator.dll nor gw2keyboard_nexus.dll in {build_rel}")
        sys.exit(1)

    dist_dir.mkdir(parents=True, exist_ok=True)
    out_chatlator = dist_dir / "GW2Chatlator.dll"
    shutil.copy2(dll, out_chatlator)
    # Remove any old legacy-named DLL from dist to avoid confusing users
    legacy_dll = dist_dir / "gw2keyboard_nexus.dll"
    if legacy_dll.exists():
        legacy_dll.unlink()
    print(f"[*] Copied standalone DLL to {out_chatlator} ({out_chatlator.stat().st_size:,} bytes)")

    out_zip = dist_dir / "GW2Keyboard-Nexus.zip"
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        # 1. Root DLL (unified naming for Nexus manager)
        z.write(dll, "GW2Chatlator.dll")

        # 2. Voice executable if built
        if voice_exe.exists():
            z.write(voice_exe, "GW2Keyboard/gw2kb_voice.exe")

        # 3. Voice backend DLLs
        if voice_dir.exists():
            for f in voice_dir.glob("*.dll"):
                z.write(f, f"GW2Keyboard/voice/{f.name}")

        # 4. Third-party notices and README
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

    # CTranslate2 is a separate, optional executable: the add-on downloads it only when selected by the user.
    if local_mt_exe.exists():
        out_local_mt = dist_dir / "GW2Keyboard-LocalMT.zip"
        licenses = ROOT / "third_party" / "licenses"
        local_notice = ROOT / "THIRD_PARTY_NOTICES_LOCAL_MT.txt"
        required = {
            "LICENSE-CTranslate2-MIT.txt": licenses / "CTranslate2-MIT.txt",
            "LICENSE-oneDNN-Apache-2.0.txt": licenses / "oneDNN-Apache-2.0.txt",
            "LICENSE-Apache-2.0.txt": licenses / "Apache-2.0.txt",
        }
        missing = [str(path) for path in [local_notice, *required.values()] if not path.is_file()]
        if missing:
            print("[FAIL] Local MT license files missing: " + ", ".join(missing))
            sys.exit(1)
        with zipfile.ZipFile(out_local_mt, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(local_mt_exe, "GW2Keyboard/gw2kb_translate.exe")
            z.write(local_notice, "GW2Keyboard/THIRD_PARTY_NOTICES_LOCAL_MT.txt")
            for archive_name, path in required.items():
                z.write(path, f"GW2Keyboard/{archive_name}")
        print(f"[*] Packaged {out_local_mt} ({out_local_mt.stat().st_size:,} bytes)")
    else:
        print("[-] Local MT helper absent; no GW2Keyboard-LocalMT.zip created.")
    print("[PASS] Packaging completed successfully.")

if __name__ == "__main__":
    main()
