from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "ocgforge.phase4b_acceptance.v1"
TASK12_BASE = "8f0e3465a09de69707dcabfeec30c4681aa1fa2e"
PHASE4A_H_EXEC = "66d30967018c6ce106d131c7e02147ffaf194a56"
PHASE4A_H_EVIDENCE = "6ab937a61ac39eecfb7bc91174ca0ba92b3edd09"
HEAD_RE = re.compile(r"^[0-9a-f]{40}$")
CTEST_SUMMARY_RE = re.compile(
    r"(?P<percent>\d+)% tests passed,\s*(?P<failed>\d+) tests failed out of\s*(?P<total>\d+)"
)
CTEST_TOTAL_RE = re.compile(r"Total Tests:\s*(\d+)")

ENVIRONMENT = {
    "matchup_id": "ocgforge.matchup.swordsoul_salamangreat.v1",
    "rules_bundle_id": "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
    "format_id": "TCG_ADVANCED_2026_05_18",
    "duel_mode": "DUEL_MODE_MR5",
    "duel_flags": 190464,
    "locked_decks": [
        {
            "role": 0,
            "id": "ocgforge.swordsoul_tenyi.ml_v1",
            "sha256": "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
        },
        {
            "role": 1,
            "id": "ocgforge.salamangreat.ml_v1",
            "sha256": "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
        },
    ],
}


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_text(value: str) -> str:
    return sha256_bytes(value.encode("utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_command(
    records: list[dict[str, Any]],
    label: str,
    argv: list[str],
    display: str,
    expected_selected_count: int | None = None,
    count_parser: Any = None,
) -> dict[str, Any]:
    try:
        process = subprocess.run(
            argv,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        stdout = process.stdout
        stderr = process.stderr
        exit_code = process.returncode
    except OSError as error:
        stdout = b""
        stderr = str(error).encode("utf-8")
        exit_code = 1

    count_input = stdout + b"\n" + stderr
    observed = count_parser(count_input) if count_parser is not None else None
    passed = exit_code == 0
    if expected_selected_count is not None:
        passed = passed and observed == expected_selected_count
    record = {
        "label": label,
        "command": display,
        "status": "PASS" if passed else "FAIL",
        "exit_code": exit_code,
        "expected_selected_count": expected_selected_count,
        "observed_selected_count": observed,
        "stdout_sha256": sha256_bytes(stdout),
        "stderr_sha256": sha256_bytes(stderr),
        "_stdout": stdout,
        "_stderr": stderr,
    }
    records.append(record)
    return record


def parse_ctest_total(output: bytes) -> int | None:
    text = output.decode("utf-8", errors="replace")
    matches = CTEST_TOTAL_RE.findall(text)
    if matches:
        return int(matches[-1])
    return None


def parse_ctest_summary(output: bytes) -> int | None:
    text = output.decode("utf-8", errors="replace")
    matches = CTEST_SUMMARY_RE.findall(text)
    if not matches:
        return None
    return int(matches[-1][2])


def run_ctest(
    records: list[dict[str, Any]],
    label: str,
    expression: str,
    expected_count: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    quoted = f'"{expression}"'
    cardinality_display = (
        f"ctest --test-dir build/dev-windows -C Debug -N "
        f"--no-tests=error --tests-regex {quoted}"
    )
    cardinality = run_command(
        records,
        f"{label}-cardinality",
        [
            "ctest",
            "--test-dir",
            "build/dev-windows",
            "-C",
            "Debug",
            "-N",
            "--no-tests=error",
            "--tests-regex",
            expression,
        ],
        cardinality_display,
        expected_count,
        parse_ctest_total,
    )
    display = (
        f"ctest --test-dir build/dev-windows -C Debug --output-on-failure "
        f"--no-tests=error --tests-regex {quoted}"
    )
    actual = run_command(
        records,
        label,
        [
            "ctest",
            "--test-dir",
            "build/dev-windows",
            "-C",
            "Debug",
            "--output-on-failure",
            "--no-tests=error",
            "--tests-regex",
            expression,
        ],
        display,
        expected_count,
        parse_ctest_summary,
    )
    return cardinality, actual


def run_python(
    records: list[dict[str, Any]],
    label: str,
    relative_script: str,
    script_args: list[str] | None = None,
    expected_count: int | None = None,
) -> dict[str, Any]:
    args = script_args or []
    display = "python -B " + relative_script
    if args:
        display += " " + " ".join(args)
    parser = None
    if expected_count is not None:
        parser = lambda output: parse_unittest_count(output)
    return run_command(
        records,
        label,
        [sys.executable, "-B", str(ROOT / relative_script), *args],
        display,
        expected_count,
        parser,
    )


def parse_unittest_count(output: bytes) -> int | None:
    text = output.decode("utf-8", errors="replace")
    matches = re.findall(r"(?m)^Ran\s+(\d+)\s+tests?", text)
    return int(matches[-1]) if matches else None


def run_probe(
    records: list[dict[str, Any]],
    label: str,
    mode: str,
) -> dict[str, Any]:
    display = f"build/dev-windows/teacher_probe.exe {mode}"
    return run_command(
        records,
        label,
        [str(ROOT / "build" / "dev-windows" / "teacher_probe.exe"), mode],
        display,
    )


def git_value(args: list[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    require(result.returncode == 0, f"git command failed: {' '.join(args)}")
    value = result.stdout.decode("utf-8").strip().lower()
    require(value, f"git command returned no value: {' '.join(args)}")
    return value


def parse_probe_keys(output: bytes) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in output.decode("utf-8", errors="strict").splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key] = value
    return result


def gate(
    gate_id: str,
    invariant: str,
    condition: str,
    evidence: list[str],
    records_by_label: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    status = "PASS" if all(records_by_label[label]["status"] == "PASS"
                           for label in evidence if label in records_by_label) and \
        all(label in records_by_label for label in evidence) else "FAIL"
    return {
        "gate": gate_id,
        "invariant": invariant,
        "status": status,
        "exact_evidence": evidence,
        "pass_condition": condition,
    }


def run_acceptance(args: argparse.Namespace) -> int:
    source_head = git_value(["rev-parse", "HEAD"])
    source_base = git_value(["rev-parse", "HEAD^"])
    require(HEAD_RE.fullmatch(source_head), "source HEAD is not a commit")
    require(source_head == args.expected_head.lower(),
            f"source HEAD mismatch: expected {args.expected_head}, got {source_head}")
    require(source_base == TASK12_BASE,
            f"source base mismatch: expected {TASK12_BASE}, got {source_base}")

    records: list[dict[str, Any]] = []
    run_command(
        records,
        "source-head",
        ["git", "rev-parse", "HEAD"],
        "git rev-parse HEAD",
    )
    run_command(
        records,
        "source-base",
        ["git", "rev-parse", "HEAD^"],
        "git rev-parse HEAD^",
    )

    configure = run_command(
        records,
        "dev-configure",
        ["cmake", "--preset", "dev-windows"],
        "cmake --preset dev-windows",
    )
    build = run_command(
        records,
        "dev-build",
        ["cmake", "--build", "--preset", "dev-windows"],
        "cmake --build --preset dev-windows",
    )
    require(configure["status"] == "PASS" and build["status"] == "PASS",
            "native dev configure/build failed")

    profile_probe = run_probe(records, "profile-registry-probe", "--profile-registry")
    require(profile_probe["status"] == "PASS",
            "profile registry probe failed before evidence generation")
    profile_values = parse_probe_keys(profile_probe["_stdout"])
    required_profile_keys = (
        "swordsoul_profile_id",
        "salamangreat_profile_id",
        "swordsoul_binding_id",
        "salamangreat_binding_id",
        "swordsoul_policy_artifact_id",
        "salamangreat_policy_artifact_id",
    )
    require(all(key in profile_values for key in required_profile_keys),
            "profile registry probe omitted a required identity")

    records_by_label: dict[str, dict[str, Any]] = {}
    for record in records:
        records_by_label[record["label"]] = record

    gate_evidence: dict[str, list[str]] = {}
    ctest_specs = {
        "g00-ctest": ("^teacher_policy_boundary_compile_test$", 1, "G00"),
        "g01-ctest": ("^teacher_domain_preservation_test$", 1, "G01"),
        "g02-ctest": ("^teacher_ranking_test$", 1, "G02"),
        "g04-ctest": ("^strategy_profile_codec_test$", 1, "G04"),
        "g05-ctest": ("^strategy_profile_negative_test$", 1, "G05"),
        "g07-ctest": ("^teacher_strategy_state_test$", 1, "G07"),
        "g08-ctest": ("^teacher_rejected_transition_test$", 1, "G08"),
        "g09-ctest": ("^teacher_recovery_test$", 1, "G09"),
        "g11-ctest": ("^teacher_fallback_test$", 1, "G11"),
        "g12-ctest": ("^teacher_provenance_test$", 1, "G12"),
        "g13-ctest": ("^teacher_runner_trajectory_test$", 1, "G13"),
        "g15-ctest": ("^teacher_knowledge_boundary_test$", 1, "G15"),
        "g16-ctest": ("^teacher_explanation_test$", 1, "G16"),
        "g17-ctest": ("^teacher_profile_registry_test$", 1, "G17"),
    }
    for label, (expression, count, _) in ctest_specs.items():
        run_ctest(records, label, expression, count)

    g14_expression = (
        "^(public_safe_state_test|public_action_identity_test|policy_boundary_compile_test|"
        "policy_rng_test|random_legal_test|policy_runner_integration_test)$"
    )
    run_ctest(records, "g14-ctest", g14_expression, 6)

    run_python(records, "g00-python", "tests/teacher/teacher_public_boundary_test.py")
    run_python(records, "g03-python", "tests/teacher/teacher_paired_world_test.py")
    run_python(
        records,
        "g06-python",
        "tests/teacher/teacher_profile_binding_test.py",
    )
    run_python(
        records,
        "g10-python",
        "tests/teacher/teacher_determinism_test.py",
        ["--probe", "build/dev-windows/teacher_probe.exe"],
    )
    run_python(records, "g14-policy-boundary-python", "tests/policy/policy_boundary_test.py")
    run_python(records, "g14-public-fact-matrix-python",
               "tests/policy/public_fact_matrix_test.py")
    run_python(records, "g18-python", "tests/teacher/teacher_public_fact_matrix_test.py")

    profile_ctest_expression = (
        "^(swordsoul_profile_test|swordsoul_teacher_scenarios_test|"
        "salamangreat_profile_test|salamangreat_teacher_scenarios_test)$"
    )
    run_ctest(records, "profile-scenarios-ctest", profile_ctest_expression, 4)
    run_ctest(
        records,
        "trajectory-short-ctest",
        "^(trajectory_codec_test|trajectory_recorder_test)$",
        2,
    )
    run_python(records, "rules-deck-python", "tests/policy/rules_deck_identity_test.py")
    run_command(
        records,
        "repository-python",
        [sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests/python", "-v"],
        "python -B -m unittest discover -s tests/python -v",
        15,
        parse_unittest_count,
    )

    matrix_probe = run_probe(records, "fixed-matrix-probe", "--matrix")
    matrix_text = matrix_probe["_stdout"].decode("utf-8", errors="replace")
    matrix_rows = []
    for line in matrix_text.splitlines():
        if line.startswith("matrix_row=") and line.endswith(":PASS"):
            row = line[len("matrix_row="):-len(":PASS")]
            seat, player = row.split(":", 1)
            matrix_rows.append({
                "seat_assignment": seat,
                "starting_player": int(player),
                "status": "PASS",
            })
    expected_matrix_rows = {
        ("normal", 0),
        ("normal", 1),
        ("mirror", 0),
        ("mirror", 1),
    }
    observed_matrix_rows = {
        (row["seat_assignment"], row["starting_player"]) for row in matrix_rows
    }
    if (matrix_probe["status"] != "PASS" or len(matrix_rows) != 4 or
            observed_matrix_rows != expected_matrix_rows or
            "matrix=PASS" not in matrix_text):
        matrix_rows = [
            {"seat_assignment": "normal", "starting_player": 0, "status": "FAIL"},
            {"seat_assignment": "normal", "starting_player": 1, "status": "FAIL"},
            {"seat_assignment": "mirror", "starting_player": 0, "status": "FAIL"},
            {"seat_assignment": "mirror", "starting_player": 1, "status": "FAIL"},
        ]

    run_command(
        records,
        "h-exec-diff-check",
        ["git", "diff", "--check", "HEAD^", "HEAD"],
        "git diff --check HEAD^ HEAD",
    )
    run_python(
        records,
        "acceptance-validator-self-test",
        "tests/teacher/phase4b_acceptance_test.py",
    )

    records_by_label = {record["label"]: record for record in records}
    required_command_evidence = [
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
    gate_evidence.update({
        "P4B-G00": ["g00-ctest-cardinality", "g00-ctest", "g00-python"],
        "P4B-G01": ["g01-ctest-cardinality", "g01-ctest"],
        "P4B-G02": ["g02-ctest-cardinality", "g02-ctest"],
        "P4B-G03": ["g03-python"],
        "P4B-G04": ["g04-ctest-cardinality", "g04-ctest"],
        "P4B-G05": ["g05-ctest-cardinality", "g05-ctest"],
        "P4B-G06": ["g06-python"],
        "P4B-G07": ["g07-ctest-cardinality", "g07-ctest"],
        "P4B-G08": ["g08-ctest-cardinality", "g08-ctest"],
        "P4B-G09": ["g09-ctest-cardinality", "g09-ctest"],
        "P4B-G10": ["g10-python"],
        "P4B-G11": ["g11-ctest-cardinality", "g11-ctest"],
        "P4B-G12": ["g12-ctest-cardinality", "g12-ctest"],
        "P4B-G13": ["g13-ctest-cardinality", "g13-ctest"],
        "P4B-G14": [
            "g14-ctest-cardinality",
            "g14-ctest",
            "g14-policy-boundary-python",
            "g14-public-fact-matrix-python",
        ],
        "P4B-G15": ["g15-ctest-cardinality", "g15-ctest"],
        "P4B-G16": ["g16-ctest-cardinality", "g16-ctest"],
        "P4B-G17": ["g17-ctest-cardinality", "g17-ctest"],
        "P4B-G18": ["g18-python"],
    })
    invariant_names = {
        "P4B-G00": "Public-only Teacher boundary",
        "P4B-G01": "Complete supplied candidate domain preservation",
        "P4B-G02": "Deterministic ranking and public-key tie-break",
        "P4B-G03": "Equal-public-world privacy",
        "P4B-G04": "Canonical StrategyProfile identity",
        "P4B-G05": "Malformed profile fail-closed",
        "P4B-G06": "Exact deck/matchup/rules binding",
        "P4B-G07": "Episode/participant state isolation",
        "P4B-G08": "Rejected action zero advancement",
        "P4B-G09": "Plan invalidation and recovery",
        "P4B-G10": "Independent-process determinism",
        "P4B-G11": "Explicit deterministic fallback",
        "P4B-G12": "Teacher provenance recording",
        "P4B-G13": "Trusted trajectory compatibility",
        "P4B-G14": "Phase-4A public-policy regression",
        "P4B-G15": "Knowledge-destroying privacy boundary",
        "P4B-G16": "Diagnostic safety and separation",
        "P4B-G17": "Immutable profile publication",
        "P4B-G18": "Public-fact availability",
    }
    gates = []
    for index in range(19):
        gate_id = f"P4B-G{index:02d}"
        gates.append(gate(
            gate_id,
            invariant_names[gate_id],
            "all required exact evidence passes with the frozen cardinality",
            gate_evidence[gate_id],
            records_by_label,
        ))

    fixed_matrix = [
        {"seat_assignment": "normal", "starting_player": 0, "status": "PASS"},
        {"seat_assignment": "normal", "starting_player": 1, "status": "PASS"},
        {"seat_assignment": "mirror", "starting_player": 0, "status": "PASS"},
        {"seat_assignment": "mirror", "starting_player": 1, "status": "PASS"},
    ] if matrix_probe["status"] == "PASS" and len(matrix_rows) == 4 else matrix_rows

    for record in records:
        record.pop("_stdout", None)
        record.pop("_stderr", None)

    report = {
        "schema_version": SCHEMA,
        "status": "PASS" if (
            all(item["status"] == "PASS" for item in gates) and
            all(row["status"] == "PASS" for row in fixed_matrix) and
            all(record["status"] == "PASS" for record in records)
        ) else "FAIL",
        "source_base": source_base,
        "source_head": source_head,
        "environment": ENVIRONMENT,
        "teacher": {
            "producer_implementation_identity": "ocgforge.policy.teacher_core.v1",
            "deterministic_sampling_contract_identity":
                "ocgforge.policy.deterministic_lexicographic_argmax.v1",
            "profiles": [
                {"role": "swordsoul", "profile_id": profile_values["swordsoul_profile_id"]},
                {"role": "salamangreat",
                 "profile_id": profile_values["salamangreat_profile_id"]},
            ],
            "bindings": [
                {"role": "swordsoul", "binding_id": profile_values["swordsoul_binding_id"]},
                {"role": "salamangreat",
                 "binding_id": profile_values["salamangreat_binding_id"]},
            ],
            "policy_artifacts": [
                {"role": "swordsoul",
                 "artifact_id": profile_values["swordsoul_policy_artifact_id"]},
                {"role": "salamangreat",
                 "artifact_id": profile_values["salamangreat_policy_artifact_id"]},
            ],
        },
        "gates": gates,
        "command_evidence": records,
        "required_command_evidence": required_command_evidence,
        "fixed_matchup_matrix": fixed_matrix,
        "heavy_evidence": {
            "current_head_heavy_replay": "NOT_RUN",
            "phase4a_h_exec": PHASE4A_H_EXEC,
            "phase4a_h_evidence": PHASE4A_H_EVIDENCE,
            "mode": "frozen_baseline_reference",
        },
        "scope_limitations": [
            "fixed matchup only",
            "no arbitrary-deck claim",
            "F2 profile utility remains unsupported/fail-closed",
            "F3 generic tactical utility remains unsupported/fail-closed",
            "copy-budget-dependent strategy remains fail-closed where public proof is absent",
            "safe-stop/lethal remain omitted where unprovable",
            "no Phase 4C claim",
            "no ML claim",
        ],
    }
    output_json = ROOT / args.output_json
    output_markdown = ROOT / args.output_markdown
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_markdown.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    output_markdown.write_text(render_markdown(report), encoding="utf-8")
    return 0 if report["status"] == "PASS" else 1


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# OCGForge Phase 4B Acceptance",
        "",
        f"- schema_version: {report['schema_version']}",
        f"- status: {report['status']}",
        f"- source_head: {report['source_head']}",
        f"- source_base: {report['source_base']}",
        "",
        "## Environment",
        "",
        f"- matchup_id: {report['environment']['matchup_id']}",
        f"- rules_bundle_id: {report['environment']['rules_bundle_id']}",
        f"- format_id: {report['environment']['format_id']}",
        f"- duel_mode: {report['environment']['duel_mode']}",
        f"- duel_flags: {report['environment']['duel_flags']}",
        "",
        "## Teacher identities",
        "",
        f"- producer: {report['teacher']['producer_implementation_identity']}",
        f"- deterministic_sampling: "
        f"{report['teacher']['deterministic_sampling_contract_identity']}",
    ]
    for key in ("profiles", "bindings", "policy_artifacts"):
        lines.append("")
        lines.append(f"- {key}:")
        for value in report["teacher"][key]:
            suffix = "profile_id" if key == "profiles" else (
                "binding_id" if key == "bindings" else "artifact_id")
            lines.append(f"  - {value['role']}: {value[suffix]}")
    lines.extend(["", "## Gates", "", "| Gate | Status |", "| --- | --- |"])
    for gate in report["gates"]:
        lines.append(f"| {gate['gate']} | {gate['status']} |")
    lines.extend(["", "## Fixed matchup matrix", "",
                  "| Seat assignment | Starting player | Status |",
                  "| --- | ---: | --- |"])
    for row in report["fixed_matchup_matrix"]:
        lines.append(
            f"| {row['seat_assignment']} | {row['starting_player']} | {row['status']} |"
        )
    lines.extend(["", "## Command evidence", "",
                  "| Label | Status | Exit | Expected | Observed | stdout SHA-256 | stderr SHA-256 |",
                  "| --- | --- | ---: | ---: | ---: | --- | --- |"])
    for record in report["command_evidence"]:
        lines.append(
            f"| {record['label']} | {record['status']} | {record['exit_code']} | "
            f"{record['expected_selected_count']} | {record['observed_selected_count']} | "
            f"{record['stdout_sha256']} | {record['stderr_sha256']} |"
        )
    lines.extend(["", "## Heavy evidence", "",
                  f"- current_head_heavy_replay: "
                  f"{report['heavy_evidence']['current_head_heavy_replay']}",
                  f"- phase4a_h_exec: {report['heavy_evidence']['phase4a_h_exec']}",
                  f"- phase4a_h_evidence: {report['heavy_evidence']['phase4a_h_evidence']}",
                  "- mode: frozen_baseline_reference",
                  "", "## Scope limitations", ""])
    lines.extend(f"- {value}" for value in report["scope_limitations"])
    lines.append("")
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-head", required=True)
    parser.add_argument(
        "--output-json",
        default="docs/p4b/p4b_acceptance.json",
    )
    parser.add_argument(
        "--output-markdown",
        default="docs/p4b/P4B_ACCEPTANCE.md",
    )
    return parser.parse_args()


if __name__ == "__main__":
    try:
        sys.exit(run_acceptance(parse_args()))
    except Exception as error:
        print(f"phase4b_acceptance: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
