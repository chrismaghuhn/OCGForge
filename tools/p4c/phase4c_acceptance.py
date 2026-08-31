from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[2]
SCHEMA = "ocgforge.phase4c_acceptance.v1"
TASK5_BASE = "cf5786c8c0b08140b997e6df2fa397cc41538020"
PHASE4A_H_EXEC = "66d30967018c6ce106d131c7e02147ffaf194a56"
PHASE4A_H_EVIDENCE = "6ab937a61ac39eecfb7bc91174ca0ba92b3edd09"
SNAPSHOT_SCHEMA = "ocgforge.public_battle_snapshot.v1"
LETHAL_SCHEMA = "ocgforge.provable_lethal.v1"
INTEGRATION_DECISION = "TEACHER_V1_PLUS_EVALUATION_SIDECAR"
POSITIVE_LETHAL_CAPABILITY = "BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT"
TEACHER_PRODUCER = "ocgforge.policy.teacher_core.v1"
SAMPLING_CONTRACT = "ocgforge.policy.deterministic_lexicographic_argmax.v1"
ROWS = (("normal", 0), ("normal", 1), ("mirror", 0), ("mirror", 1))

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

TEACHER = {
    "producer_implementation_identity": TEACHER_PRODUCER,
    "deterministic_sampling_contract_identity": SAMPLING_CONTRACT,
    "profiles": [
        {
            "role": "swordsoul",
            "profile_id": "ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74",
        },
        {
            "role": "salamangreat",
            "profile_id": "ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55",
        },
    ],
    "bindings": [
        {
            "role": "swordsoul",
            "binding_id": "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c",
        },
        {
            "role": "salamangreat",
            "binding_id": "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56",
        },
    ],
    "policy_artifacts": [
        {
            "role": "swordsoul",
            "artifact_id": "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d",
        },
        {
            "role": "salamangreat",
            "artifact_id": "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527",
        },
    ],
}

GATE_IDS = [f"P4C-G{index:02d}" for index in range(15)]
STATUSES = {"PASS", "FAIL", "NOT_RUN", "SKIPPED", "BLOCKED"}
SHA256 = re.compile(r"^[0-9a-f]{64}$")
HEAD = re.compile(r"^[0-9a-f]{40}$")

REQUIRED_COMMAND_LABELS = [
    "source-head",
    "source-base",
    "dev-configure",
    "dev-build",
    "g00-public-boundary-ctest-cardinality",
    "g00-public-boundary-ctest",
    "g00-public-boundary-python",
    "g00-lethal-boundary-ctest-cardinality",
    "g00-lethal-boundary-ctest",
    "g01-03-snapshot-ctest-cardinality",
    "g01-03-snapshot-ctest",
    "g04-battle-shape-ctest-cardinality",
    "g04-battle-shape-ctest",
    "g05-battle-determinism-python",
    "g06-battle-paired-world-python",
    "g07-10-lethal-ctest-cardinality",
    "g07-10-lethal-ctest",
    "g11-identity-ctest-cardinality",
    "g11-identity-ctest",
    "g12-trajectory-ctest-cardinality",
    "g12-trajectory-ctest",
    "g13-rules-python",
    "g14-teacher-regression-ctest-cardinality",
    "g14-teacher-regression-ctest",
    "task5-trajectory-ctest-cardinality",
    "task5-trajectory-ctest",
    "task5-evaluation-selftest",
    "task5-evaluation-orchestrator",
    "fixed-row-normal-0",
    "fixed-row-normal-1",
    "fixed-row-mirror-0",
    "fixed-row-mirror-1",
    "phase4b-full-teacher-ctest-cardinality",
    "phase4b-full-teacher-ctest",
    "phase4b-public-boundary-python",
    "phase4b-paired-world-python",
    "phase4b-profile-binding-python",
    "phase4b-determinism-python",
    "policy-boundary-python",
    "public-fact-matrix-python",
    "trajectory-short-ctest-cardinality",
    "trajectory-short-ctest",
    "repository-python",
    "h-exec-diff-check",
    "acceptance-validator-self-test",
]


