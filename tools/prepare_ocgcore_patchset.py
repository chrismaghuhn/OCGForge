#!/usr/bin/env python3
"""Prepare the immutable-base, repository-patched ocgcore development checkout."""

from __future__ import annotations

import argparse
import json
import shutil
import stat
import subprocess
import sys
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.ocgcore_patchset import PatchsetError, validate_patchset_files
from tools.rules_bundle import BundleVerificationError, load_lock, verify_checkout


class PreparationError(RuntimeError):
    """Raised when the derived core cannot be prepared safely."""


def _run(command: list[str], cwd: Path | None = None) -> str:
    try:
        result = subprocess.run(command, cwd=cwd, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "stderr", "") or getattr(exc, "stdout", "") or str(exc)
        raise PreparationError(f"command failed: {' '.join(command)}: {detail.strip()}") from exc
    return result.stdout.strip()


def _git_status(path: Path) -> str:
    return _run(["git", "-C", str(path), "status", "--porcelain", "--ignore-submodules=none"])


def _git_head(path: Path) -> str:
    return _run(["git", "-C", str(path), "rev-parse", "HEAD"])


def _marker_for(lock: dict[str, Any], patchset: dict[str, Any]) -> dict[str, Any]:
    source = lock["sources"]["core"]
    return {
        "schema_version": 1,
        "base_commit": source["commit"],
        "base_checkout_sha256": source["resolved_checkout_hash"]["value"],
        "patchset": patchset,
    }


def _target_is_safe(target: Path, base: Path) -> None:
    target = target.resolve()
    base = base.resolve()
    if target == base:
        raise PreparationError("refusing to patch the immutable base checkout")
    if target.name != "ocgcore" or target.parent.name != "derived":
        raise PreparationError("derived core output must be .cache/derived/ocgcore")


def _read_marker(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise PreparationError(f"cannot read derived-core marker {path}: {exc}") from exc
    return value if isinstance(value, dict) else None


def _is_reusable(target: Path, marker: dict[str, Any], patchset_root: Path) -> bool:
    recorded = _read_marker(target / ".ocgforge-patchset.json")
    if recorded is None:
        return False
    recorded_identity = {key: value for key, value in recorded.items() if key != "modified_files"}
    if recorded_identity != marker or not isinstance(recorded.get("modified_files"), list):
        return False
    tracked = _run(["git", "-C", str(target), "diff", "--name-only"]).splitlines()
    untracked = _run(["git", "-C", str(target), "ls-files", "--others", "--exclude-standard"]).splitlines()
    if tracked != recorded["modified_files"] or untracked != [".ocgforge-patchset.json"]:
        return False
    for entry in marker["patchset"]["ordered_patches"]:
        patch_path = patchset_root / entry["name"]
        try:
            _run(["git", "-C", str(target), "apply", "--reverse", "--check", str(patch_path)])
        except PreparationError:
            return False
    return True


def _make_writable(root: Path) -> None:
    for path in (root, *root.rglob("*")):
        try:
            path.chmod(path.stat().st_mode | stat.S_IWRITE)
        except OSError:
            pass


def _apply_patchset(target: Path, patchset: dict[str, Any], marker: dict[str, Any], patchset_root: Path) -> None:
    for entry in patchset["ordered_patches"]:
        patch_path = patchset_root / entry["name"]
        _run(["git", "apply", "--check", str(patch_path)], cwd=target)
        _run(["git", "apply", str(patch_path)], cwd=target)
    marker["modified_files"] = _run(["git", "diff", "--name-only"], cwd=target).splitlines()
    (target / ".ocgforge-patchset.json").write_text(
        json.dumps(marker, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _recreate_target(target: Path, base: Path, patchset: dict[str, Any], marker: dict[str, Any], patchset_root: Path) -> None:
    if target.exists():
        if _git_head(target) == marker["base_commit"] and not _git_status(target):
            _apply_patchset(target, patchset, marker, patchset_root)
            return
        _make_writable(target)
        shutil.rmtree(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    _run(["git", "clone", "--local", "--no-hardlinks", "--no-checkout", "--recurse-submodules", str(base), str(target)])
    _run(["git", "checkout", "--detach", marker["base_commit"]], cwd=target)
    _run(["git", "submodule", "update", "--init", "--recursive"], cwd=target)
    _apply_patchset(target, patchset, marker, patchset_root)


def prepare(*, lock_path: Path, cache_root: Path, patchset_root: Path, output: Path) -> dict[str, Any]:
    lock_path = lock_path.resolve()
    cache_root = cache_root.resolve()
    patchset_root = patchset_root.resolve()
    output = output.resolve()
    try:
        lock = load_lock(lock_path)
        verify_checkout(lock, cache_root)
        patchset = lock["rule_affecting_inputs"]["core"]["patchset"]
        validate_patchset_files(patchset, patchset_root)
    except (BundleVerificationError, PatchsetError, KeyError) as exc:
        raise PreparationError(str(exc)) from exc

    base = cache_root / lock["sources"]["core"]["cache_directory"]
    if _git_status(base):
        raise PreparationError(f"immutable base checkout is dirty: {base}")
    _target_is_safe(output, base)
    marker = _marker_for(lock, patchset)
    if not _is_reusable(output, marker, patchset_root):
        _recreate_target(output, base, patchset, marker, patchset_root)
    return {
        "ok": True,
        "derived_checkout": str(output.resolve()),
        "base_commit": marker["base_commit"],
        "patchset_id": patchset["id"],
        "patchset_sha256": patchset["sha256"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--patchset-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(
            lock_path=args.lock,
            cache_root=args.cache,
            patchset_root=args.patchset_root,
            output=args.output,
        ), sort_keys=True))
    except (PreparationError, OSError) as exc:
        print(json.dumps({"ok": False, "error": str(exc)}, sort_keys=True))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
