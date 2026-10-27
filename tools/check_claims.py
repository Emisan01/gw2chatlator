#!/usr/bin/env python3
"""
tools/check_claims.py — Cross-file architectural claims consistency verifier.
Verifies invariants and claims defined in tools/claims.json across repository files.
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def main():
    claims_file = ROOT / "tools" / "claims.json"
    if not claims_file.exists():
        print(f"[FAIL] Claims file missing: {claims_file}")
        sys.exit(1)

    try:
        data = json.loads(claims_file.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"[FAIL] Invalid JSON in {claims_file}: {e}")
        sys.exit(1)

    all_passed = True
    claims = data.get("claims", [])
    print(f"[*] Checking {len(claims)} architectural claims ...")

    for claim in claims:
        cid = claim.get("id")
        desc = claim.get("description", "")
        occurrences = claim.get("occurrences", [])
        claim_ok = True

        for occ in occurrences:
            file_rel = occ.get("file")
            pattern = occ.get("pattern")
            target_path = ROOT / file_rel
            if not target_path.exists():
                print(f"  [FAIL] [{cid}] Target file not found: {file_rel}")
                claim_ok = False
                continue

            content = target_path.read_text(encoding="utf-8")
            if not re.search(pattern, content):
                print(f"  [FAIL] [{cid}] Pattern '{pattern}' not matched in {file_rel}")
                claim_ok = False

        if claim_ok:
            print(f"  [PASS] [{cid}] {desc}")
        else:
            all_passed = False

    if not all_passed:
        print("[FAIL] Claims verification failed!")
        sys.exit(1)

    print("[PASS] All architectural claims verified across files.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
