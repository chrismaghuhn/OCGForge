from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
from pathlib import Path
from typing import Any


SCHEMA = "ocgforge.phase4b_acceptance.v1"
GATE_IDS = [f"P4B-G{index:02d}" for index in range(19)]
STATUSES = {"PASS", "FAIL", "NOT_RUN", "SKIPPED", "BLOCKED"}
SHA256 = re.compile(r"^[0-9a-f]{64}$")
HEAD = re.compile(r"^[0-9a-f]{40}$")
REQUIRED_AUXILIARY_LABELS = [
    "source-head",
    "source-base",
    "dev-configure",
    "dev-build",
    "profile-registry-probe",
    "profile-scenarios-ctest-cardinality",
    "profile-scenarios-ctest",
    "trajectory-short-ctest-cardinality",
    "trajectory-short-ctest",
    "rules-deck-python",
    "repository-python",
    "fixed-matrix-probe",
    "h-exec-diff-check",
    "acceptance-validator-self-test",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_markdown(report: dict[str, Any], markdown: str) -> None:
    require(f"schema_version: {SCHEMA}" in markdown,
            "Markdown schema does not match JSON")
    require(f"status: {report['status']}" in markdown,
            "Markdown status does not match JSON")
    require(f"source_head: {report['source_head']}" in markdown,
            "Markdown source head does not match JSON")
    for gate in report["gates"]:
        marker = f"| {gate['gate']} | {gate['status']} |"
        require(marker in markdown,
                f"Markdown gate status does not match JSON: {gate['gate']}")


def validate_report(report: dict[str, Any],
                    markdown: str | None = None,
                    expected_head: str | None = None) -> None:
    require(report.get("schema_version") == SCHEMA, "invalid acceptance schema")
    require(report.get("status") in STATUSES, "invalid acceptance status")
    source_base = report.get("source_base")
    source_head = report.get("source_head")
    require(isinstance(source_base, str) and HEAD.fullmatch(source_base),
            "invalid source_base")
    require(isinstance(source_head, str) and HEAD.fullmatch(source_head),
            "invalid source_head")
    if expected_head is not None:
        require(source_head == expected_head, "source_head mismatch")

    gates = report.get("gates")
    require(isinstance(gates, list), "gates must be a list")
    actual_ids = [gate.get("gate") for gate in gates]
    require(actual_ids == GATE_IDS, "gate IDs must be exactly P4B-G00 through P4B-G18")

    evidence = report.get("command_evidence")
    require(isinstance(evidence, list), "command_evidence must be a list")
    evidence_by_label: dict[str, dict[str, Any]] = {}
    for record in evidence:
        label = record.get("label")
        require(isinstance(label, str) and label not in evidence_by_label,
                "command evidence labels must be unique")
        require(record.get("status") in STATUSES, f"invalid command status: {label}")
        require(isinstance(record.get("command"), str), f"missing command: {label}")
        require(record.get("stdout_sha256") and
                SHA256.fullmatch(record["stdout_sha256"]),
                f"invalid stdout hash: {label}")
        require(record.get("stderr_sha256") and
                SHA256.fullmatch(record["stderr_sha256"]),
                f"invalid stderr hash: {label}")
        if record.get("status") == "PASS":
            require(record.get("exit_code") == 0,
                    f"PASS command has nonzero exit: {label}")
            expected = record.get("expected_selected_count")
            observed = record.get("observed_selected_count")
            if expected is not None:
                require(observed == expected,
                        f"CTest cardinality mismatch: {label}")
        evidence_by_label[label] = record

    for gate in gates:
        require(gate.get("status") in STATUSES, f"invalid gate status: {gate.get('gate')}")
        labels = gate.get("exact_evidence")
        require(isinstance(labels, list) and labels, f"gate lacks evidence: {gate['gate']}")
        for label in labels:
            require(label in evidence_by_label,
                    f"gate references missing evidence: {gate['gate']}:{label}")
            if gate["status"] == "PASS":
                require(evidence_by_label[label]["status"] == "PASS",
                        f"PASS gate references non-PASS evidence: {gate['gate']}")

    environment = report.get("environment")
    require(isinstance(environment, dict), "missing environment identities")
    for key in ("matchup_id", "rules_bundle_id", "format_id", "duel_mode",
                "duel_flags", "locked_decks"):
        require(key in environment, f"missing environment field: {key}")
    teacher = report.get("teacher")
    require(isinstance(teacher, dict), "missing Teacher identities")
    for key in ("producer_implementation_identity",
                "deterministic_sampling_contract_identity",
                "profiles", "bindings", "policy_artifacts"):
        require(key in teacher, f"missing Teacher field: {key}")
    for key in ("profiles", "bindings", "policy_artifacts"):
        require(len(teacher[key]) == 2, f"Teacher identity count is not two: {key}")

    matrix = report.get("fixed_matchup_matrix")
    require(isinstance(matrix, list) and len(matrix) == 4,
            "fixed matchup matrix must have four rows")
    rows = [(row.get("seat_assignment"), row.get("starting_player")) for row in matrix]
    require(len(set(rows)) == 4, "fixed matchup matrix has duplicate rows")
    require(all(row.get("status") == "PASS" for row in matrix),
            "fixed matchup matrix is not fully passing")

    heavy = report.get("heavy_evidence")
    require(isinstance(heavy, dict) and
            heavy.get("current_head_heavy_replay") == "NOT_RUN",
            "current-head Heavy Replay must remain NOT_RUN")

    if report["status"] == "PASS":
        require(all(gate["status"] == "PASS" for gate in gates),
                "overall PASS requires all gates PASS")
        required_commands = report.get("required_command_evidence")
        require(required_commands == REQUIRED_AUXILIARY_LABELS,
                "required auxiliary command evidence set is incomplete")
        for label in required_commands:
            require(label in evidence_by_label and
                    evidence_by_label[label]["status"] == "PASS",
                    f"required auxiliary command did not pass: {label}")
        require(all(record["status"] == "PASS" for record in evidence),
                "overall PASS cannot contain any failed command evidence")
    if markdown is not None:
        validate_markdown(report, markdown)


def valid_report() -> dict[str, Any]:
    evidence = []
    for index, label in enumerate(REQUIRED_AUXILIARY_LABELS):
        evidence.append({
            "label": label,
            "command": f"command {index}",
            "status": "PASS",
            "exit_code": 0,
            "expected_selected_count": 1,
            "observed_selected_count": 1,
            "stdout_sha256": hashlib.sha256(f"stdout-{index}".encode()).hexdigest(),
            "stderr_sha256": hashlib.sha256(b"").hexdigest(),
        })
    return {
        "schema_version": SCHEMA,
        "status": "PASS",
        "source_base": "0" * 40,
        "source_head": "1" * 40,
        "environment": {
            "matchup_id": "matchup",
            "rules_bundle_id": "rules",
            "format_id": "format",
            "duel_mode": "mode",
            "duel_flags": 190464,
            "locked_decks": ["deck-a", "deck-b"],
        },
        "teacher": {
            "producer_implementation_identity": "producer",
            "deterministic_sampling_contract_identity": "sampling",
            "profiles": ["profile-a", "profile-b"],
            "bindings": ["binding-a", "binding-b"],
            "policy_artifacts": ["artifact-a", "artifact-b"],
        },
        "gates": [
            {"gate": gate_id, "status": "PASS",
             "exact_evidence": [
                 REQUIRED_AUXILIARY_LABELS[index % len(REQUIRED_AUXILIARY_LABELS)]
             ]}
            for index, gate_id in enumerate(GATE_IDS)
        ],
        "command_evidence": evidence,
        "required_command_evidence": REQUIRED_AUXILIARY_LABELS.copy(),
        "fixed_matchup_matrix": [
            {"seat_assignment": seat, "starting_player": player, "status": "PASS"}
            for seat in ("normal", "mirror")
            for player in (0, 1)
        ],
        "heavy_evidence": {"current_head_heavy_replay": "NOT_RUN"},
    }


def run_negative_self_tests() -> None:
    baseline = valid_report()
    validate_report(baseline)
    cases: list[tuple[str, Any]] = []

    missing = copy.deepcopy(baseline)
    missing["gates"] = missing["gates"][:-1]
    cases.append(("missing gate", missing))

    duplicate = copy.deepcopy(baseline)
    duplicate["gates"][1]["gate"] = duplicate["gates"][0]["gate"]
    cases.append(("duplicate gate", duplicate))

    unknown = copy.deepcopy(baseline)
    unknown["gates"][0]["gate"] = "P4B-G99"
    cases.append(("unknown gate", unknown))

    no_evidence = copy.deepcopy(baseline)
    no_evidence["gates"][0]["exact_evidence"] = []
    cases.append(("pass without evidence", no_evidence))

    nonzero = copy.deepcopy(baseline)
    nonzero["command_evidence"][0]["exit_code"] = 1
    cases.append(("nonzero pass", nonzero))

    wrong_count = copy.deepcopy(baseline)
    wrong_count["command_evidence"][0]["observed_selected_count"] = 2
    cases.append(("wrong count", wrong_count))

    unreferenced_fail = copy.deepcopy(baseline)
    unreferenced_fail["command_evidence"].append({
        "label": "unreferenced-fail",
        "command": "unreferenced command",
        "status": "FAIL",
        "exit_code": 1,
        "expected_selected_count": None,
        "observed_selected_count": None,
        "stdout_sha256": hashlib.sha256(b"").hexdigest(),
        "stderr_sha256": hashlib.sha256(b"failure").hexdigest(),
    })
    cases.append(("unreferenced command failure", unreferenced_fail))

    wrong_head = copy.deepcopy(baseline)
    cases.append(("head mismatch", wrong_head))

    missing_identity = copy.deepcopy(baseline)
    del missing_identity["teacher"]["bindings"]
    cases.append(("missing identity", missing_identity))

    missing_row = copy.deepcopy(baseline)
    missing_row["fixed_matchup_matrix"].pop()
    cases.append(("missing matrix row", missing_row))

    duplicate_row = copy.deepcopy(baseline)
    duplicate_row["fixed_matchup_matrix"][1] = duplicate_row["fixed_matchup_matrix"][0]
    cases.append(("duplicate matrix row", duplicate_row))

    heavy_pass = copy.deepcopy(baseline)
    heavy_pass["heavy_evidence"]["current_head_heavy_replay"] = "PASS"
    cases.append(("heavy pass", heavy_pass))

    invalid_status = copy.deepcopy(baseline)
    invalid_status["gates"][0]["status"] = "UNKNOWN"
    cases.append(("invalid status", invalid_status))

    for name, case in cases:
        try:
            validate_report(case, expected_head="f" * 40 if name == "head mismatch" else None)
        except AssertionError:
            continue
        raise AssertionError(f"negative acceptance case was accepted: {name}")

    markdown = "\n".join([
        "schema_version: ocgforge.phase4b_acceptance.v1",
        "status: PASS",
        "source_head: " + baseline["source_head"],
    ] + [f"| {gate['gate']} | {gate['status']} |" for gate in baseline["gates"]])
    validate_report(baseline, markdown)
    bad_markdown = markdown.replace("| P4B-G00 | PASS |", "| P4B-G00 | FAIL |")
    try:
        validate_report(baseline, bad_markdown)
    except AssertionError:
        pass
    else:
        raise AssertionError("inconsistent Markdown was accepted")


def validate_files(json_path: Path, markdown_path: Path) -> None:
    report = json.loads(json_path.read_text(encoding="utf-8"))
    validate_report(report, markdown_path.read_text(encoding="utf-8"))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    run_negative_self_tests()
    if args.report is not None or args.markdown is not None:
        require(args.report is not None and args.markdown is not None,
                "--report and --markdown must be supplied together")
        validate_files(args.report, args.markdown)
        print("phase4b_acceptance_validation=PASS")
    else:
        print("phase4b_acceptance_test=PASS")


if __name__ == "__main__":
    main()
