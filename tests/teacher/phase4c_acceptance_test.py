from __future__ import annotations

import copy
import importlib.util
import json
import subprocess
import tempfile
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / "tools" / "p4c" / "phase4c_acceptance.py"
SPEC = importlib.util.spec_from_file_location("phase4c_acceptance", GENERATOR_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load the Phase-4C acceptance generator")
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)

SCHEMA = GENERATOR.SCHEMA
TASK5_BASE = GENERATOR.TASK5_BASE
PHASE4A_H_EXEC = GENERATOR.PHASE4A_H_EXEC
PHASE4A_H_EVIDENCE = GENERATOR.PHASE4A_H_EVIDENCE
GATE_IDS = GENERATOR.GATE_IDS
STATUSES = GENERATOR.STATUSES
REQUIRED_COMMAND_LABELS = GENERATOR.REQUIRED_COMMAND_LABELS
ROWS = GENERATOR.ROWS
SHA256 = GENERATOR.SHA256
HEAD = GENERATOR.HEAD

REQUIRED_TOP_LEVEL = {
    "schema_version",
    "status",
    "source_base",
    "source_head",
    "environment",
    "teacher",
    "battle_contracts",
    "integration_decision",
    "positive_lethal_capability",
    "gates",
    "fixed_matchup_matrix",
    "task5_metrics",
    "command_evidence",
    "required_command_evidence",
    "heavy_evidence",
    "scope_limitations",
}
REQUIRED_COMMAND_FIELDS = {
    "label",
    "command",
    "status",
    "exit_code",
    "expected_selected_count",
    "observed_selected_count",
    "stdout_sha256",
    "stderr_sha256",
}

FROZEN_EXPECTED_COUNTS: dict[str, int | None] = {
    label: None for label in REQUIRED_COMMAND_LABELS
}
FROZEN_EXPECTED_COUNTS.update(
    {
        "g00-public-boundary-ctest-cardinality": 1,
        "g00-public-boundary-ctest": 1,
        "g00-lethal-boundary-ctest-cardinality": 1,
        "g00-lethal-boundary-ctest": 1,
        "g01-03-snapshot-ctest-cardinality": 1,
        "g01-03-snapshot-ctest": 1,
        "g04-battle-shape-ctest-cardinality": 1,
        "g04-battle-shape-ctest": 1,
        "g07-10-lethal-ctest-cardinality": 1,
        "g07-10-lethal-ctest": 1,
        "g11-identity-ctest-cardinality": 1,
        "g11-identity-ctest": 1,
        "g12-trajectory-ctest-cardinality": 2,
        "g12-trajectory-ctest": 2,
        "g14-teacher-regression-ctest-cardinality": 4,
        "g14-teacher-regression-ctest": 4,
        "task5-trajectory-ctest-cardinality": 1,
        "task5-trajectory-ctest": 1,
        "phase4b-full-teacher-ctest-cardinality": 10,
        "phase4b-full-teacher-ctest": 10,
        "trajectory-short-ctest-cardinality": 5,
        "trajectory-short-ctest": 5,
        "repository-python": 15,
    }
)
if set(FROZEN_EXPECTED_COUNTS) != set(REQUIRED_COMMAND_LABELS):
    raise RuntimeError("frozen cardinality mapping does not cover every command label")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def reject(
    mutated: dict[str, Any],
    message: str,
    expected_source_head: str | None = None,
) -> None:
    try:
        validate_report(mutated, expected_source_head=expected_source_head)
    except AssertionError:
        return
    raise AssertionError(f"negative case was accepted: {message}")


