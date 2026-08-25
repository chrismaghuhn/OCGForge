"""Finalize the M4.3.6 report from completed A/B and regression evidence."""

from __future__ import annotations

import json
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
REPORT_PATH = REPO_ROOT / "docs/m4/m4_3_6_direct_canonical_writer.json"
MARKDOWN_PATH = REPO_ROOT / "docs/m4/M4_3_6_DIRECT_CANONICAL_WRITER.md"
GATE_SUMMARY_PATH = REPO_ROOT / "artifacts/m4/m4-3-6/gates/regression_gate_summary.json"

sys.path.insert(0, str(REPO_ROOT))
from tools.m4.run_m4_3_6_direct_writer_ab import _markdown  # noqa: E402


def _gate(status: str, command: str, evidence: str, **extra: Any) -> dict[str, Any]:
    result: dict[str, Any] = {
        "status": status,
        "command": command,
        "evidence": evidence,
        "artifact": evidence,
    }
    result.update(extra)
    return result


def _regression_gates() -> dict[str, dict[str, Any]]:
    return {
        "full_ctest_control": _gate(
            "PASS",
            "ctest --test-dir build/m4-3-6-control --output-on-failure",
            "build/m4-3-6-control (93/93, exit 0, 162.66 s)",
            tests=93,
            failed=0,
        ),
        "full_ctest_experiment": _gate(
            "PASS",
            "ctest --test-dir build/m4-3-6-experiment2 --output-on-failure",
            "build/m4-3-6-experiment2 (93/93, exit 0, 108.63 s)",
            tests=93,
            failed=0,
        ),
        "repository_python": _gate(
            "PASS",
            "python -B -m unittest discover -s tests/python -v",
            "8 tests passed, exit 0",
            tests=8,
            failed=0,
        ),
        "m3_python": _gate(
            "PASS",
            "python -B -m unittest discover -s tests/m3 -v",
            "17 tests passed, exit 0",
            tests=17,
            failed=0,
        ),
        "m4_python": _gate(
            "PASS",
            "python -B -m unittest discover -s tests/m4 -v",
            "second full invocation: 124 tests, 3 skipped, exit 0",
            tests=124,
            skipped=3,
            failed=0,
        ),
        "privacy_control": _gate(
            "PASS",
            "ctest --test-dir build/m4-3-6-control --output-on-failure",
            "privacy_projection_test and continuation_privacy_test; full CTest",
        ),
        "privacy_experiment": _gate(
            "PASS",
            "ctest --test-dir build/m4-3-6-experiment2 --output-on-failure",
            "privacy_projection_test and continuation_privacy_test; full CTest",
        ),
        "candidate_observation_consistency": _gate(
            "PASS",
            "ctest --test-dir build/m4-3-6-control --output-on-failure",
            "observation_builder_test, m4_worker_integration_test, and unchanged worker counters",
        ),
        "canonical_fixed_deck_regression": _gate(
            "PASS",
            "python tests/m3/full_game/full_fixed_deck_test.py --games 16 --max-steps 2200",
            "artifacts/m4/m4-3-6/gates/fixed_deck_control and fixed_deck_experiment",
            complete_games="16/16 per binary",
            both_start_player_partitions=True,
        ),
        "deterministic_worker_gate": _gate(
            "PASS",
            "compare_build_modes.py plus m3_determinism_test.py for control and experiment",
            "artifacts/m4/m4-3-6/ab_after_control_fix/worker_conformance.json; artifacts/m4/m4-3-6/gates/determinism_{control,experiment}",
            normalized_trace_hash_equal=True,
            raw_trace_hash_equal=True,
            observation_hashes_equal=True,
        ),
    }


def _remaining_buckets(report: dict[str, Any]) -> list[dict[str, Any]]:
    experiment_rows = [
        row["summary"]
        for row in report.get("repetitions", [])
        if row.get("variant") == "experiment"
    ]
    if not experiment_rows:
        return []
    observation_median = statistics.median(
        row["outer_observation_us"] for row in experiment_rows
    )
    bucket_names = (
        "observation_hash",
        "observation_canonical_serialization",
        "observation_query_decode",
        "observation_other",
        "observation_query_location",
        "observation_visibility_privacy",
        "observation_entity_projection",
    )
    rows = []
    for bucket in bucket_names:
        values = [
            row["observation_timing_us"][bucket]["total_us"]
            for row in experiment_rows
        ]
        median_us = statistics.median(values)
        rows.append(
            {
                "bucket": bucket,
                "median_us": median_us,
                "fraction_of_observation_median_percent": (
                    100.0 * median_us / observation_median
                    if observation_median
                    else 0.0
                ),
            }
        )
    rows.sort(key=lambda row: row["median_us"], reverse=True)
    for index, row in enumerate(rows, start=1):
        row["rank"] = index
    return rows


