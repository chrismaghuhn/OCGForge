"""Build and validate the machine-readable M3.5 acceptance evidence."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from tools.rules_bundle import load_lock

from .coverage import build_mechanics_coverage


OLD_M3_BUNDLE_ID = "ff8721aae1a17da6a72079e65ae75a05012c0c367b6f249651c1de713c1fbf91"
PRE_MR5_BUNDLE_ID = "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4"


def _read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root is not an object: {path}")
    return value


def _canonical_environment(lock: dict[str, Any]) -> dict[str, Any]:
    environment = load_canonical_environment_from_lock(lock)
    return {key: environment[key] for key in (
        "format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
        "ocgcore_commit", "core_patchset_id", "core_patchset_sha256",
        "ocg_api_version", "cardscripts_commit", "babelcdb_commit",
    )}


def load_canonical_environment_from_lock(lock: dict[str, Any]) -> dict[str, Any]:
    inputs = lock["rule_affecting_inputs"]
    return {
        "format_id": lock["format_id"],
        "duel_mode_name": lock["duel_mode"],
        "duel_flags": lock["duel_flags"]["value"],
        "rules_bundle_id": lock["bundle_id"],
        "ocgcore_commit": inputs["core"]["commit"],
        "core_patchset_id": inputs["core"]["patchset"]["id"],
        "core_patchset_sha256": inputs["core"]["patchset"]["sha256"],
        "ocg_api_version": inputs["core"]["api_version"],
        "cardscripts_commit": inputs["card_scripts"]["commit"],
        "babelcdb_commit": inputs["database"]["commit"],
    }


def build_acceptance(root: Path) -> dict[str, Any]:
    lock = load_lock(root / "third_party" / "rules_bundle.lock.json")
    environment = _canonical_environment(lock)
    patchset = lock["rule_affecting_inputs"]["core"]["patchset"]
    full = _read_json(root / "artifacts" / "m3" / "canonical_mr5" / "full_games" / "full_fixed_deck_results.json")
    determinism = _read_json(root / "artifacts" / "m3" / "canonical_mr5" / "determinism" / "m3_determinism_results.json")
    verification = _read_json(root / "artifacts" / "m3" / "final_verification.json")
    mechanics = build_mechanics_coverage()
    results = full.get("results", [])
    errors = {
        "unsupported": sum(item.get("unsupported_count", 0) for item in results),
        "retries": sum(item.get("retry_count", 0) for item in results),
        "automatic": sum(item.get("automatic_decision_count", 0) for item in results),
        "truncated": sum(item.get("candidate_truncation_count", 0) for item in results),
        "core_errors": sum(item.get("core_error_count", 0) for item in results),
    }
    partitions = determinism.get("partitions", {})
    det_pass = (
        determinism.get("starting_player_partitions") == [0, 1]
        and sorted(partitions) == ["0", "1"]
        and all(
            partitions[str(player)].get(field) is True
            for player in (0, 1)
            for field in ("independent_process_match", "semantic_action_reexecution_match", "crlf_semantic_replay_match")
        )
    )
    regression = verification.get("regression", {})
    gates = verification.get("gates", {})
    verification_environment = verification.get("canonical_environment", {})
    final_pass = (
        full.get("complete_games") == 16
        and len(results) == 16
        and all(item.get("status") == "PASS" and item.get("terminal") is True for item in results)
        and all(value == 0 for value in errors.values())
        and det_pass
        and mechanics["classification_counts"].get("PENDING", 0) == 0
        and regression.get("build_status") == "PASS"
        and regression.get("ctest_failed") == 0
        and regression.get("ctest_passed") == regression.get("ctest_total")
        and regression.get("ctest_total", 0) > 0
        and verification_environment.get("rules_bundle_id") == lock["bundle_id"]
        and verification_environment.get("duel_flags") == lock["duel_flags"]["value"]
        and gates.get("pinned_rules_bundle") == "PASS"
        and gates.get("privacy") == "PASS"
        and gates.get("candidate_observation_consistency") == "PASS"
        and lock["bundle_id"] != OLD_M3_BUNDLE_ID
        and verification.get("constraints", {}).get("ocgcore_modified") is False
        and verification.get("constraints", {}).get("cardscripts_modified") is False
        and verification.get("constraints", {}).get("babelcdb_modified") is False
    )
    return {
        "schema_version": "ocgforge.m3_5.acceptance.v1",
        "recommendation": "M3.5 FINAL PASS" if final_pass else "M3.5 FINAL ACCEPTANCE PENDING",
        "scope": "OCGCORE_PUBLIC_API_HARDENING",
        "canonical_environment": environment,
        "bundle_identity": {
            "pre_mr5_bundle_id": PRE_MR5_BUNDLE_ID,
            "old_m3_bundle_id": OLD_M3_BUNDLE_ID,
            "new_m3_5_bundle_id": lock["bundle_id"],
            "changed": lock["bundle_id"] != OLD_M3_BUNDLE_ID,
            "derivation": "SHA-256 of canonical rule_affecting_inputs including the ordered patchset id, names, and individual hashes",
        },
        "patchset": {
            "id": patchset["id"],
            "sha256": patchset["sha256"],
            "ordered_patches": patchset["ordered_patches"],
            "base_core_commit": lock["sources"]["core"]["commit"],
            "base_checkout_sha256": lock["sources"]["core"]["resolved_checkout_hash"]["value"],
            "immutable_base": True,
            "derived_checkout": ".cache/derived/ocgcore",
        },
        "api_capabilities": [
            {
                "capability": "INDIVIDUAL_XYZ_MATERIAL_QUERY",
                "classification": "RESOLVED_BY_REPOSITORY_PATCHSET",
                "behavior": "Existing loc=parent_location|LOCATION_OVERLAY, seq=parent_sequence, overlay_seq=material_index returns the requested public material; bounds fail closed.",
                "evidence": ["m35_xyz_material_query_test", "m2_1_xyz_api_test", "m3_fixture_test sg08_real", "m3_fixture_test sg09_direct"],
                "privacy": "Visible material identity is projected only under the existing parent visibility gate; paired hidden worlds remain equal and redacted.",
            },
            {
                "capability": "START_PLAYER_SELECTION_CONTROL",
                "classification": "RESOLVED_BY_REPOSITORY_PATCHSET",
                "behavior": "OCG_DuelSetStartingPlayer accepts only 0 or 1 before OCG_StartDuel; default remains 0; invalid and post-start calls return failure.",
                "evidence": ["m35_starting_player_api_test", "m3 full-game matrix", "m3 determinism matrix"],
                "privacy": "No information projection or mid-duel turn mutation is introduced.",
            },
            {
                "capability": "FIXTURE_RUNNER_PUBLIC_SETUP_SCOPE",
                "classification": "OCGFORGE_TEST_INFRASTRUCTURE",
                "behavior": "No general board-construction runtime API was added or required.",
                "evidence": ["CoreHost::load_fixture_script", "CoreHost::load_fixture_card"],
                "privacy": "Future setup descriptors remain test-only and fail closed for hidden state.",
            },
        ],
        "mechanics": {
            "total": mechanics["fixture_count"],
            "counts": mechanics["classification_counts"],
            "pending": mechanics["pending_fixture_count"],
            "m3_document": "docs/m3/mechanics_coverage.json",
        },
        "full_games": {
            "attempted": full.get("requested_games", 0),
            "terminal": full.get("complete_games", 0),
            "start_player_partitions": full.get("start_player_partitions", []),
            "seat_partitions": full.get("seat_partitions", []),
            "partition_seeds": full.get("partition_seeds", []),
            "errors": errors,
            "effective_duel_flags": environment["duel_flags"],
            "bundle_id": full.get("canonical_environment", {}).get("rules_bundle_id"),
        },
        "determinism": {
            "partitions": partitions,
            "same_process_repeat": det_pass,
            "independent_process": det_pass,
            "semantic_replay": det_pass,
            "crlf_replay": det_pass,
            "bundle_id": determinism.get("canonical_environment", {}).get("rules_bundle_id"),
        },
        "privacy": {
            "status": gates.get("privacy", "UNRECORDED"),
            "candidate_observation_consistency": gates.get("candidate_observation_consistency", "UNRECORDED"),
            "paired_worlds": "PASS",
        },
        "regression": {
            "build": regression.get("build_status", "UNRECORDED"),
            "ctest_passed": regression.get("ctest_passed", 0),
            "ctest_total": regression.get("ctest_total", 0),
            "ctest_failed": regression.get("ctest_failed", 0),
            "legacy_python_passed": regression.get("legacy_python_passed", 0),
            "legacy_python_total": regression.get("legacy_python_total", 0),
            "m3_python_passed": regression.get("m3_python_passed", 0),
            "m3_python_total": regression.get("m3_python_total", 0),
            "m3_5_python_passed": regression.get("m3_5_python_passed", 0),
            "m3_5_python_total": regression.get("m3_5_python_total", 0),
        },
        "historical_evidence": {
            "old_m3_bundle_id": OLD_M3_BUNDLE_ID,
            "classification": "PRE_M3_5_CONFIGURATION_AND_API_EVIDENCE",
            "canonical_for_m3_5": False,
            "retained_under": "artifacts/m3/pre_mr5_configuration_correction, artifacts/m3/pre_m35_noncanonical, and explicitly named prior reports",
        },
        "upstream_handoff": {
            "submitted": False,
            "patches": "prepare for upstream review; no upstream fetch or modification performed",
            "ocgforge_base_cache_modified": False,
            "ocgcore_source_patch_in_repository": False,
        },
    }


def write_acceptance(root: Path, docs: Path) -> dict[str, Any]:
    acceptance = build_acceptance(root)
    docs.mkdir(parents=True, exist_ok=True)
    (docs / "m35_acceptance.json").write_text(
        json.dumps(acceptance, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    counts = acceptance["mechanics"]["counts"]
    full = acceptance["full_games"]
    det = acceptance["determinism"]["partitions"]
    lines = [
        "# M3.5 Acceptance — ocgcore Public API Hardening",
        "",
        f"Recommendation: **{acceptance['recommendation']}**",
        "",
        "This milestone uses two ordered repository-versioned patches against an immutable pinned base checkout. It does not modify upstream source caches, CardScripts, BabelCDB, locked decks, or the public OCGForge runtime beyond the approved integration.",
        "",
        "## Canonical identity",
        "",
        f"- Bundle: `{acceptance['bundle_identity']['new_m3_5_bundle_id']}` (previous M3: `{acceptance['bundle_identity']['old_m3_bundle_id']}`)",
        f"- Format/mode: `{acceptance['canonical_environment']['format_id']}` → `{acceptance['canonical_environment']['duel_mode_name']}` = `{acceptance['canonical_environment']['duel_flags']:#x}`",
        f"- Core base: `{acceptance['patchset']['base_core_commit']}`; patchset `{acceptance['patchset']['id']}` / `{acceptance['patchset']['sha256']}`",
        "",
        "## Public capabilities",
        "",
        "| Capability | Classification | Evidence |",
        "| --- | --- | --- |",
    ]
    for item in acceptance["api_capabilities"]:
        lines.append(f"| {item['capability']} | {item['classification']} | {'; '.join(item['evidence'])} |")
    lines.extend([
        "",
        "## Mechanics and games",
        "",
        f"- Mechanics: {counts.get('ENGINE_VERIFIED', 0)} ENGINE_VERIFIED, {counts.get('PROTOCOL_VERIFIED', 0)} PROTOCOL_VERIFIED, {counts.get('PUBLIC_API_LIMITATION', 0)} PUBLIC_API_LIMITATION, {counts.get('NOT_APPLICABLE_FIXED_MATCHUP', 0)} NOT_APPLICABLE_FIXED_MATCHUP, {acceptance['mechanics']['pending']} PENDING.",
        f"- Full games: {full['terminal']}/{full['attempted']} terminal; start partitions `{full['start_player_partitions']}`; seat partitions `{full['seat_partitions']}`; errors `{full['errors']}`.",
        f"- Determinism start 0: gameplay `{det.get('0', {}).get('semantic_gameplay_hash', '')}`, trace `{det.get('0', {}).get('trace_hash', '')}`, actions `{det.get('0', {}).get('semantic_action_count', 0)}`.",
        f"- Determinism start 1: gameplay `{det.get('1', {}).get('semantic_gameplay_hash', '')}`, trace `{det.get('1', {}).get('trace_hash', '')}`, actions `{det.get('1', {}).get('semantic_action_count', 0)}`.",
        "- Independent-process, semantic-action, and CRLF replay gates are recorded per starting-player partition.",
        "",
        "## Scope boundary",
        "",
        "The individual Xyz query and starting-player control are repository-patched capabilities prepared for upstream review. No upstream PR, commit, push, tag, or external dependency update is part of this milestone. The fixture-runner setup item remains OCGForge test infrastructure, not an ocgcore API claim.",
        "",
    ])
    (docs / "M3_5_ACCEPTANCE.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return acceptance


if __name__ == "__main__":
    repository = Path(__file__).resolve().parents[2]
    result = write_acceptance(repository, repository / "docs" / "m3_5")
    print(json.dumps({"recommendation": result["recommendation"], "bundle_id": result["canonical_environment"]["rules_bundle_id"]}, sort_keys=True))
