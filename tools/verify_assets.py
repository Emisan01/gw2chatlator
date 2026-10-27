#!/usr/bin/env python3
"""
tools/verify_assets.py — Verifies uploaded GitHub Release assets against release manifests.

Checks:
  1. Version release tag (vX.Y.Z): GW2Chatlator.dll, manifest.json
  2. Extras release tag (extras): GW2Keyboard-LocalMT.zip, GW2Keyboard-Nexus.zip, model packages, extras-manifest.json

Outputs a comparison table and exits with 0 (all pass) or 1 (mismatch).
"""

import argparse
import hashlib
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def fetch_json(url: str):
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "GW2Chatlator-Asset-Verifier",
            "Accept": "application/vnd.github.v3+json"
        }
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))

def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def verify_local(manifest_path: Path) -> bool:
    if not manifest_path.exists():
        print(f"[FAIL] Manifest not found: {manifest_path}")
        return False
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    dist_dir = manifest_path.parent
    all_ok = True
    print(f"\n[*] Checking local artifacts in {dist_dir} against {manifest_path.name}:")
    print(f"{'Status':<8} {'Asset Name':<32} {'Bytes':<12} {'SHA-256':<64}")
    print("-" * 120)

    for art in data.get("artifacts", []):
        name = art["name"]
        exp_bytes = art["bytes"]
        exp_sha = art["sha256"]
        file_path = dist_dir / name
        if not file_path.exists():
            print(f"{'MISSING':<8} {name:<32} {exp_bytes:<12} {'File not on disk'}")
            all_ok = False
            continue

        actual_bytes = file_path.stat().st_size
        actual_sha = sha256_file(file_path)

        if actual_bytes == exp_bytes and actual_sha.lower() == exp_sha.lower():
            print(f"{'OK':<8} {name:<32} {actual_bytes:<12} {actual_sha[:16]}...{actual_sha[-16:]}")
        else:
            print(f"{'MISMATCH':<8} {name:<32} expected: {exp_bytes} / {exp_sha[:8]}..., actual: {actual_bytes} / {actual_sha[:8]}...")
            all_ok = False

    return all_ok

def verify_remote_tag(repo: str, tag: str, manifest_data: dict, manifest_path: Path, manifest_asset: str) -> bool:
    """Every manifest artifact plus the manifest itself: bytes and the SHA-256 GitHub reports as `digest`
    (no download, also for the 582 MB model)."""
    url = f"https://api.github.com/repos/{repo}/releases/tags/{tag}"
    print(f"\n[*] Querying GitHub API for release tag '{tag}' ({url}) ...")
    try:
        rel = fetch_json(url)
    except urllib.error.HTTPError as e:
        print(f"[FAIL] Could not query release {tag}: {e}")
        return False
    except Exception as e:
        print(f"[FAIL] Network or API error querying release {tag}: {e}")
        return False

    assets_by_name = {a["name"]: a for a in rel.get("assets", [])}
    expected = [(a["name"], a["bytes"], a["sha256"]) for a in manifest_data.get("artifacts", [])]
    expected.append((manifest_asset, manifest_path.stat().st_size, sha256_file(manifest_path)))
    all_ok = True
    print(f"{'Status':<8} {'Asset Name':<36} {'Bytes (expected / online)':<28} SHA-256")
    print("-" * 120)

    for name, exp_bytes, exp_sha in expected:
        asset = assets_by_name.get(name)
        if not asset:
            print(f"{'MISSING':<8} {name:<36} {exp_bytes:<28} not found in release")
            all_ok = False
            continue
        actual_bytes = asset.get("size")
        digest = (asset.get("digest") or "").lower()
        actual_sha = digest[len("sha256:"):] if digest.startswith("sha256:") else ""
        if actual_bytes == exp_bytes and actual_sha == exp_sha.lower():
            print(f"{'OK':<8} {name:<36} {f'{exp_bytes} / {actual_bytes}':<28} {exp_sha[:16]}...")
        else:
            status = "NODIGEST" if not actual_sha else "MISMATCH"
            print(f"{status:<8} {name:<36} {f'{exp_bytes} / {actual_bytes}':<28} "
                  f"expected {exp_sha[:16]}..., online {actual_sha[:16] or '-'}...")
            all_ok = False

    return all_ok

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default="Emisan01/gw2chatlator", help="GitHub repo (default: Emisan01/gw2chatlator)")
    parser.add_argument("--manifest", type=Path, default=ROOT / "build" / "dist" / "manifest.json",
                        help="Path to version manifest.json")
    parser.add_argument("--extras-manifest", type=Path, default=ROOT / "build" / "dist" / "extras-manifest.json",
                        help="Path to extras-manifest.json")
    parser.add_argument("--local", action="store_true", help="Only verify local files against manifests")
    args = parser.parse_args()

    if args.local:
        ok_v = verify_local(args.manifest)
        ok_e = verify_local(args.extras_manifest) if args.extras_manifest.exists() else True
        if ok_v and ok_e:
            print("\n[PASS] All local assets match manifests.")
            sys.exit(0)
        else:
            print("\n[FAIL] Local assets do not match manifests.")
            sys.exit(1)

    if not args.manifest.exists() or not args.extras_manifest.exists():
        print(f"[FAIL] Manifest files missing in build/dist! Run tools/pack_release.py first.")
        sys.exit(1)

    v_data = json.loads(args.manifest.read_text(encoding="utf-8"))
    e_data = json.loads(args.extras_manifest.read_text(encoding="utf-8"))
    version = v_data.get("version", "2.6.3")
    v_tag = f"v{version}"

    ok_v = verify_remote_tag(args.repo, v_tag, v_data, args.manifest, "manifest.json")
    ok_e = verify_remote_tag(args.repo, "extras", e_data, args.extras_manifest, "extras-manifest.json")

    if ok_v and ok_e:
        print("\n[PASS] All online release assets successfully verified against manifests.")
        sys.exit(0)
    else:
        print("\n[FAIL] Release asset verification failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