def validate_report(
    report: dict[str, Any], expected_source_head: str | None = None
) -> None:
    require(set(report) == REQUIRED_TOP_LEVEL, "acceptance report top-level schema drift")
    require(report["schema_version"] == SCHEMA, "wrong acceptance schema")
    require(report["status"] in STATUSES, "invalid acceptance status")
    require(
        isinstance(report["source_base"], str)
        and HEAD.fullmatch(report["source_base"]),
        "invalid source_base",
    )
    require(
        isinstance(report["source_head"], str)
        and HEAD.fullmatch(report["source_head"]),
        "invalid source_head",
    )
    if expected_source_head is not None:
        require(report["source_head"] == expected_source_head, "source_head mismatch")
    require(report["source_base"] == TASK5_BASE, "wrong Task-5 source base")

    require(report["environment"] == GENERATOR.ENVIRONMENT, "environment identity drift")
    require(
        report["battle_contracts"] == {
            "snapshot_schema": GENERATOR.SNAPSHOT_SCHEMA,
            "lethal_schema": GENERATOR.LETHAL_SCHEMA,
        },
        "Battle/Lethal contract identity drift",
    )
    require(
        report["integration_decision"] == GENERATOR.INTEGRATION_DECISION,
        "integration decision drift",
    )
    require(
        report["positive_lethal_capability"]
        == GENERATOR.POSITIVE_LETHAL_CAPABILITY,
        "positive-lethal limitation drift",
    )

    teacher = report["teacher"]
    require(teacher == GENERATOR.TEACHER, "Teacher identity drift")
    serialized = json.dumps(report, sort_keys=True)
    for forbidden in (
        "ocgforge.policy.teacher_core.v2",
        "DecisionFrame",
        "SubmissionToken",
        "CoreHost",
        "PlayerObservation",
        "semantic_key",
        "response_bytes",
        "passcode",
        "C:\\",
        "C:/",
        "PID=",
    ):
        require(forbidden not in serialized, f"forbidden evidence value: {forbidden}")

    gates = report["gates"]
    require(isinstance(gates, list), "gates must be a list")
    require([item.get("gate") for item in gates] == GATE_IDS, "gate IDs are not exact")

    evidence = report["command_evidence"]
    require(isinstance(evidence, list), "command_evidence must be a list")
    labels = [item.get("label") for item in evidence]
    require(labels == REQUIRED_COMMAND_LABELS, "command labels are missing, duplicate, or unexpected")
    evidence_by_label: dict[str, dict[str, Any]] = {}
    for item in evidence:
        require(set(item) == REQUIRED_COMMAND_FIELDS, "command evidence contains an unapproved field")
        label = item["label"]
        require(isinstance(label, str) and label not in evidence_by_label, "duplicate command label")
        require(item["status"] in STATUSES, f"invalid command status: {label}")
        require(isinstance(item["command"], str) and item["command"], f"missing command: {label}")
        require(
            not any(marker in item["command"] for marker in ("C:\\", "C:/", "\\\\")),
            f"absolute path in command evidence: {label}",
        )
        require(isinstance(item["exit_code"], int), f"missing exit code: {label}")
        for key in ("expected_selected_count", "observed_selected_count"):
            require(
                item[key] is None or isinstance(item[key], int),
                f"invalid cardinality field: {label}:{key}",
            )
        frozen_count = FROZEN_EXPECTED_COUNTS[label]
        require(
            item["expected_selected_count"] == frozen_count,
            f"self-declared cardinality disagrees with frozen label contract: {label}",
        )
        if frozen_count is None:
            require(
                item["observed_selected_count"] is None,
                f"non-cardinality command reported an observed count: {label}",
            )
        require(
            item["expected_selected_count"] is None
            or item["observed_selected_count"] == item["expected_selected_count"],
            f"CTest/Python cardinality mismatch: {label}",
        )
        require(SHA256.fullmatch(item["stdout_sha256"] or ""), f"invalid stdout SHA: {label}")
        require(SHA256.fullmatch(item["stderr_sha256"] or ""), f"invalid stderr SHA: {label}")
        if item["status"] == "PASS":
            require(item["exit_code"] == 0, f"PASS command has nonzero exit: {label}")
        evidence_by_label[label] = item

    require(report["required_command_evidence"] == REQUIRED_COMMAND_LABELS,
            "required command evidence set is not exact")
    for label in report["required_command_evidence"]:
        require(label in evidence_by_label, f"missing required command evidence: {label}")

    for item in gates:
        require(item["status"] in STATUSES, f"invalid gate status: {item['gate']}")
        exact_evidence = item.get("exact_evidence")
        require(isinstance(exact_evidence, list) and exact_evidence, f"gate lacks evidence: {item['gate']}")
        require(len(exact_evidence) == len(set(exact_evidence)), f"gate evidence duplicated: {item['gate']}")
        for label in exact_evidence:
            require(label in evidence_by_label, f"gate references missing command: {item['gate']}:{label}")
        if item["status"] == "PASS":
            require(
                all(evidence_by_label[label]["status"] == "PASS" for label in exact_evidence),
                f"PASS gate references failed evidence: {item['gate']}",
            )

    matrix = report["fixed_matchup_matrix"]
    require(isinstance(matrix, list) and len(matrix) == 4, "fixed matrix must have four rows")
    observed_rows = [(row.get("seat_assignment"), row.get("starting_player")) for row in matrix]
    require(observed_rows == list(ROWS), "fixed matrix rows are missing, duplicate, or reordered")
    for row in matrix:
        require(row.get("status") in STATUSES, "invalid fixed matrix status")
        for key in (
            "record_count",
            "battle_decision_record_count",
            "battle_command_candidate_count",
            "sidecar_invalid_count",
            "proven_lethal_count",
            "lower_bound_present_count",
        ):
            require(isinstance(row.get(key), int) and row[key] >= 0, f"invalid matrix metric: {key}")
        if report["status"] == "PASS":
            require(row["status"] == "PASS", "overall PASS has a failed matrix row")

    metrics = report["task5_metrics"]
    expected_metric_keys = {
        "record_count",
        "battle_decision_record_count",
        "battle_command_candidate_count",
        "battle_coverage",
        "sidecar_invalid_count",
        "proven_lethal_count",
        "lower_bound_present_count",
        "sidecar_influences_gameplay",
    }
    require(set(metrics) == expected_metric_keys, "Task-5 metric schema drift")
    for key in expected_metric_keys - {"battle_coverage", "sidecar_influences_gameplay"}:
        require(isinstance(metrics[key], int) and metrics[key] >= 0, f"invalid Task-5 metric: {key}")
    require(metrics["battle_coverage"] in {"PASS", "FAIL"}, "invalid Battle coverage status")
    require(metrics["sidecar_influences_gameplay"] == "NO", "sidecar influence changed")

    heavy = report["heavy_evidence"]
    require(
        heavy
        == {
            "current_head_heavy_replay": "NOT_RUN",
            "phase4a_h_exec": PHASE4A_H_EXEC,
            "phase4a_h_evidence": PHASE4A_H_EVIDENCE,
            "mode": "frozen_baseline_reference",
        },
        "heavy evidence is not correctly bounded",
    )
    require(isinstance(report["scope_limitations"], list), "scope limitations must be a list")
    for limitation in (
        "fixed certified matchup only",
        "Teacher v1 remains gameplay policy",
        "Battle/Lethal are evaluation-only sidecar evidence",
        "positive lethal is blocked under the accepted current-action contract",
        "no general provable-lethal claim",
        "no complete battle-resolution claim",
        "no arbitrary-deck battle-intelligence claim",
        "no Teacher-v2 claim",
        "no ML claim",
        "no Phase 5 claim",
    ):
        require(limitation in report["scope_limitations"], f"missing scope limitation: {limitation}")

    if report["status"] == "PASS":
        require(all(item["status"] == "PASS" for item in gates), "overall PASS requires all gates PASS")
        require(all(item["status"] == "PASS" for item in evidence), "overall PASS contains failed command evidence")
        require(all(row["status"] == "PASS" for row in matrix), "overall PASS contains failed matrix evidence")
        require(metrics["battle_coverage"] == "PASS", "overall PASS has no Battle coverage")
        require(metrics["battle_decision_record_count"] > 0, "overall PASS has no Battle decision records")
        require(metrics["battle_command_candidate_count"] > 0, "overall PASS has no Battle candidates")
        require(metrics["sidecar_invalid_count"] == 0, "overall PASS has sidecar INVALID evidence")
        require(metrics["proven_lethal_count"] == 0, "overall PASS has PROVEN_LETHAL evidence")
        require(metrics["lower_bound_present_count"] == 0, "overall PASS has a lethal lower bound")
        require(report["source_base"] == TASK5_BASE, "overall PASS has wrong source base")