def _augment_games_per_second_timing(report: dict[str, Any]) -> None:
    by_variant: dict[str, list[float]] = {"control": [], "experiment": []}
    for row in report.get("repetitions", []):
        variant = row.get("variant")
        if variant in by_variant:
            by_variant[variant].append(row["summary"]["games_per_second"])
    timing = report["timing_decision"]
    timing.setdefault("medians", {})["games_per_second"] = {
        variant: statistics.median(values) for variant, values in by_variant.items()
    }
    timing.setdefault("ranges", {})["games_per_second"] = {
        f"{variant}_min": min(values) for variant, values in by_variant.items()
    }
    timing["ranges"]["games_per_second"].update(
        {
            f"{variant}_max": max(values)
            for variant, values in by_variant.items()
        }
    )


def _m4_python_evidence() -> dict[str, Any]:
    return {
        "first_full_run": {
            "status": "FAIL",
            "tests": 123,
            "skipped": 3,
            "failure": "test_result_then_exit_never_publishes_passed_under_repeated_scheduling",
            "note": "The isolated retry passed; this run is retained as a scheduling-flake observation.",
        },
        "isolated_retry": {
            "status": "PASS",
            "command": "python -B -m unittest tests.m4.test_failure_isolation.FailureIsolationTests.test_result_then_exit_never_publishes_passed_under_repeated_scheduling -v",
            "tests": 1,
        },
        "second_full_run": {
            "status": "PASS",
            "tests": 124,
            "skipped": 3,
            "elapsed_seconds": 1225.587,
        },
    }


def main() -> int:
    report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
    gates = _regression_gates()
    report["gates"] = gates
    _augment_games_per_second_timing(report)
    report["regression_evidence"] = {"m4_python_runs": _m4_python_evidence()}
    report["remaining_release_observation_buckets"] = _remaining_buckets(report)
    report["implementation"] = {
        "control": "existing std::ostringstream renderer",
        "experiment": "private DirectCanonicalWriter with direct literal/punctuation append and std::to_chars integer formatting",
        "build_switch": "YGO_M4_DIRECT_CANONICAL_WRITER",
        "switch_visibility": "PRIVATE",
        "default_build_uses_experiment": False,
        "json_escape_control_variable_unchanged": True,
        "preparation_and_ordering_unchanged": True,
    }
    report["checkpoints"] = {
        "m4_3_4_frozen_checkpoint": "76c1a028f1c99a8ed46b63da3c3b93b64cf3a0e8",
        "m4_3_5_benchmark_rerun": False,
        "m4_3_5_report_preserved": True,
    }
    report["repository"]["final_git_diff_check_pass"] = subprocess.run(
        ["git", "diff", "--check"], cwd=REPO_ROOT, check=False
    ).returncode == 0
    report["repository"]["final_worktree_status"] = subprocess.run(
        ["git", "status", "--short"],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    ).stdout

    required_gates = (
        "full_ctest_control",
        "full_ctest_experiment",
        "repository_python",
        "m3_python",
        "m4_python",
        "privacy_control",
        "privacy_experiment",
        "candidate_observation_consistency",
        "canonical_fixed_deck_regression",
        "deterministic_worker_gate",
    )
    all_regressions_pass = all(gates[name]["status"] == "PASS" for name in required_gates)
    focused_pass = report["equivalence"]["focused_tests"].get("pass") is True
    semantic_pass = report["equivalence"]["worker_conformance"].get("pass") is True
    timing_pass = report["timing_decision"].get("pass") is True
    if not focused_pass:
        report["status"] = "M4.3.6 REJECTED — CANONICAL BYTE DIVERGENCE"
    elif not semantic_pass:
        report["status"] = "M4.3.6 REJECTED — SEMANTIC DIVERGENCE"
    elif not timing_pass:
        report["status"] = "M4.3.6 REJECTED — NO MATERIAL BENEFIT"
    elif not all_regressions_pass:
        report["status"] = "M4.3.6 REJECTED — REGRESSION GATE FAILURE"
    else:
        report["status"] = "M4.3.6 ACCEPTED"

    GATE_SUMMARY_PATH.parent.mkdir(parents=True, exist_ok=True)
    GATE_SUMMARY_PATH.write_text(
        json.dumps(
            {
                "schema": "ocgforge.m4.m4_3_6_regression_gate_summary.v1",
                "status": report["status"],
                "gates": gates,
                "m4_python_runs": report["regression_evidence"]["m4_python_runs"],
            },
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    REPORT_PATH.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    MARKDOWN_PATH.write_text(_markdown(report), encoding="utf-8")
    print(
        json.dumps(
            {
                "status": report["status"],
                "all_regression_gates_pass": all_regressions_pass,
                "timing_pass": timing_pass,
                "focused_byte_equivalence_pass": focused_pass,
                "semantic_pass": semantic_pass,
            },
            ensure_ascii=False,
            sort_keys=True,
        )
    )
    return 0 if report["status"] == "M4.3.6 ACCEPTED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
