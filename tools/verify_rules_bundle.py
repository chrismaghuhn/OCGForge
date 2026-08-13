#!/usr/bin/env python3
"""Verify the exact M0 rules bundle and print machine-readable evidence."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.rules_bundle import BundleVerificationError, load_lock, verify_checkout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    try:
        lock = load_lock(args.lock)
        checkouts = verify_checkout(lock, args.cache)
    except BundleVerificationError as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, sort_keys=True))
        return 2

    print(
        json.dumps(
            {
                "ok": True,
                "bundle_id": lock["bundle_id"],
                "checkouts": checkouts,
                "core_api_version": lock["sources"]["core"]["expected_api_version"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
