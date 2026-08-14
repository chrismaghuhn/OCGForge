from __future__ import annotations

import argparse
import json
from pathlib import Path
import tempfile

from .audit import build_matchup_audit, write_audit_reports
from .coverage import STATUS_VALUES, build_api_gaps, build_mechanics_coverage, write_coverage_reports
from .m35 import build_acceptance
from .rules_mode import CANONICAL_DUEL_FLAGS, CANONICAL_DUEL_MODE, CANONICAL_FORMAT_ID, load_canonical_environment


def _audit_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--deck-a", required=True, type=Path)
    parser.add_argument("--deck-b", required=True, type=Path)
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--scripts", required=True, type=Path)
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--json", required=True, dest="compatibility_json", type=Path)
    parser.add_argument("--markdown", required=True, dest="compatibility_markdown", type=Path)
    parser.add_argument("--check-existing", action="store_true")


def _run_audit(args: argparse.Namespace) -> int:
    audit = build_matchup_audit(
        deck_a=args.deck_a,
        deck_b=args.deck_b,
        database=args.database,
        scripts=args.scripts,
        lock=args.lock,
    )
    if not args.check_existing:
        write_audit_reports(
            audit,
            manifest=args.manifest,
            compatibility_json=args.compatibility_json,
            compatibility_markdown=args.compatibility_markdown,
        )
        return 0

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        write_audit_reports(
            audit,
            manifest=root / "manifest.json",
            compatibility_json=root / "compatibility.json",
            compatibility_markdown=root / "compatibility.md",
        )
        expected = {
            args.manifest: root / "manifest.json",
            args.compatibility_json: root / "compatibility.json",
            args.compatibility_markdown: root / "compatibility.md",
        }
        mismatches = []
        for actual, generated in expected.items():
            if not actual.is_file() or actual.read_bytes() != generated.read_bytes():
                mismatches.append(str(actual))
        if mismatches:
            raise SystemExit("audit output differs: " + ", ".join(mismatches))
    return 0


def _require_canonical_environment(value: dict, expected: dict, label: str) -> None:
    for key in (
        "format_id",
        "duel_mode_name",
        "duel_flags",
        "rules_bundle_id",
        "ocgcore_commit",
        "core_patchset_id",
        "core_patchset_sha256",
        "ocg_api_version",
        "cardscripts_commit",
        "babelcdb_commit",
    ):
        if value.get(key) != expected[key]:
            raise SystemExit(f"{label} disagrees with the canonical environment: {key}")


def _require_canonical_run_fields(value: dict, expected: dict, label: str) -> None:
    for key in ("format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
                "core_patchset_id", "core_patchset_sha256"):
        if value.get(key) != expected[key]:
            raise SystemExit(f"{label} disagrees with the canonical environment: {key}")


def _validate_canonical_artifacts(root: Path, environment: dict) -> None:
    full_path = root / "artifacts" / "m3" / "canonical_mr5" / "full_games" / "full_fixed_deck_results.json"
    determinism_path = root / "artifacts" / "m3" / "canonical_mr5" / "determinism" / "m3_determinism_results.json"
    if not full_path.is_file() or not determinism_path.is_file():
        raise SystemExit("canonical MR5 full-game and determinism artifacts are required")
    full_games = json.loads(full_path.read_text(encoding="utf-8"))
    _require_canonical_environment(full_games.get("canonical_environment", {}), environment, "full-game runner")
    if full_games.get("requested_games") != 16 or full_games.get("complete_games") != 16 or len(full_games.get("results", [])) != 16:
        raise SystemExit("canonical full-game artifact must contain 16 complete games")
    if full_games.get("start_player_partitions") != [0, 1] or full_games.get("seat_partitions") != ["normal", "mirror"]:
        raise SystemExit("canonical full-game artifact must cover both starting-player and seat partitions")
    for index, result in enumerate(full_games.get("results", [])):
        _require_canonical_run_fields(result, environment, f"full-game result {index}")
        if result.get("status") != "PASS" or result.get("terminal") is not True:
            raise SystemExit(f"canonical full-game result {index} is not terminal PASS")
        if any(result.get(field, 0) != 0 for field in (
            "unsupported_count", "retry_count", "automatic_decision_count",
            "candidate_truncation_count", "core_error_count",
        )):
            raise SystemExit(f"canonical full-game result {index} has a nonzero rejection/error count")
    determinism = json.loads(determinism_path.read_text(encoding="utf-8"))
    _require_canonical_environment(determinism.get("canonical_environment", {}), environment, "determinism runner")
    if determinism.get("starting_player_partitions") != [0, 1]:
        raise SystemExit("determinism runner must cover starting-player partitions [0, 1]")
    partitions = determinism.get("partitions", {})
    if sorted(partitions) != ["0", "1"]:
        raise SystemExit("determinism runner is missing a starting-player partition")
    for player in (0, 1):
        partition = partitions[str(player)]
        if partition.get("starting_player") != player:
            raise SystemExit(f"determinism partition {player} records the wrong starting player")
        for field in ("independent_process_match", "semantic_action_reexecution_match",
                      "crlf_semantic_replay_match"):
            if partition.get(field) is not True:
                raise SystemExit(f"determinism partition {player} failed {field}")


