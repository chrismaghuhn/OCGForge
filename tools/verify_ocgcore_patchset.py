#!/usr/bin/env python3
"""Read-only verification for the repository-versioned derived ocgcore."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.ocgcore_patchset import PatchsetError, validate_patchset_files
from tools.rules_bundle import BundleVerificationError, load_lock, verify_checkout


class VerificationError(RuntimeError):
    """Raised when the immutable base or derived patchset is not exact."""


def _run(command: list[str], cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", "") or getattr(exc, "stdout", "") or str(exc)
        raise VerificationError(f"command failed: {' '.join(command)}: {detail.strip()}") from exc
    return result.stdout.strip()


def _verify_derived(derived: Path, lock: dict[str, Any], patchset: dict[str, Any]) -> None:
    if not derived.is_dir():
        raise VerificationError(f"derived checkout is missing: {derived}")
    source = lock["sources"]["core"]
    if _run(["git", "-C", str(derived), "rev-parse", "HEAD"]) != source["commit"]:
        raise VerificationError("derived checkout does not start from the pinned base commit")
    marker_path = derived / ".ocgforge-patchset.json"
    try:
        marker = json.loads(marker_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise VerificationError(f"cannot read derived patchset marker: {marker_path}") from exc
    expected_marker = {
        "schema_version": 1,
        "base_commit": source["commit"],
        "base_checkout_sha256": source["resolved_checkout_hash"]["value"],
        "patchset": patchset,
    }
    for key, value in expected_marker.items():
        if marker.get(key) != value:
            raise VerificationError(f"derived patchset marker mismatch: {key}")
    modified_files = marker.get("modified_files")
    if not isinstance(modified_files, list) or not modified_files:
        raise VerificationError("derived patchset marker has no modified file list")
    actual_modified = _run(["git", "-C", str(derived), "diff", "--name-only"]).splitlines()
    if actual_modified != modified_files:
        raise VerificationError("derived patchset modified file list is not reproducible")
    untracked = _run(["git", "-C", str(derived), "ls-files", "--others", "--exclude-standard"]).splitlines()
    if untracked != [".ocgforge-patchset.json"]:
        raise VerificationError("derived checkout contains unexpected untracked files")


def verify(*, lock_path: Path, cache_root: Path, patchset_root: Path, derived: Path | None) -> dict[str, Any]:
    try:
        lock = load_lock(lock_path)
        verify_checkout(lock, cache_root)
        patchset = lock["rule_affecting_inputs"]["core"]["patchset"]
        validate_patchset_files(patchset, patchset_root)
    except (BundleVerificationError, PatchsetError, KeyError) as exc:
        raise VerificationError(str(exc)) from exc

    base = cache_root / lock["sources"]["core"]["cache_directory"]
    if _run(["git", "-C", str(base), "status", "--porcelain", "--ignore-submodules=none"]):
        raise VerificationError("immutable base checkout is dirty")
    if derived is not None:
        _verify_derived(derived.resolve(), lock, patchset)
    return {
        "ok": True,
        "base_commit": lock["sources"]["core"]["commit"],
        "patchset_id": patchset["id"],
        "patchset_sha256": patchset["sha256"],
        "derived_checked": derived is not None,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--patchset-root", type=Path, required=True)
    parser.add_argument("--derived", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(verify(
            lock_path=args.lock,
            cache_root=args.cache,
            patchset_root=args.patchset_root,
            derived=args.derived,
        ), sort_keys=True))
    except (VerificationError, OSError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, sort_keys=True))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
