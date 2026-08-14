"""Lock-backed canonical rules-mode metadata for M3 acceptance runners."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from tools.rules_bundle import load_lock


CANONICAL_FORMAT_ID = "TCG_ADVANCED_2026_05_18"
CANONICAL_DUEL_MODE = "DUEL_MODE_MR5"
CANONICAL_DUEL_FLAGS = 0x2E800


def load_canonical_environment(lock_path: Path) -> dict[str, Any]:
    lock = load_lock(lock_path)
    inputs = lock["rule_affecting_inputs"]
    environment = {
        "format_id": lock["format_id"],
        "duel_mode_name": lock["duel_mode"],
        "duel_flags": lock["duel_flags"]["value"],
        "rules_bundle_id": lock["bundle_id"],
        "ocgcore_commit": lock["sources"]["core"]["commit"],
        "core_patchset_id": inputs["core"]["patchset"]["id"],
        "core_patchset_sha256": inputs["core"]["patchset"]["sha256"],
        "ocg_api_version": lock["sources"]["core"]["expected_api_version"],
        "cardscripts_commit": lock["sources"]["cardscripts"]["commit"],
        "babelcdb_commit": lock["sources"]["babelcdb"]["commit"],
        "rule_affecting_inputs": inputs,
    }
    if (
        environment["format_id"] != CANONICAL_FORMAT_ID
        or environment["duel_mode_name"] != CANONICAL_DUEL_MODE
        or environment["duel_flags"] != CANONICAL_DUEL_FLAGS
    ):
        raise ValueError("locked format does not resolve to canonical pinned MR5")
    return environment


def assert_canonical_environment(metadata: dict[str, Any], label: str = "artifact") -> None:
    expected = {
        "format_id": CANONICAL_FORMAT_ID,
        "duel_mode_name": CANONICAL_DUEL_MODE,
        "duel_flags": CANONICAL_DUEL_FLAGS,
    }
    for key, value in expected.items():
        if metadata.get(key) != value:
            raise AssertionError(f"{label} does not use canonical {key}: {metadata.get(key)!r}")
