"""Canonical rules-bundle hashing and verification primitives."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from pathlib import Path
from typing import Any

from tools.ocgcore_patchset import PatchsetError, validate_patchset_metadata


SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-f]{40}$")


class BundleVerificationError(RuntimeError):
    """Raised when a rules bundle is missing, floating, or mismatched."""


def canonical_json(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def bundle_id_for(rule_affecting_inputs: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(rule_affecting_inputs)).hexdigest()


def _relative_file_paths(root: Path) -> list[Path]:
    paths: list[Path] = []
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if ".git" in relative.parts:
            continue
        if path.is_symlink():
            raise BundleVerificationError(f"symlink is not allowed in bundle source: {path}")
        if path.is_file():
            paths.append(relative)
    return sorted(paths, key=lambda item: item.as_posix())


def checkout_hash(root: Path) -> str:
    """Hash path names and file bytes in stable POSIX path order."""

    root = root.resolve()
    if not root.is_dir():
        raise BundleVerificationError(f"bundle checkout is missing: {root}")

    digest = hashlib.sha256()
    for relative in _relative_file_paths(root):
        data = (root / relative).read_bytes()
        digest.update(relative.as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(len(data)).encode("ascii"))
        digest.update(b"\0")
        digest.update(data)
        digest.update(b"\0")
    return digest.hexdigest()


def file_hash(path: Path) -> str:
    if not path.is_file() or path.is_symlink():
        raise BundleVerificationError(f"database artifact is missing or invalid: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _git_head(path: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise BundleVerificationError(f"cannot read Git HEAD for {path}: {exc}") from exc
    return result.stdout.strip()


def validate_lock_shape(lock: dict[str, Any]) -> None:
    schema_version = lock.get("schema_version")
    if schema_version not in {1, 2}:
        raise BundleVerificationError("rules bundle schema_version must be 1 or 2")
    bundle_id = lock.get("bundle_id")
    if not isinstance(bundle_id, str) or not SHA256_RE.fullmatch(bundle_id):
        raise BundleVerificationError("bundle_id must be a lowercase SHA-256 hex digest")

    inputs = lock.get("rule_affecting_inputs")
    if not isinstance(inputs, dict) or not inputs:
        raise BundleVerificationError("rule_affecting_inputs must be a non-empty object")

    sources = lock.get("sources")
    if not isinstance(sources, dict) or not sources:
        raise BundleVerificationError("sources must be a non-empty object")
    for name, source in sources.items():
        if not isinstance(source, dict):
            raise BundleVerificationError(f"source {name} must be an object")
        repository = source.get("repository")
        commit = source.get("commit")
        if not isinstance(repository, str) or not repository.startswith("https://"):
            raise BundleVerificationError(f"source {name} must use an HTTPS repository")
        if not isinstance(commit, str) or not COMMIT_RE.fullmatch(commit):
            raise BundleVerificationError(f"source {name} must pin a full commit SHA")
        resolved = source.get("resolved_checkout_hash")
        if (
            not isinstance(resolved, dict)
            or resolved.get("algorithm") != "sha256"
            or not isinstance(resolved.get("value"), str)
            or not SHA256_RE.fullmatch(resolved["value"])
        ):
            raise BundleVerificationError(
                f"source {name} must record a SHA-256 resolved_checkout_hash"
            )
        if not isinstance(source.get("license"), str) or not source["license"]:
            raise BundleVerificationError(f"source {name} must record a license marker")
        if not isinstance(source.get("retrieval_method"), str) or not source["retrieval_method"]:
            raise BundleVerificationError(f"source {name} must record retrieval_method")

    core = sources.get("core")
    if not isinstance(core, dict) or core.get("expected_api_version") != "11.0":
        raise BundleVerificationError("core expected_api_version must be 11.0")
    if schema_version >= 2:
        input_core = inputs.get("core")
        patchset = input_core.get("patchset") if isinstance(input_core, dict) else None
        if not isinstance(patchset, dict):
            raise BundleVerificationError("schema v2 requires rule_affecting_inputs.core.patchset")
        try:
            validate_patchset_metadata(patchset)
        except PatchsetError as exc:
            raise BundleVerificationError(f"invalid ocgcore patchset: {exc}") from exc

    artifact = lock.get("database_artifact")
    if not isinstance(artifact, dict) or not SHA256_RE.fullmatch(str(artifact.get("sha256", ""))):
        raise BundleVerificationError("database_artifact.sha256 must be a SHA-256 digest")

    duel_flags = lock.get("duel_flags")
    if not isinstance(duel_flags, dict) or not isinstance(duel_flags.get("value"), int):
        raise BundleVerificationError("duel_flags.value must be an integer")

    format_id = lock.get("format_id")
    duel_mode = lock.get("duel_mode")
    input_format_id = inputs.get("format_id")
    input_duel_mode = inputs.get("duel_mode")
    input_duel_flags = inputs.get("duel_flags")
    if not isinstance(format_id, str) or not format_id:
        raise BundleVerificationError("format_id must be a non-empty string")
    if not isinstance(duel_mode, str) or not duel_mode:
        raise BundleVerificationError("duel_mode must be a non-empty string")
    if input_format_id != format_id or input_duel_mode != duel_mode:
        raise BundleVerificationError("top-level format/mode does not match rule_affecting_inputs")
    if input_duel_flags != duel_flags:
        raise BundleVerificationError("top-level duel_flags does not match rule_affecting_inputs")


def load_lock(path: Path) -> dict[str, Any]:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BundleVerificationError(f"cannot read lock file {path}: {exc}") from exc
    if not isinstance(lock, dict):
        raise BundleVerificationError("lock file root must be an object")
    validate_lock_shape(lock)
    if lock["bundle_id"] != bundle_id_for(lock["rule_affecting_inputs"]):
        raise BundleVerificationError("bundle_id does not match canonical rule_affecting_inputs")
    return lock


def verify_checkout(lock: dict[str, Any], cache_root: Path) -> list[str]:
    """Verify all cached checkouts and the selected database artifact."""

    failures: list[str] = []
    for name, source in lock["sources"].items():
        checkout = cache_root / source["cache_directory"]
        if not checkout.is_dir():
            failures.append(f"{name}: missing checkout {checkout}")
            continue
        try:
            head = _git_head(checkout)
            if head != source["commit"]:
                failures.append(f"{name}: HEAD {head} != pinned {source['commit']}")
            actual_hash = checkout_hash(checkout)
            if actual_hash != source["resolved_checkout_hash"]["value"]:
                failures.append(
                    f"{name}: checkout hash {actual_hash} != pinned "
                    f"{source['resolved_checkout_hash']['value']}"
                )
        except BundleVerificationError as exc:
            failures.append(f"{name}: {exc}")

    database_source = lock["database_artifact"]["source"]
    database_path = cache_root / lock["sources"][database_source]["cache_directory"] / lock["database_artifact"]["path"]
    try:
        actual_database_hash = file_hash(database_path)
        if actual_database_hash != lock["database_artifact"]["sha256"]:
            failures.append(
                f"database artifact hash {actual_database_hash} != pinned "
                f"{lock['database_artifact']['sha256']}"
            )
    except BundleVerificationError as exc:
        failures.append(str(exc))

    if failures:
        raise BundleVerificationError("; ".join(failures))
    return [
        f"{name}: {source['commit']} ({source['resolved_checkout_hash']['value']})"
        for name, source in lock["sources"].items()
    ]
