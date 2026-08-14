"""Deterministic validation helpers for repository-versioned ocgcore patches."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any


class PatchsetError(ValueError):
    """Raised when a patchset manifest or patch file is invalid."""


def canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    try:
        return _sha256_bytes(path.read_bytes())
    except OSError as exc:
        raise PatchsetError(f"cannot read patch file {path}: {exc}") from exc


def _hash_input(manifest: dict[str, Any]) -> dict[str, Any]:
    patchset_id = manifest.get("id")
    ordered_patches = manifest.get("ordered_patches")
    if not isinstance(patchset_id, str) or not patchset_id:
        raise PatchsetError("patchset id must be a non-empty string")
    if not isinstance(ordered_patches, list) or not ordered_patches:
        raise PatchsetError("ordered_patches must be a non-empty list")
    return {"id": patchset_id, "ordered_patches": ordered_patches}


def patchset_hash_for(manifest: dict[str, Any]) -> str:
    """Hash the ordered patch names and bytes hashes, excluding the aggregate hash."""

    return _sha256_bytes(canonical_json(_hash_input(manifest)))


def _safe_patch_path(root: Path, name: str) -> Path:
    if not isinstance(name, str) or not name or Path(name).is_absolute():
        raise PatchsetError("patch name must be a relative path")
    root = root.resolve()
    path = (root / Path(name)).resolve()
    if path != root and root not in path.parents:
        raise PatchsetError(f"patch path escapes patch root: {name}")
    if path.is_symlink() or not path.is_file():
        raise PatchsetError(f"patch file is missing or not a regular file: {name}")
    return path


def build_patchset_manifest(root: Path, names: list[str], patchset_id: str) -> dict[str, Any]:
    entries = []
    for name in names:
        path = _safe_patch_path(root, name)
        entries.append({"name": name, "sha256": _sha256_file(path)})
    manifest: dict[str, Any] = {"id": patchset_id, "ordered_patches": entries}
    manifest["sha256"] = patchset_hash_for(manifest)
    return manifest


def validate_patchset_metadata(manifest: dict[str, Any]) -> None:
    """Validate shape, ordered entries, and the aggregate patchset hash."""

    hash_input = _hash_input(manifest)
    expected_aggregate = manifest.get("sha256")
    if not isinstance(expected_aggregate, str) or len(expected_aggregate) != 64:
        raise PatchsetError("patchset sha256 must be a 64-character digest")
    if expected_aggregate != patchset_hash_for(hash_input):
        raise PatchsetError("patchset sha256 does not match canonical ordered patch metadata")

    seen: set[str] = set()
    for entry in hash_input["ordered_patches"]:
        if not isinstance(entry, dict):
            raise PatchsetError("each ordered patch must be an object")
        name = entry.get("name")
        digest = entry.get("sha256")
        if not isinstance(name, str) or not name or name in seen:
            raise PatchsetError("patch names must be unique non-empty strings")
        if not isinstance(digest, str) or len(digest) != 64:
            raise PatchsetError(f"patch sha256 is invalid: {name}")
        seen.add(name)


def validate_patchset_files(manifest: dict[str, Any], root: Path) -> None:
    """Validate metadata and every ordered patch file's exact bytes hash."""

    validate_patchset_metadata(manifest)
    hash_input = _hash_input(manifest)
    root = root.resolve()
    if not root.is_dir():
        raise PatchsetError(f"patch root is missing: {root}")
    expected_names = {entry["name"] for entry in hash_input["ordered_patches"]}
    actual_files: set[str] = set()
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise PatchsetError(f"patch root contains a symlink: {relative}")
        if path.is_file():
            actual_files.add(relative)
    extra_names = sorted(actual_files - expected_names)
    missing_names = sorted(expected_names - actual_files)
    if extra_names:
        raise PatchsetError("unexpected patch files: " + ", ".join(extra_names))
    if missing_names:
        raise PatchsetError("missing patch files: " + ", ".join(missing_names))
    for entry in hash_input["ordered_patches"]:
        name = entry["name"]
        digest = entry["sha256"]
        actual = _sha256_file(_safe_patch_path(root, name))
        if actual != digest:
            raise PatchsetError(f"patch hash mismatch for {name}: {actual} != {digest}")