def _run_validate_docs(args: argparse.Namespace) -> int:
    docs = Path(args.docs)
    root = docs.parents[1]
    environment = load_canonical_environment(root / "third_party" / "rules_bundle.lock.json")
    _validate_canonical_artifacts(root, environment)
    required = (
        docs / "card_compatibility.json",
        docs / "CARD_COMPATIBILITY.md",
        docs / "mechanics_coverage.json",
        docs / "MECHANICS_COVERAGE.md",
        docs / "public_api_gaps.json",
        docs / "PUBLIC_API_GAPS.md",
        docs / "m3_acceptance_matrix.json",
        docs / "M3_ACCEPTANCE_MATRIX.md",
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise SystemExit("missing M3 documentation: " + ", ".join(missing))
    compatibility = json.loads((docs / "card_compatibility.json").read_text(encoding="utf-8"))
    if len(compatibility.get("slots", [])) != 110:
        raise SystemExit("card compatibility documentation does not contain 110 slots")
    coverage = json.loads((docs / "mechanics_coverage.json").read_text(encoding="utf-8"))
    rows = coverage.get("rows", [])
    if len(rows) < 45:
        raise SystemExit(f"mechanics coverage documentation does not contain the 45 required rows: {len(rows)}")
    if any(row.get("status") not in STATUS_VALUES for row in rows):
        raise SystemExit("mechanics coverage contains an unknown status")
    if coverage.get("fixture_count") != len(rows):
        raise SystemExit("mechanics coverage fixture_count does not match row count")
    expected_counts = {status: sum(row.get("status") == status for row in rows) for status in STATUS_VALUES}
    if coverage.get("classification_counts") != expected_counts:
        raise SystemExit("mechanics coverage classification counts do not match row statuses")
    for row in rows:
        status = row.get("status")
        if status == "ENGINE_VERIFIED":
            if not row.get("evidence_source") or not row.get("engine_evidence") or not row.get("observation_evidence"):
                raise SystemExit(f"ENGINE_VERIFIED row lacks fixture evidence: {row.get('key')}")
            if not row.get("observation_hash_chain_sha256"):
                raise SystemExit(f"ENGINE_VERIFIED row lacks observation hash evidence: {row.get('key')}")
        elif status == "PROTOCOL_VERIFIED":
            if not row.get("evidence_source") or not row.get("protocol_evidence"):
                raise SystemExit(f"PROTOCOL_VERIFIED row lacks protocol evidence: {row.get('key')}")
        elif status == "PUBLIC_API_LIMITATION":
            if not row.get("public_api_gap_id"):
                raise SystemExit(f"PUBLIC_API_LIMITATION row lacks public_api_gap_id: {row.get('key')}")
        elif status == "NOT_APPLICABLE_FIXED_MATCHUP":
            if not row.get("rationale") or not row.get("source_evidence"):
                raise SystemExit(f"NOT_APPLICABLE row lacks rationale/source evidence: {row.get('key')}")
        elif status == "PENDING" and not row.get("blocker"):
            raise SystemExit(f"PENDING row is missing a blocker: {row.get('key')}")
    api_gaps = json.loads((docs / "public_api_gaps.json").read_text(encoding="utf-8"))
    api_ids = {row.get("gap_id") for row in api_gaps.get("rows", [])}
    if "INDIVIDUAL_XYZ_MATERIAL_QUERY" not in api_ids:
        raise SystemExit("known Xyz material API limitation is missing")
    if any(row.get("status") == "PUBLIC_API_LIMITATION" and row.get("public_api_gap_id") not in api_ids
           for row in rows):
        raise SystemExit("mechanics public API limitation lacks a matching public_api_gaps entry")
    rules_mode = json.loads((docs / "rules_mode_audit.json").read_text(encoding="utf-8"))
    if rules_mode.get("format_id") != CANONICAL_FORMAT_ID:
        raise SystemExit("rules-mode audit does not identify the locked TCG format")
    if rules_mode.get("status") != "RESOLVED_CONFIGURATION_CORRECTION":
        raise SystemExit("rules-mode audit does not mark the MR5 correction resolved")
    canonical = rules_mode.get("canonical", {})
    if canonical.get("duel_mode") != CANONICAL_DUEL_MODE:
        raise SystemExit("rules-mode audit does not record canonical DUEL_MODE_MR5")
    if canonical.get("duel_flags", {}).get("value") != CANONICAL_DUEL_FLAGS:
        raise SystemExit("rules-mode audit does not record canonical MR5 flags")
    if canonical.get("rules_bundle_id") != environment["rules_bundle_id"]:
        raise SystemExit("rules-mode audit bundle identity does not match the lock")
    matrix = json.loads((docs / "m3_acceptance_matrix.json").read_text(encoding="utf-8"))
    if matrix.get("schema_version") != "ocgforge.m3.acceptance_matrix.v1":
        raise SystemExit("M3 acceptance matrix schema is invalid")
    matrix_rows = matrix.get("rows", [])
    if len(matrix_rows) < 45:
        raise SystemExit("M3 acceptance matrix does not contain all required mechanics rows")
    matrix_mechanics = [row for row in matrix_rows if row.get("criterion_id") in {item.get("key") for item in rows}]
    if len(matrix_mechanics) != len(rows):
        raise SystemExit("M3 acceptance matrix mechanics rows do not match mechanics coverage rows")
    if any(row.get("status") != next(item["status"] for item in rows if item.get("key") == row.get("criterion_id"))
           for row in matrix_mechanics):
        raise SystemExit("M3 acceptance matrix mechanics statuses do not match mechanics coverage")
    if matrix.get("mechanics_counts", {}).get("total") != len(rows):
        raise SystemExit("M3 acceptance matrix mechanics total does not match coverage")
    for key, count in expected_counts.items():
        matrix_key = {
            "ENGINE_VERIFIED": "engine_verified",
            "PROTOCOL_VERIFIED": "protocol_verified",
            "PUBLIC_API_LIMITATION": "public_api_limitations",
            "NOT_APPLICABLE_FIXED_MATCHUP": "not_applicable_fixed_matchup",
            "PENDING": "pending",
        }[key]
        if matrix.get("mechanics_counts", {}).get(matrix_key) != count:
            raise SystemExit(f"M3 acceptance matrix {matrix_key} count does not match coverage")
    canonical_matrix = matrix.get("canonical_environment", {})
    _require_canonical_environment(canonical_matrix, environment, "acceptance matrix")
    if matrix.get("recommendation") not in {"M3 FINAL PASS", "M3 FINAL ACCEPTANCE PENDING"}:
        raise SystemExit("M3 acceptance matrix recommendation is invalid")
    return 0


def _run_validate_m35(args: argparse.Namespace) -> int:
    docs = Path(args.docs)
    root = docs.parents[1]
    acceptance_path = docs / "m35_acceptance.json"
    if not acceptance_path.is_file() or not (docs / "M3_5_ACCEPTANCE.md").is_file() or not (docs / "PUBLIC_API_HARDENING.md").is_file():
        raise SystemExit("M3.5 acceptance documentation is incomplete")
    acceptance = json.loads(acceptance_path.read_text(encoding="utf-8"))
    if acceptance.get("schema_version") != "ocgforge.m3_5.acceptance.v1":
        raise SystemExit("M3.5 acceptance schema is invalid")
    environment = load_canonical_environment(root / "third_party" / "rules_bundle.lock.json")
    expected_environment = {
        key: environment[key]
        for key in ("format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
                    "ocgcore_commit", "core_patchset_id", "core_patchset_sha256",
                    "ocg_api_version", "cardscripts_commit", "babelcdb_commit")
    }
    if acceptance.get("canonical_environment") != expected_environment:
        raise SystemExit("M3.5 acceptance canonical environment disagrees with the lock")
    lock = json.loads((root / "third_party" / "rules_bundle.lock.json").read_text(encoding="utf-8"))
    expected_patchset = lock["rule_affecting_inputs"]["core"]["patchset"]
    patchset = acceptance.get("patchset", {})
    if patchset.get("id") != expected_patchset["id"] or patchset.get("sha256") != expected_patchset["sha256"] or patchset.get("ordered_patches") != expected_patchset["ordered_patches"]:
        raise SystemExit("M3.5 acceptance patchset disagrees with the lock")
    if patchset.get("base_core_commit") != expected_environment["ocgcore_commit"] or patchset.get("immutable_base") is not True:
        raise SystemExit("M3.5 acceptance does not prove the immutable pinned base")
    capabilities = {row.get("capability"): row for row in acceptance.get("api_capabilities", [])}
    for capability in ("INDIVIDUAL_XYZ_MATERIAL_QUERY", "START_PLAYER_SELECTION_CONTROL"):
        row = capabilities.get(capability)
        if not row or row.get("classification") != "RESOLVED_BY_REPOSITORY_PATCHSET" or not row.get("evidence"):
            raise SystemExit(f"M3.5 capability is not closed with evidence: {capability}")
    mechanics = json.loads((root / "docs" / "m3" / "mechanics_coverage.json").read_text(encoding="utf-8"))
    if acceptance.get("mechanics", {}).get("counts") != mechanics.get("classification_counts") or mechanics.get("classification_counts", {}).get("PENDING") != 0:
        raise SystemExit("M3.5 mechanics counts do not match M3 coverage or still contain PENDING")
    _validate_canonical_artifacts(root, environment)
    full = json.loads((root / "artifacts" / "m3" / "canonical_mr5" / "full_games" / "full_fixed_deck_results.json").read_text(encoding="utf-8"))
    full_acceptance = acceptance.get("full_games", {})
    if full_acceptance.get("attempted") != full.get("requested_games") or full_acceptance.get("terminal") != full.get("complete_games"):
        raise SystemExit("M3.5 full-game acceptance counts disagree with the artifact")
    if full_acceptance.get("errors", {}).get("core_errors", 0) != 0:
        raise SystemExit("M3.5 full-game acceptance records core errors")
    determinism = json.loads((root / "artifacts" / "m3" / "canonical_mr5" / "determinism" / "m3_determinism_results.json").read_text(encoding="utf-8"))
    if acceptance.get("determinism", {}).get("partitions") != determinism.get("partitions"):
        raise SystemExit("M3.5 determinism partitions disagree with the artifact")
    privacy = acceptance.get("privacy", {})
    if privacy.get("status") != "PASS" or privacy.get("candidate_observation_consistency") != "PASS":
        raise SystemExit("M3.5 privacy/candidate-observation gate is not PASS")
    if acceptance.get("recommendation") not in {"M3.5 FINAL PASS", "M3.5 FINAL ACCEPTANCE PENDING"}:
        raise SystemExit("M3.5 recommendation is invalid")
    if acceptance.get("recommendation") == "M3.5 FINAL PASS":
        expected = build_acceptance(root)
        if expected.get("recommendation") != "M3.5 FINAL PASS":
            raise SystemExit("M3.5 FINAL PASS is not reproducible from current artifacts")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="python -m tools.m3.cli")
    commands = parser.add_subparsers(dest="command", required=True)
    audit = commands.add_parser("audit")
    _audit_arguments(audit)
    validate = commands.add_parser("validate-docs")
    validate.add_argument("--docs", required=True, type=Path)
    coverage = commands.add_parser("generate-coverage")
    coverage.add_argument("--docs", required=True, type=Path)
    m35_validate = commands.add_parser("validate-m35")
    m35_validate.add_argument("--docs", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "audit":
        return _run_audit(args)
    if args.command == "validate-docs":
        return _run_validate_docs(args)
    if args.command == "generate-coverage":
        write_coverage_reports(args.docs)
        return 0
    if args.command == "validate-m35":
        return _run_validate_m35(args)
    raise AssertionError(f"unhandled M3 command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