def validate_files(
    json_path: Path,
    markdown_path: Path,
    expected_source_head: str | None = None,
) -> None:
    json_text = json_path.read_text(encoding="utf-8")
    report = json.loads(json_text)
    require(
        json.dumps(report, indent=2, sort_keys=True) + "\n" == json_text,
        "JSON is not the canonical generated representation",
    )
    validate_report(report, expected_source_head=expected_source_head)
    markdown = markdown_path.read_text(encoding="utf-8")
    require(markdown == GENERATOR.render_markdown(report), "Markdown is inconsistent with JSON model")


def valid_report() -> dict[str, Any]:
    command_evidence = []
    for index, label in enumerate(REQUIRED_COMMAND_LABELS):
        frozen_count = FROZEN_EXPECTED_COUNTS[label]
        command_evidence.append(
            {
                "label": label,
                "command": f"command-{index}",
                "status": "PASS",
                "exit_code": 0,
                "expected_selected_count": frozen_count,
                "observed_selected_count": frozen_count,
                "stdout_sha256": GENERATOR.sha256_bytes(f"stdout-{index}".encode()),
                "stderr_sha256": GENERATOR.sha256_bytes(b""),
            }
        )
    return {
        "schema_version": SCHEMA,
        "status": "PASS",
        "source_base": TASK5_BASE,
        "source_head": "1" * 40,
        "environment": copy.deepcopy(GENERATOR.ENVIRONMENT),
        "teacher": copy.deepcopy(GENERATOR.TEACHER),
        "battle_contracts": {
            "snapshot_schema": GENERATOR.SNAPSHOT_SCHEMA,
            "lethal_schema": GENERATOR.LETHAL_SCHEMA,
        },
        "integration_decision": GENERATOR.INTEGRATION_DECISION,
        "positive_lethal_capability": GENERATOR.POSITIVE_LETHAL_CAPABILITY,
        "gates": [
            {
                "gate": gate_id,
                "invariant": GENERATOR.GATE_INVARIANTS[gate_id],
                "status": "PASS",
                "exact_evidence": copy.deepcopy(GENERATOR.GATE_EVIDENCE[gate_id]),
                "pass_condition": "all exact evidence records pass with the frozen cardinality",
            }
            for gate_id in GATE_IDS
        ],
        "fixed_matchup_matrix": [
            {
                "seat_assignment": seat,
                "starting_player": player,
                "status": "PASS",
                "record_count": 4,
                "battle_decision_record_count": 1,
                "battle_command_candidate_count": 2,
                "sidecar_invalid_count": 0,
                "proven_lethal_count": 0,
                "lower_bound_present_count": 0,
            }
            for seat, player in ROWS
        ],
        "task5_metrics": {
            "record_count": 16,
            "battle_decision_record_count": 4,
            "battle_command_candidate_count": 8,
            "battle_coverage": "PASS",
            "sidecar_invalid_count": 0,
            "proven_lethal_count": 0,
            "lower_bound_present_count": 0,
            "sidecar_influences_gameplay": "NO",
        },
        "command_evidence": command_evidence,
        "required_command_evidence": REQUIRED_COMMAND_LABELS.copy(),
        "heavy_evidence": {
            "current_head_heavy_replay": "NOT_RUN",
            "phase4a_h_exec": PHASE4A_H_EXEC,
            "phase4a_h_evidence": PHASE4A_H_EVIDENCE,
            "mode": "frozen_baseline_reference",
        },
        "scope_limitations": [
            "fixed certified matchup only",
            "Teacher v1 remains gameplay policy",
            "Battle/Lethal are evaluation-only sidecar evidence",
            "positive lethal is blocked under the accepted current-action contract",
            "no general provable-lethal claim",
            "no complete battle-resolution claim",
            "no arbitrary-deck battle-intelligence claim",
            "no Teacher-v2 claim",
            "no ML claim",
            "no Phase 5 claim",
        ],
    }