def load_task5_evaluation_module() -> Any:
    path = ROOT / "tools" / "p4c" / "phase4c_teacher_evaluation.py"
    spec = importlib.util.spec_from_file_location("phase4c_teacher_evaluation", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load the accepted Task-5 row validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def parse_ctest_total(value: bytes) -> int | None:
    text = value.decode("utf-8", errors="replace")
    matches = re.findall(r"(?m)^\s*Total Tests:\s*(\d+)\s*$", text)
    return int(matches[-1]) if matches else None


def parse_ctest_summary(value: bytes) -> tuple[int, int] | None:
    text = value.decode("utf-8", errors="replace")
    matches = re.findall(
        r"(?m)(\d+)% tests passed,\s*(\d+) tests failed out of\s*(\d+)",
        text,
    )
    if not matches:
        return None
    passed, failed, total = (int(item) for item in matches[-1])
    del passed
    return total, failed


def parse_unittest_count(value: bytes) -> int | None:
    text = value.decode("utf-8", errors="replace")
    matches = re.findall(r"(?m)^Ran\s+(\d+)\s+tests?", text)
    return int(matches[-1]) if matches else None


def run_command(
    records: list[dict[str, Any]],
    label: str,
    argv: list[str],
    display: str,
    expected_selected_count: int | None = None,
    count_parser: Callable[[bytes], Any] | None = None,
    require_zero_ctest_failures: bool = False,
    post_validate: Callable[[bytes, bytes], str | None] | None = None,
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
    parsed_count = count_parser(count_input) if count_parser is not None else None
    observed = parsed_count[0] if isinstance(parsed_count, tuple) else parsed_count
    ctest_failures = parsed_count[1] if isinstance(parsed_count, tuple) else None
    passed = exit_code == 0
    if expected_selected_count is not None:
        passed = passed and observed == expected_selected_count
    if require_zero_ctest_failures:
        passed = passed and ctest_failures == 0

    validation_error: str | None = None
    if passed and post_validate is not None:
        try:
            validation_error = post_validate(stdout, stderr)
        except Exception as error:  # pragma: no cover - defensive harness boundary
            validation_error = str(error)
        passed = validation_error is None

    record: dict[str, Any] = {
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
    if validation_error is not None:
        record["_validation_error"] = validation_error
    records.append(record)
    return record


def run_ctest(
    records: list[dict[str, Any]],
    label: str,
    expression: str,
    expected_count: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    quoted = f'"{expression}"'
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
        f"ctest --test-dir build/dev-windows -C Debug -N --no-tests=error --tests-regex {quoted}",
        expected_count,
        parse_ctest_total,
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
        f"ctest --test-dir build/dev-windows -C Debug --output-on-failure --no-tests=error --tests-regex {quoted}",
        expected_count,
        parse_ctest_summary,
        require_zero_ctest_failures=True,
    )
    return cardinality, actual


def run_python(
    records: list[dict[str, Any]],
    label: str,
    relative_script: str,
    script_args: list[str] | None = None,
    expected_count: int | None = None,
    post_validate: Callable[[bytes, bytes], str | None] | None = None,
) -> dict[str, Any]:
    args = script_args or []
    display = "python -B " + relative_script
    if args:
        display += " " + " ".join(args)
    return run_command(
        records,
        label,
        [sys.executable, "-B", str(ROOT / relative_script), *args],
        display,
        expected_count,
        parse_unittest_count if expected_count is not None else None,
        post_validate=post_validate,
    )


def run_probe(
    records: list[dict[str, Any]],
    label: str,
    mode: str,
    post_validate: Callable[[bytes, bytes], str | None] | None = None,
) -> dict[str, Any]:
    display = f"build/dev-windows/phase4c_teacher_probe.exe {mode}"
    return run_command(
        records,
        label,
        [str(ROOT / "build" / "dev-windows" / "phase4c_teacher_probe.exe"), mode],
        display,
        post_validate=post_validate,
    )


def run_teacher_row_probe(
    records: list[dict[str, Any]],
    label: str,
    row: str,
    starting_player: int,
    post_validate: Callable[[bytes, bytes], str | None],
) -> dict[str, Any]:
    display = (
        "build/dev-windows/phase4c_teacher_probe.exe"
        f" --row {row} {starting_player}"
    )
    return run_command(
        records,
        label,
        [
            str(ROOT / "build" / "dev-windows" / "phase4c_teacher_probe.exe"),
            "--row",
            row,
            str(starting_player),
        ],
        display,
        post_validate=post_validate,
    )


def gate(
    gate_id: str,
    invariant: str,
    evidence: list[str],
    records_by_label: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    status = "PASS" if evidence and all(
        label in records_by_label and records_by_label[label]["status"] == "PASS"
        for label in evidence
    ) else "FAIL"
    return {
        "gate": gate_id,
        "invariant": invariant,
        "status": status,
        "exact_evidence": evidence,
        "pass_condition": "all exact evidence records pass with the frozen cardinality",
    }


GATE_EVIDENCE = {
    "P4C-G00": [
        "g00-public-boundary-ctest-cardinality",
        "g00-public-boundary-ctest",
        "g00-public-boundary-python",
        "g00-lethal-boundary-ctest-cardinality",
        "g00-lethal-boundary-ctest",
    ],
    "P4C-G01": ["g01-03-snapshot-ctest-cardinality", "g01-03-snapshot-ctest"],
    "P4C-G02": ["g01-03-snapshot-ctest-cardinality", "g01-03-snapshot-ctest"],
    "P4C-G03": ["g01-03-snapshot-ctest-cardinality", "g01-03-snapshot-ctest"],
    "P4C-G04": ["g04-battle-shape-ctest-cardinality", "g04-battle-shape-ctest"],
    "P4C-G05": ["g05-battle-determinism-python"],
    "P4C-G06": ["g06-battle-paired-world-python"],
    "P4C-G07": ["g07-10-lethal-ctest-cardinality", "g07-10-lethal-ctest"],
    "P4C-G08": ["g07-10-lethal-ctest-cardinality", "g07-10-lethal-ctest"],
    "P4C-G09": ["g07-10-lethal-ctest-cardinality", "g07-10-lethal-ctest"],
    "P4C-G10": ["g07-10-lethal-ctest-cardinality", "g07-10-lethal-ctest"],
    "P4C-G11": ["g11-identity-ctest-cardinality", "g11-identity-ctest"],
    "P4C-G12": ["g12-trajectory-ctest-cardinality", "g12-trajectory-ctest"],
    "P4C-G13": ["g13-rules-python"],
    "P4C-G14": [
        "g14-teacher-regression-ctest-cardinality",
        "g14-teacher-regression-ctest",
    ],
}

GATE_INVARIANTS = {
    "P4C-G00": "public-only Battle and Lethal boundary",
    "P4C-G01": "exact N-candidate preservation",
    "P4C-G02": "visible current ATK/DEF extraction",
    "P4C-G03": "redacted and absent stats fail closed",
    "P4C-G04": "exact BattleCommand public shape",
    "P4C-G05": "independent-process Battle/Lethal determinism",
    "P4C-G06": "equal-public-world Battle/Lethal privacy",
    "P4C-G07": "checked integer and canonical lethal behavior",
    "P4C-G08": "no optimistic lethal false positives",
    "P4C-G09": "missing response/effect proof fails closed",
    "P4C-G10": "no future-action queue or search",
    "P4C-G11": "Phase-4B Teacher v1 identity immutability",
    "P4C-G12": "trajectory and replay compatibility",
    "P4C-G13": "locked deck/rules identity",
    "P4C-G14": "Phase-4B Teacher regression",
}


def validate_source(stdout: bytes, expected: str, label: str) -> str | None:
    value = stdout.decode("utf-8", errors="strict").strip().lower()
    return None if value == expected else f"{label} mismatch: {value}"


def validate_marker_output(
    required_markers: tuple[str, ...], stdout: bytes, _stderr: bytes
) -> str | None:
    text = stdout.decode("utf-8", errors="strict")
    missing = [marker for marker in required_markers if marker not in text.splitlines()]
    return "missing output marker(s): " + ", ".join(missing) if missing else None


def task5_row_validator(
    row: str, starting_player: int
) -> Callable[[bytes, bytes], str | None]:
    def validate(stdout: bytes, _stderr: bytes) -> str | None:
        try:
            module = load_task5_evaluation_module()
            values = module.parse_output(stdout)
            module.validate_row(values, row, starting_player)
        except Exception as error:
            return str(error)
        return None

    return validate


def parse_validated_row(stdout: bytes, row: str, starting_player: int) -> dict[str, str] | None:
    try:
        module = load_task5_evaluation_module()
        values = module.parse_output(stdout)
        module.validate_row(values, row, starting_player)
        return values
    except Exception:
        return None


def run_acceptance(args: argparse.Namespace) -> int:
    expected_head = args.expected_head.lower()
    if HEAD.fullmatch(expected_head) is None:
        raise RuntimeError("expected H_EXEC is not a full commit SHA")

    records: list[dict[str, Any]] = []
    source_head = run_command(
        records,
        "source-head",
        ["git", "rev-parse", "HEAD"],
        "git rev-parse HEAD",
        post_validate=lambda stdout, _stderr: validate_source(
            stdout, expected_head, "source HEAD"
        ),
    )
    source_base = run_command(
        records,
        "source-base",
        ["git", "rev-parse", "HEAD^"],
        "git rev-parse HEAD^",
        post_validate=lambda stdout, _stderr: validate_source(
            stdout, TASK5_BASE, "source base"
        ),
    )
    actual_source_head = source_head["_stdout"].decode("utf-8", errors="replace").strip().lower()
    actual_source_base = source_base["_stdout"].decode("utf-8", errors="replace").strip().lower()

    run_command(
        records,
        "dev-configure",
        ["cmake", "--preset", "dev-windows"],
        "cmake --preset dev-windows",
    )
    run_command(
        records,
        "dev-build",
        ["cmake", "--build", "--preset", "dev-windows"],
        "cmake --build --preset dev-windows",
    )

    run_ctest(records, "g00-public-boundary-ctest", "^public_battle_boundary_compile_test$", 1)
    run_python(records, "g00-public-boundary-python", "tests/teacher/phase4c_public_boundary_test.py")
    run_ctest(records, "g00-lethal-boundary-ctest", "^provable_lethal_boundary_compile_test$", 1)
    run_ctest(
        records,
        "g01-03-snapshot-ctest",
        "^public_battle_snapshot_test$",
        1,
    )
    run_ctest(records, "g04-battle-shape-ctest", "^battle_command_shape_test$", 1)
    run_python(
        records,
        "g05-battle-determinism-python",
        "tests/teacher/phase4c_battle_determinism_test.py",
        ["--probe", "build/dev-windows/phase4c_battle_probe.exe"],
    )
    run_python(
        records,
        "g06-battle-paired-world-python",
        "tests/teacher/phase4c_battle_paired_world_test.py",
        ["--probe", "build/dev-windows/phase4c_battle_probe.exe"],
    )
    run_ctest(
        records,
        "g07-10-lethal-ctest",
        "^provable_lethal_test$",
        1,
    )
    run_ctest(
        records,
        "g11-identity-ctest",
        "^phase4b_teacher_identity_regression_test$",
        1,
    )
    run_ctest(
        records,
        "g12-trajectory-ctest",
        "^(trajectory_codec_test|trajectory_recorder_test)$",
        2,
    )
    run_python(records, "g13-rules-python", "tests/policy/rules_deck_identity_test.py")
    run_ctest(
        records,
        "g14-teacher-regression-ctest",
        "^(teacher_goal_line_test|teacher_recovery_test|teacher_fallback_test|teacher_explanation_test)$",
        4,
    )

    run_ctest(
        records,
        "task5-trajectory-ctest",
        "^phase4c_teacher_trajectory_test$",
        1,
    )
    run_python(
        records,
        "task5-evaluation-selftest",
        "tests/teacher/phase4c_teacher_evaluation_test.py",
    )
    run_python(
        records,
        "task5-evaluation-orchestrator",
        "tools/p4c/phase4c_teacher_evaluation.py",
        ["--probe", "build/dev-windows/phase4c_teacher_probe.exe"],
        post_validate=lambda stdout, stderr: validate_marker_output(
            (
                "NORMAL_START0=PASS",
                "NORMAL_START1=PASS",
                "MIRROR_START0=PASS",
                "MIRROR_START1=PASS",
                "FIXED_MATCHUP_MATRIX=PASS",
                "INDEPENDENT_PROCESS_DETERMINISM=PASS",
                "TEACHER_V1_IDENTITIES=PASS",
                "TRUSTED_TRAJECTORY_PATH=PASS",
                "SIDECAR_ISOLATION=PASS",
                "BATTLE_COVERAGE=PASS",
                "POSITIVE_LETHAL_LIMITATION=PASS",
                "PHASE4C_TASK5_EVALUATION=PASS",
            ),
            stdout,
            stderr,
        ),
    )

    matrix_rows: list[dict[str, Any]] = []
    for row, starting_player in ROWS:
        label = f"fixed-row-{row}-{starting_player}"
        record = run_teacher_row_probe(
            records,
            label,
            row,
            starting_player,
            task5_row_validator(row, starting_player),
        )
        values = parse_validated_row(record["_stdout"], row, starting_player)
        matrix_rows.append(
            {
                "seat_assignment": row,
                "starting_player": starting_player,
                "status": "PASS" if record["status"] == "PASS" and values else "FAIL",
                "record_count": int(values["RECORD_COUNT"]) if values else 0,
                "battle_decision_record_count": int(values["BATTLE_DECISION_RECORD_COUNT"]) if values else 0,
                "battle_command_candidate_count": int(values["BATTLE_COMMAND_CANDIDATE_COUNT"]) if values else 0,
                "sidecar_invalid_count": int(values["SIDECAR_INVALID_COUNT"]) if values else 0,
                "proven_lethal_count": int(values["PROVEN_LETHAL_COUNT"]) if values else 0,
                "lower_bound_present_count": int(values["LOWER_BOUND_PRESENT_COUNT"]) if values else 0,
            }
        )

    run_ctest(
        records,
        "phase4b-full-teacher-ctest",
        "^(teacher_policy_boundary_compile_test|teacher_domain_preservation_test|teacher_ranking_test|teacher_strategy_state_test|teacher_rejected_transition_test|teacher_recovery_test|teacher_fallback_test|teacher_provenance_test|teacher_runner_trajectory_test|teacher_explanation_test)$",
        10,
    )
    run_python(records, "phase4b-public-boundary-python", "tests/teacher/teacher_public_boundary_test.py")
    run_python(records, "phase4b-paired-world-python", "tests/teacher/teacher_paired_world_test.py")
    run_python(records, "phase4b-profile-binding-python", "tests/teacher/teacher_profile_binding_test.py")
    run_python(
        records,
        "phase4b-determinism-python",
        "tests/teacher/teacher_determinism_test.py",
        ["--probe", "build/dev-windows/teacher_probe.exe"],
    )
    run_python(records, "policy-boundary-python", "tests/policy/policy_boundary_test.py")
    run_python(records, "public-fact-matrix-python", "tests/policy/public_fact_matrix_test.py")
    run_ctest(
        records,
        "trajectory-short-ctest",
        "^(trajectory_codec_test|trajectory_recorder_test|trajectory_shard_test|trajectory_receipt_test|trajectory_dataset_manifest_test)$",
        5,
    )
    run_command(
        records,
        "repository-python",
        [sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests/python", "-v"],
        "python -B -m unittest discover -s tests/python -v",
        15,
        parse_unittest_count,
    )
    run_command(
        records,
        "h-exec-diff-check",
        ["git", "diff", "--check", "HEAD^", "HEAD"],
        "git diff --check HEAD^ HEAD",
    )
    run_python(
        records,
        "acceptance-validator-self-test",
        "tests/teacher/phase4c_acceptance_test.py",
    )

    records_by_label = {record["label"]: record for record in records}
    gates = [
        gate(gate_id, GATE_INVARIANTS[gate_id], GATE_EVIDENCE[gate_id], records_by_label)
        for gate_id in GATE_IDS
    ]
    metrics = {
        "record_count": sum(row["record_count"] for row in matrix_rows),
        "battle_decision_record_count": sum(
            row["battle_decision_record_count"] for row in matrix_rows
        ),
        "battle_command_candidate_count": sum(
            row["battle_command_candidate_count"] for row in matrix_rows
        ),
        "battle_coverage": "PASS"
        if sum(row["battle_decision_record_count"] for row in matrix_rows) > 0
        and sum(row["battle_command_candidate_count"] for row in matrix_rows) > 0
        else "FAIL",
        "sidecar_invalid_count": sum(row["sidecar_invalid_count"] for row in matrix_rows),
        "proven_lethal_count": sum(row["proven_lethal_count"] for row in matrix_rows),
        "lower_bound_present_count": sum(
            row["lower_bound_present_count"] for row in matrix_rows
        ),
        "sidecar_influences_gameplay": "NO",
    }

    missing_required = [
        label for label in REQUIRED_COMMAND_LABELS if label not in records_by_label
    ]
    global_pass = (
        actual_source_head == expected_head
        and actual_source_base == TASK5_BASE
        and not missing_required
        and len(records) == len(REQUIRED_COMMAND_LABELS)
        and all(record["status"] == "PASS" for record in records)
        and all(item["status"] == "PASS" for item in gates)
        and all(row["status"] == "PASS" for row in matrix_rows)
        and metrics["battle_coverage"] == "PASS"
        and metrics["sidecar_invalid_count"] == 0
        and metrics["proven_lethal_count"] == 0
        and metrics["lower_bound_present_count"] == 0
    )

    for record in records:
        record.pop("_stdout", None)
        record.pop("_stderr", None)
        record.pop("_validation_error", None)

    report = {
        "schema_version": SCHEMA,
        "status": "PASS" if global_pass else "FAIL",
        "source_base": actual_source_base,
        "source_head": actual_source_head,
        "environment": ENVIRONMENT,
        "teacher": TEACHER,
        "battle_contracts": {
            "snapshot_schema": SNAPSHOT_SCHEMA,
            "lethal_schema": LETHAL_SCHEMA,
        },
        "integration_decision": INTEGRATION_DECISION,
        "positive_lethal_capability": POSITIVE_LETHAL_CAPABILITY,
        "gates": gates,
        "fixed_matchup_matrix": matrix_rows,
        "task5_metrics": metrics,
        "command_evidence": records,
        "required_command_evidence": REQUIRED_COMMAND_LABELS,
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

    output_json = ROOT / args.output_json
    output_markdown = ROOT / args.output_markdown
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_markdown.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    output_markdown.write_text(render_markdown(report), encoding="utf-8", newline="\n")
    return 0 if global_pass else 1


def render_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# OCGForge Phase 4C Acceptance",
        "",
        f"- schema_version: {report['schema_version']}",
        f"- status: {report['status']}",
        f"- source_base: {report['source_base']}",
        f"- source_head: {report['source_head']}",
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
        f"- sampling: {report['teacher']['deterministic_sampling_contract_identity']}",
    ]
    for key, field in (
        ("profiles", "profile_id"),
        ("bindings", "binding_id"),
        ("policy_artifacts", "artifact_id"),
    ):
        lines.append("")
        lines.append(f"- {key}:")
        for value in report["teacher"][key]:
            lines.append(f"  - {value['role']}: {value[field]}")
    lines.extend(
        [
            "",
            "## Battle contracts",
            "",
            f"- snapshot_schema: {report['battle_contracts']['snapshot_schema']}",
            f"- lethal_schema: {report['battle_contracts']['lethal_schema']}",
            f"- integration_decision: {report['integration_decision']}",
            f"- positive_lethal_capability: {report['positive_lethal_capability']}",
            "",
            "## Gates",
            "",
            "| Gate | Status |",
            "| --- | --- |",
        ]
    )
    for item in report["gates"]:
        lines.append(f"| {item['gate']} | {item['status']} |")
    lines.extend(
        [
            "",
            "## Fixed matchup matrix",
            "",
            "| Seat assignment | Starting player | Status | Records | Battle records | Battle candidates |",
            "| --- | ---: | --- | ---: | ---: | ---: |",
        ]
    )
    for row in report["fixed_matchup_matrix"]:
        lines.append(
            f"| {row['seat_assignment']} | {row['starting_player']} | {row['status']} | "
            f"{row['record_count']} | {row['battle_decision_record_count']} | "
            f"{row['battle_command_candidate_count']} |"
        )
    metrics = report["task5_metrics"]
    lines.extend(
        [
            "",
            "## Task-5 metrics",
            "",
            f"- record_count: {metrics['record_count']}",
            f"- battle_decision_record_count: {metrics['battle_decision_record_count']}",
            f"- battle_command_candidate_count: {metrics['battle_command_candidate_count']}",
            f"- battle_coverage: {metrics['battle_coverage']}",
            f"- sidecar_invalid_count: {metrics['sidecar_invalid_count']}",
            f"- proven_lethal_count: {metrics['proven_lethal_count']}",
            f"- lower_bound_present_count: {metrics['lower_bound_present_count']}",
            f"- sidecar_influences_gameplay: {metrics['sidecar_influences_gameplay']}",
            "",
            "## Command evidence",
            "",
            "| Label | Status | Exit | Expected | Observed | stdout SHA-256 | stderr SHA-256 |",
            "| --- | --- | ---: | ---: | ---: | --- | --- |",
        ]
    )
    for record in report["command_evidence"]:
        lines.append(
            f"| {record['label']} | {record['status']} | {record['exit_code']} | "
            f"{record['expected_selected_count']} | {record['observed_selected_count']} | "
            f"{record['stdout_sha256']} | {record['stderr_sha256']} |"
        )
    lines.extend(
        [
            "",
            "## Heavy evidence",
            "",
            f"- current_head_heavy_replay: {report['heavy_evidence']['current_head_heavy_replay']}",
            f"- phase4a_h_exec: {report['heavy_evidence']['phase4a_h_exec']}",
            f"- phase4a_h_evidence: {report['heavy_evidence']['phase4a_h_evidence']}",
            "- mode: frozen_baseline_reference",
            "",
            "## Scope limitations",
            "",
        ]
    )
    lines.extend(f"- {value}" for value in report["scope_limitations"])
    lines.append("")
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--expected-head", required=True)
    parser.add_argument("--output-json", default="docs/p4c/p4c_acceptance.json")
    parser.add_argument("--output-markdown", default="docs/p4c/P4C_ACCEPTANCE.md")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        sys.exit(run_acceptance(parse_args()))
    except Exception as error:
        print(f"phase4c_acceptance: FAIL: {error}", file=sys.stderr)
        sys.exit(1)
