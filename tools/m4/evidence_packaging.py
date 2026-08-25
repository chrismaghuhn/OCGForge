"""Canonical byte handling for repository-backed M4 evidence."""

from __future__ import annotations

import hashlib
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DERIVED_TEXT_ROOTS = (
    REPOSITORY_ROOT / "artifacts" / "m4",
    REPOSITORY_ROOT / "artifacts" / "m4-finalization" / "verification",
    REPOSITORY_ROOT / "docs" / "m4",
)
DERIVED_TEXT_SUFFIXES = frozenset({".json", ".jsonl", ".log", ".md"})


class EvidencePackagingError(ValueError):
    """Raised when a known text evidence artifact has malformed newlines."""


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def is_known_derived_text_evidence(path: str | Path) -> bool:
    """Return whether *path* is a generated M4 text-evidence artifact."""

    candidate = Path(path)
    return candidate.suffix.lower() in DERIVED_TEXT_SUFFIXES and any(
        _is_within(candidate, root) for root in DERIVED_TEXT_ROOTS
    )


def canonical_text_bytes(data: bytes) -> bytes:
    """Canonicalize CRLF to LF and reject bare CR in text evidence."""

    normalized = data.replace(b"\r\n", b"\n")
    if b"\r" in normalized:
        raise EvidencePackagingError("derived text evidence contains a bare carriage return")
    return normalized


def evidence_bytes(path: str | Path) -> bytes:
    """Read evidence bytes using the repository's text/binary boundary."""

    resolved = Path(path)
    data = resolved.read_bytes()
    if is_known_derived_text_evidence(resolved):
        return canonical_text_bytes(data)
    return data


def evidence_sha256(path: str | Path) -> str:
    """Hash the canonical repository representation of an evidence artifact."""

    return hashlib.sha256(evidence_bytes(path)).hexdigest()


def write_text_lf(path: str | Path, text: str) -> None:
    """Write generated text evidence with explicit LF newlines."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(text)
