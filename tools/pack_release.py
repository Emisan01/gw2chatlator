#!/usr/bin/env python3
"""
tools/pack_release.py — Packages GW2Chatlator release assets.

Outputs to build/dist:
  - gw2keyboard_nexus.dll
  - GW2Keyboard-Nexus.zip
"""

import os
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_REL = ROOT / "build" / "addons" / "nexus-keyboard" / "Release"
DIST_DIR = ROOT / "build" / "dist"

def main():
    dll = BUILD_REL / "gw2keyboard_nexus.dll"
    voice_exe = BUILD_REL / "GW2Keyboard" / "gw2kb_voice.exe"
    voice_dir = BUILD_REL / "GW2Keyboard" / "voice"

    if not dll.exists():
        print(f"[FAIL] Release DLL not found: {dll}")
        sys.exit(1)

    DIST_DIR.mkdir(parents=True, exist_ok=True)
    out_dll = DIST_DIR / "gw2keyboard_nexus.dll"
    shutil.copy2(dll, out_dll)
    print(f"[*] Copied standalone DLL to {out_dll} ({out_dll.stat().st_size:,} bytes)")

    out_zip = DIST_DIR / "GW2Keyboard-Nexus.zip"
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as z:
        # 1. Root DLL
        z.write(dll, "gw2keyboard_nexus.dll")

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
            "Put gw2keyboard_nexus.dll in your Guild Wars 2\\addons folder.\n"
            "The GW2Keyboard folder contains the optional voice helper and whisper libraries.\n"
        )
        z.writestr("GW2Keyboard/README.txt", readme_text)

    print(f"[*] Packaged {out_zip} ({out_zip.stat().st_size:,} bytes)")
    print("[PASS] Packaging completed successfully.")

if __name__ == "__main__":
    main()