def run_negative_self_tests() -> None:
    baseline = valid_report()
    validate_report(baseline)

    missing_command = copy.deepcopy(baseline)
    missing_command["command_evidence"].pop()
    reject(missing_command, "missing required command")

    duplicate_command = copy.deepcopy(baseline)
    duplicate_command["command_evidence"].append(copy.deepcopy(duplicate_command["command_evidence"][0]))
    reject(duplicate_command, "duplicate command label")

    failed_required = copy.deepcopy(baseline)
    failed_required["command_evidence"][0]["status"] = "FAIL"
    failed_required["command_evidence"][0]["exit_code"] = 1
    reject(failed_required, "failed required command")

    failed_unreferenced = copy.deepcopy(baseline)
    failed_unreferenced["command_evidence"].append(
        {
            "label": "unreferenced-failure",
            "command": "command-extra",
            "status": "FAIL",
            "exit_code": 1,
            "expected_selected_count": None,
            "observed_selected_count": None,
            "stdout_sha256": GENERATOR.sha256_bytes(b""),
            "stderr_sha256": GENERATOR.sha256_bytes(b"failure"),
        }
    )
    reject(failed_unreferenced, "failed unreferenced auxiliary command")

    wrong_count = copy.deepcopy(baseline)
    wrong_count_record = next(
        item for item in wrong_count["command_evidence"]
        if item["label"] == "g12-trajectory-ctest"
    )
    wrong_count_record["expected_selected_count"] = 1
    wrong_count_record["observed_selected_count"] = 1
    reject(wrong_count, "wrong self-declared multi-target CTest cardinality")

    old_wrong_count = copy.deepcopy(baseline)
    old_wrong_count_record = next(
        item for item in old_wrong_count["command_evidence"]
        if item["label"] == "g12-trajectory-ctest"
    )
    old_wrong_count_record["observed_selected_count"] = 1
    reject(old_wrong_count, "wrong observed CTest selected count")

    noncount_cardinality = copy.deepcopy(baseline)
    source_head_record = next(
        item for item in noncount_cardinality["command_evidence"]
        if item["label"] == "source-head"
    )
    source_head_record["expected_selected_count"] = 1
    source_head_record["observed_selected_count"] = 1
    reject(noncount_cardinality, "unexpected cardinality on non-count command")

    missing_gate = copy.deepcopy(baseline)
    missing_gate["gates"].pop()
    reject(missing_gate, "missing gate")

    duplicate_gate = copy.deepcopy(baseline)
    duplicate_gate["gates"][1]["gate"] = duplicate_gate["gates"][0]["gate"]
    reject(duplicate_gate, "duplicate gate")

    non_pass_gate = copy.deepcopy(baseline)
    non_pass_gate["gates"][0]["status"] = "FAIL"
    reject(non_pass_gate, "non-PASS gate")

    missing_row = copy.deepcopy(baseline)
    missing_row["fixed_matchup_matrix"].pop()
    reject(missing_row, "missing matrix row")

    duplicate_row = copy.deepcopy(baseline)
    duplicate_row["fixed_matchup_matrix"][1] = copy.deepcopy(duplicate_row["fixed_matchup_matrix"][0])
    reject(duplicate_row, "duplicate matrix row")

    failed_row = copy.deepcopy(baseline)
    failed_row["fixed_matchup_matrix"][0]["status"] = "FAIL"
    reject(failed_row, "failed matrix row")

    wrong_base = copy.deepcopy(baseline)
    wrong_base["source_base"] = "2" * 40
    reject(wrong_base, "wrong source base")

    wrong_head = copy.deepcopy(baseline)
    wrong_head["source_head"] = "0" * 40
    reject(wrong_head, "wrong source head", expected_source_head=baseline["source_head"])

    wrong_snapshot = copy.deepcopy(baseline)
    wrong_snapshot["battle_contracts"]["snapshot_schema"] = "wrong.snapshot.v1"
    reject(wrong_snapshot, "wrong Battle schema")

    wrong_lethal = copy.deepcopy(baseline)
    wrong_lethal["battle_contracts"]["lethal_schema"] = "wrong.lethal.v1"
    reject(wrong_lethal, "wrong Lethal schema")

    wrong_teacher = copy.deepcopy(baseline)
    wrong_teacher["teacher"]["producer_implementation_identity"] = "ocgforge.policy.teacher_core.v2"
    reject(wrong_teacher, "Teacher-v2 identity")

    corrupt_teacher = copy.deepcopy(baseline)
    corrupt_teacher["teacher"]["producer_implementation_identity"] = "ocgforge.policy.teacher_core.corrupt"
    reject(corrupt_teacher, "general wrong Teacher identity")

    wrong_limitation = copy.deepcopy(baseline)
    wrong_limitation["positive_lethal_capability"] = "PROVEN_LETHAL_AVAILABLE"
    reject(wrong_limitation, "wrong positive-lethal limitation")

    sidecar_invalid = copy.deepcopy(baseline)
    sidecar_invalid["task5_metrics"]["sidecar_invalid_count"] = 1
    reject(sidecar_invalid, "sidecar INVALID evidence")

    proven = copy.deepcopy(baseline)
    proven["task5_metrics"]["proven_lethal_count"] = 1
    reject(proven, "PROVEN_LETHAL evidence")

    lower_bound = copy.deepcopy(baseline)
    lower_bound["task5_metrics"]["lower_bound_present_count"] = 1
    reject(lower_bound, "lethal lower bound evidence")

    no_influence = copy.deepcopy(baseline)
    no_influence["task5_metrics"]["sidecar_influences_gameplay"] = "YES"
    reject(no_influence, "sidecar influence")

    no_coverage = copy.deepcopy(baseline)
    no_coverage["task5_metrics"]["battle_coverage"] = "FAIL"
    no_coverage["task5_metrics"]["battle_decision_record_count"] = 0
    no_coverage["task5_metrics"]["battle_command_candidate_count"] = 0
    reject(no_coverage, "zero Battle coverage")

    heavy_pass = copy.deepcopy(baseline)
    heavy_pass["heavy_evidence"]["current_head_heavy_replay"] = "PASS"
    reject(heavy_pass, "current-head Heavy Replay falsely marked PASS")

    invalid_status = copy.deepcopy(baseline)
    invalid_status["status"] = "UNKNOWN"
    reject(invalid_status, "invalid report status")

    with tempfile.TemporaryDirectory() as directory:
        directory_path = Path(directory)
        json_path = directory_path / "report.json"
        markdown_path = directory_path / "report.md"
        json_path.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        markdown_path.write_text(GENERATOR.render_markdown(baseline), encoding="utf-8")
        validate_files(json_path, markdown_path)

        noncanonical_json = directory_path / "noncanonical.json"
        noncanonical_json.write_text(json.dumps(baseline) + "\n", encoding="utf-8")
        try:
            validate_files(noncanonical_json, markdown_path)
        except AssertionError:
            pass
        else:
            raise AssertionError("noncanonical JSON was accepted")

        bad_markdown = directory_path / "bad.md"
        bad_markdown.write_text(markdown_path.read_text(encoding="utf-8") + "\nchanged\n", encoding="utf-8")
        try:
            validate_files(json_path, bad_markdown)
        except AssertionError:
            pass
        else:
            raise AssertionError("inconsistent Markdown was accepted")


def main() -> int:
    run_negative_self_tests()
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    if args.report is None and args.markdown is None:
        print("phase4c_acceptance_test=PASS")
        return 0
    require(args.report is not None and args.markdown is not None,
            "--report and --markdown must be supplied together")
    current_head = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    require(current_head.returncode == 0, "could not resolve current source head")
    expected_source_head = current_head.stdout.decode("utf-8", errors="strict").strip().lower()
    validate_files(args.report, args.markdown, expected_source_head=expected_source_head)
    require(json.loads(args.report.read_text(encoding="utf-8"))["status"] == "PASS",
            "generated acceptance evidence is not overall PASS")
    print("phase4c_acceptance_validation=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
