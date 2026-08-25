"""Run the M4.3.5 clean Release equivalence and alternating reserve A/B."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import sys
from typing import Any, Iterable, Mapping

_REPO_ROOT = Path(__file__).resolve().parents[2]
if __package__ in (None, "") and str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.m4.performance_audit import run_audit_sample


MASTER_SEED = 20260815
GAMES = 16
MAX_STEPS = 2200
WORKERS = 1
OBSERVATION_MODE = "full"
THROUGHPUT_MODE = "throughput"
TRACE_PERSISTENCE = False
AUDIT_PREFIX = "M4_PERFORMANCE_AUDIT "
LIFECYCLE_PREFIX = "M4_SERIALIZATION_LIFECYCLE "


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _run(command: list[str], *, cwd: Path = _REPO_ROOT) -> dict[str, Any]:
    completed = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return {
        "command": command,
        "exit_code": completed.returncode,
        "stdout": completed.stdout.decode("utf-8", errors="replace"),
        "stderr": completed.stderr.decode("utf-8", errors="replace"),
        "stdout_sha256": hashlib.sha256(completed.stdout).hexdigest(),
        "stderr_sha256": hashlib.sha256(completed.stderr).hexdigest(),
    }


def _parse_prefixed_records(paths: Iterable[str | Path], prefix: str) -> dict[str, dict[str, Any]]:
    records: dict[str, dict[str, Any]] = {}
    for raw_path in paths:
        path = Path(raw_path)
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if not line.startswith(prefix):
                continue
            record = json.loads(line[len(prefix) :])
            job_id = record.get("job_id")
            if not isinstance(job_id, str) or not job_id:
                raise ValueError(f"{prefix.strip()} record at {path}:{line_number} has no job_id")
            if job_id in records:
                raise ValueError(f"duplicate {prefix.strip()} record for {job_id}")
            records[job_id] = record
    return records


def _sum_numeric_maps(rows: Iterable[Mapping[str, Any]], keys: Iterable[str]) -> dict[str, int]:
    names = tuple(keys)
    result = {key: 0 for key in names}
    for row in rows:
        for key in names:
            value = row.get(key, 0)
            if isinstance(value, bool) or not isinstance(value, int):
                raise ValueError(f"counter {key} is not an integer")
            result[key] += value
    return result


def _sidecar_timing(sidecars: Mapping[str, Mapping[str, Any]], name: str) -> int:
    return sum(
        int(sidecar["observation_timing_us"][name]["total_us"])
        for sidecar in sidecars.values()
    )


def _sample_summary(
    sample: Mapping[str, Any],
    *,
    expected_mode: str | None = None,
    result_evidence_path: Path | None = None,
) -> dict[str, Any]:
    jobs = sample["jobs"]
    results = sample["results"]
    sidecars = sample["sidecars"]
    expected_ids = [str(job["job_id"]) for job in jobs]
    lifecycle = _parse_prefixed_records(sample["worker_stderr_paths"], LIFECYCLE_PREFIX)
    missing_lifecycle = [job_id for job_id in expected_ids if job_id not in lifecycle]
    if missing_lifecycle:
        raise ValueError("missing lifecycle sidecars: " + ", ".join(missing_lifecycle))
    unexpected_lifecycle = sorted(set(lifecycle) - set(expected_ids))
    if unexpected_lifecycle:
        raise ValueError("unexpected lifecycle sidecars: " + ", ".join(unexpected_lifecycle))

    worker_us = sum(int(result["simulation_elapsed_us"]) for result in results)
    errors = _sum_numeric_maps((result["errors"] for result in results), (
        "retries", "unsupported", "automatic", "truncated", "core_errors", "worker_errors"
    ))
    operations = _sum_numeric_maps((result["counters"] for result in results), (
        "ocg_duel_process", "ocg_duel_query", "ocg_duel_query_location", "ocg_duel_query_field",
        "ocg_duel_query_count", "script_reader_requests", "script_loads", "observations",
        "entities_projected", "candidate_sets", "candidate_total", "candidate_max",
        "semantic_hashes", "trace_bytes_serialized"
    ))
    lifecycle_totals = _sum_numeric_maps(lifecycle.values(), (
        "serialize_without_hash_calls", "serialize_without_hash_bytes", "sha256_calls",
        "canonical_serialize_calls", "canonical_serialize_bytes",
        "same_mutation_epoch_duplicate_calls"
    ))
    reserve_records = [sidecar.get("future_m4_3_5_reserve_output") for sidecar in sidecars.values()]
    if all(record is None for record in reserve_records) and expected_mode == "ostringstream":
        reserve = {
            "mode": "ostringstream",
            "calls": lifecycle_totals["serialize_without_hash_calls"],
            "requested_capacity": 0,
            "final_bytes": lifecycle_totals["serialize_without_hash_bytes"],
            "final_capacity": 0,
            "growth_events": 0,
            "unused_capacity": 0,
        }
    elif all(isinstance(record, dict) for record in reserve_records):
        reserve = {
            "mode": {str(record["mode"]) for record in reserve_records},
            "calls": sum(int(record["calls"]) for record in reserve_records),
            "requested_capacity": sum(int(record["requested_capacity"]) for record in reserve_records),
            "final_bytes": sum(int(record["final_bytes"]) for record in reserve_records),
            "final_capacity": sum(int(record["final_capacity"]) for record in reserve_records),
            "growth_events": sum(int(record["growth_events"]) for record in reserve_records),
            "unused_capacity": sum(int(record["unused_capacity"]) for record in reserve_records),
        }
    else:
        raise ValueError("performance sidecars have mixed or unexpected reserve telemetry")
    if isinstance(reserve["mode"], set):
        if len(reserve["mode"]) != 1:
            raise ValueError("mixed reserve telemetry modes in one sample")
        reserve["mode"] = next(iter(reserve["mode"]))
    if expected_mode is not None and reserve["mode"] != expected_mode:
        raise ValueError(
            f"expected reserve telemetry mode {expected_mode!r}, got {reserve['mode']!r}"
        )
    if reserve["calls"] != lifecycle_totals["serialize_without_hash_calls"]:
        raise ValueError("reserve output calls do not close against serialization lifecycle calls")
    if reserve["final_bytes"] != lifecycle_totals["serialize_without_hash_bytes"]:
        raise ValueError("reserve output bytes do not close against serialization lifecycle bytes")
    if reserve["calls"] != operations["observations"]:
        raise ValueError("reserve output calls do not close against observation count")
    if lifecycle_totals["sha256_calls"] != lifecycle_totals["serialize_without_hash_calls"]:
        raise ValueError("SHA-256 calls do not close against serialization lifecycle calls")
    if lifecycle_totals["canonical_serialize_calls"] != 0:
        raise ValueError("throughput lifecycle unexpectedly called canonical_serialize")
    if lifecycle_totals["canonical_serialize_bytes"] != 0:
        raise ValueError("throughput lifecycle unexpectedly emitted canonical_serialize bytes")
    if lifecycle_totals["same_mutation_epoch_duplicate_calls"] != 0:
        raise ValueError("throughput lifecycle contains same-epoch duplicate serialization calls")
    if reserve["mode"] == "reserve_backed":
        if reserve["final_bytes"] > reserve["final_capacity"]:
            raise ValueError("reserve output final bytes exceed final capacity")
        if reserve["final_capacity"] < reserve["requested_capacity"] and reserve["growth_events"] == 0:
            raise ValueError("reserve output grew beyond its requested capacity without a growth event")
        if reserve["unused_capacity"] != reserve["final_capacity"] - reserve["final_bytes"]:
            raise ValueError("reserve output unused capacity does not close")
    elif any(reserve[key] != 0 for key in ("requested_capacity", "final_capacity", "growth_events", "unused_capacity")):
        raise ValueError("ostringstream control telemetry contains reserve-only capacity values")
    for path in sample["worker_stderr_paths"]:
        file_path = Path(path)
        if not file_path.is_file():
            raise ValueError(f"missing worker stderr evidence: {file_path}")

    worker_result_evidence = None
    if result_evidence_path is not None:
        if not result_evidence_path.is_file():
            raise ValueError(f"missing worker result evidence: {result_evidence_path}")
        worker_result_evidence = {
            "path": str(result_evidence_path),
            "bytes": result_evidence_path.stat().st_size,
            "sha256": _sha256_file(result_evidence_path),
        }

    observation_total_us = sum(int(sidecar["observation_total_us"]) for sidecar in sidecars.values())
    serializer_us = _sidecar_timing(sidecars, "observation_canonical_serialization")
    hash_us = _sidecar_timing(sidecars, "observation_hash")
    query_decode_us = _sidecar_timing(sidecars, "observation_query_decode")
    query_location_us = _sidecar_timing(sidecars, "observation_query_location")
    query_field_us = _sidecar_timing(sidecars, "observation_query_field")
    entity_projection_us = _sidecar_timing(sidecars, "observation_entity_projection")
    privacy_us = _sidecar_timing(sidecars, "observation_visibility_privacy")
    return {
        "games": len(results),
        "worker_local_simulation_us": worker_us,
        "games_per_second": len(results) / (worker_us / 1_000_000),
        "outer_observation_us": observation_total_us,
        "serializer_us": serializer_us,
        "hash_us": hash_us,
        "query_decode_us": query_decode_us,
        "query_location_us": query_location_us,
        "query_field_us": query_field_us,
        "entity_projection_us": entity_projection_us,
        "privacy_us": privacy_us,
        "observation_fraction_of_worker_percent": observation_total_us / worker_us * 100,
        "serializer_fraction_of_worker_percent": serializer_us / worker_us * 100,
        "hash_fraction_of_worker_percent": hash_us / worker_us * 100,
        "lifecycle": lifecycle_totals,
        "reserve_output": reserve,
        "operation_counters": operations,
        "error_counters": errors,
        "job_ids": expected_ids,
        "worker_stderr": [
            {
                "path": str(Path(path)),
                "bytes": Path(path).stat().st_size,
                "sha256": _sha256_file(Path(path)),
            }
            for path in sample["worker_stderr_paths"]
        ],
        "worker_result_evidence": worker_result_evidence,
        "ready": sample["ready_messages"],
    }


def _relative_speedup(before: float, after: float) -> float:
    if before == 0:
        return 0.0
    return (before - after) / before * 100.0


def _timing_decision(repetitions: list[dict[str, Any]]) -> dict[str, Any]:
    control = [row for row in repetitions if row["variant"] == "control"]
    experiment = [row for row in repetitions if row["variant"] == "experiment"]
    if len(control) != 3 or len(experiment) != 3:
        return {"pass": False, "reason": "expected three repetitions per variant"}
    control_worker = statistics.median(row["summary"]["worker_local_simulation_us"] for row in control)
    experiment_worker = statistics.median(row["summary"]["worker_local_simulation_us"] for row in experiment)
    control_serializer = statistics.median(row["summary"]["serializer_us"] for row in control)
    experiment_serializer = statistics.median(row["summary"]["serializer_us"] for row in experiment)
    worker_speedup = _relative_speedup(control_worker, experiment_worker)
    serializer_speedup = _relative_speedup(control_serializer, experiment_serializer)
    pairs = list(zip(control, experiment))
    paired_worker_improvements = sum(
        right["summary"]["worker_local_simulation_us"] < left["summary"]["worker_local_simulation_us"]
        for left, right in pairs
    )
    paired_serializer_improvements = sum(
        right["summary"]["serializer_us"] < left["summary"]["serializer_us"]
        for left, right in pairs
    )
    return {
        "materiality_rule": {
            "serializer_median_improvement_percent_min": 5.0,
            "worker_median_improvement_percent_min": 3.0,
            "paired_repetitions_improving_min": 2,
        },
        "control_worker_median_us": control_worker,
        "experiment_worker_median_us": experiment_worker,
        "control_serializer_median_us": control_serializer,
        "experiment_serializer_median_us": experiment_serializer,
        "worker_speedup_percent": worker_speedup,
        "serializer_speedup_percent": serializer_speedup,
        "paired_worker_improvements": paired_worker_improvements,
        "paired_serializer_improvements": paired_serializer_improvements,
        "pass": (
            worker_speedup >= 3.0
            and serializer_speedup >= 5.0
            and paired_worker_improvements >= 2
            and paired_serializer_improvements >= 2
        ),
    }


def _build_identity(
    worker: Path,
    build_dir: Path,
    ready: list[dict[str, Any]],
    *,
    expected_variant: str,
) -> dict[str, Any]:
    cache = build_dir / "CMakeCache.txt"
    ninja = build_dir / "build.ninja"
    cache_text = cache.read_text(encoding="utf-8", errors="replace") if cache.is_file() else ""
    ninja_text = ninja.read_text(encoding="utf-8", errors="replace") if ninja.is_file() else ""
    cache_fields: dict[str, str] = {}
    for key in (
        "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID",
        "CMAKE_CXX_COMPILER_VERSION", "CMAKE_CXX_FLAGS", "CMAKE_GENERATOR",
    ):
        prefix = key + ":"
        cache_fields[key] = next(
            (line.split("=", 1)[1] for line in cache_text.splitlines() if line.startswith(prefix) and "=" in line),
            "NOT_RECORDED",
        )
    flag_lines = "\n".join(line for line in ninja_text.splitlines() if "FLAGS =" in line)
    forbidden_tokens = (
        "-flto", "/gl", "lto", "pgo", "-fprofile", "-march=", "-mcpu=", "-mtune=",
        "/arch:", "-mavx", "-msse", "ffast-math",
        "ygo_m4_serialization_shape_audit",
    )
    forbidden = sorted(token for token in forbidden_tokens if token in flag_lines.lower())
    reserve_macro = "ygo_m4_reserve_backed_serialization"
    reserve_macro_present = reserve_macro in flag_lines.lower()
    expected_reserve_macro = expected_variant == "experiment"
    if expected_variant not in {"control", "experiment"}:
        raise ValueError(f"unknown A/B build variant: {expected_variant}")
    if reserve_macro_present != expected_reserve_macro:
        expected = "present" if expected_reserve_macro else "absent"
        raise ValueError(
            f"{build_dir} reserve macro must be {expected}, "
            f"but was {'present' if reserve_macro_present else 'absent'}"
        )
    if cache_fields["CMAKE_BUILD_TYPE"] != "Release" or cache_fields["CMAKE_GENERATOR"] != "Ninja":
        raise ValueError(f"{build_dir} is not a Release/Ninja build")
    if "-O3" not in flag_lines or "-DNDEBUG" not in flag_lines:
        raise ValueError(f"{build_dir} lacks ordinary Release -O3 -DNDEBUG evidence")
    if forbidden:
        raise ValueError(f"{build_dir} contains forbidden optimization flags: {forbidden}")
    if not ready or not isinstance(ready[0].get("compiler_identity"), str):
        raise ValueError(f"{build_dir} lacks compiler identity in the ready record")
    return {
        "worker": str(worker.resolve()),
        "worker_sha256": _sha256_file(worker),
        "build_dir": str(build_dir.resolve()),
        "cmake_cache_sha256": hashlib.sha256(cache_text.encode("utf-8")).hexdigest(),
        "cmake_cache": cache_fields,
        "release_flags_observed": "-O3 -DNDEBUG",
        "forbidden_flags": forbidden,
        "expected_variant": expected_variant,
        "reserve_macro_present": reserve_macro_present,
        "shape_instrumentation_present": "YGO_M4_SERIALIZATION_SHAPE_AUDIT" in flag_lines,
        "ordinary_release_policy_pass": True,
        "ready": ready,
    }


def _render_markdown(report: Mapping[str, Any]) -> str:
    decision = report.get("timing_decision", {})
    lines = [
        "# M4.3.5 Reserve-Backed Canonical Serialization A/B",
        "",
        f"**Status:** {report.get('status', 'NOT_RECORDED')}",
        "",
        "## Scope and workload",
        "",
        "This report measures only the top-level serializer output-buffer experiment. "
        "Event history, canonical format, escaping, sorting, SHA-256, privacy, and engine inputs were unchanged.",
        "",
        "| Field | Value |",
        "|---|---|",
        f"| Workload | Swordsoul Tenyi ML v1 vs Salamangreat ML v1 |",
        f"| Master seed | `{MASTER_SEED}` |",
        f"| Games / workers / max steps | `{GAMES}` / `{WORKERS}` / `{MAX_STEPS}` |",
        "| Observation / mode / trace persistence | FULL / throughput / off |",
        f"| Starting HEAD | `{report['repository']['starting_head']}` |",
        "",
        "## Equivalence gates",
        "",
        f"- Focused fixture and existing observation-builder output: **{report['equivalence']['fixture_comparison_pass']}**.",
        f"- Conformance trace comparison, including per-observation hashes: **{report['equivalence']['conformance_comparison_pass']}**.",
        "- Full regression gates are recorded separately and must be PASS before acceptance.",
        "",
        "## Raw timing repetitions",
        "",
        "| Run | Variant | Worker-local us | Games/s | Serializer us | Hash us | Outer observation us |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for row in report.get("repetitions", []):
        summary = row["summary"]
        lines.append(
            f"| {row['label']} | {row['variant']} | {summary['worker_local_simulation_us']} | "
            f"{summary['games_per_second']:.9f} | {summary['serializer_us']} | {summary['hash_us']} | "
            f"{summary['outer_observation_us']} |"
        )
    lines.extend([
        "",
        "## Median/range decision",
        "",
        f"- Worker-local median speedup: `{decision.get('worker_speedup_percent', 'NOT_RUN')}`%.",
        f"- Serializer median speedup: `{decision.get('serializer_speedup_percent', 'NOT_RUN')}`%.",
        f"- Paired worker improvements: `{decision.get('paired_worker_improvements', 'NOT_RUN')}/3`.",
        f"- Paired serializer improvements: `{decision.get('paired_serializer_improvements', 'NOT_RUN')}/3`.",
        f"- Materiality decision before full gates: **{'PASS' if decision.get('pass') else 'FAIL/NOT_RUN'}**.",
        "",
        "## Structural metrics",
        "",
        "The JSON artifact retains every raw sidecar path/hash, operation counter set, lifecycle totals, and reserve request/final-capacity/growth/unused totals. Control and experiment must have identical operation counters and canonical byte totals.",
        "",
        "## Scope boundary",
        "",
        "No event-history reduction, `to_chars` rewrite, JSON-escape rewrite, hash change, cache, incremental observation, ocgcore change, or M5 work was performed.",
        "",
    ])
    return "\n".join(lines)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control-worker", required=True, type=Path)
    parser.add_argument("--experiment-worker", required=True, type=Path)
    parser.add_argument("--control-build-dir", required=True, type=Path)
    parser.add_argument("--experiment-build-dir", required=True, type=Path)
    parser.add_argument("--run-root", required=True, type=Path)
    parser.add_argument("--fixture-json", required=True, type=Path)
    parser.add_argument("--conformance-json", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-markdown", required=True, type=Path)
    args = parser.parse_args(arguments)
    args.run_root.mkdir(parents=True, exist_ok=True)

    fixture_command = [
        sys.executable, "tools/m4/compare_reserve_serialization.py",
        "--control-fixture", str((args.control_build_dir / "m4_3_5_serialization_fixture_test.exe").resolve()),
        "--experiment-fixture", str((args.experiment_build_dir / "m4_3_5_serialization_fixture_test.exe").resolve()),
        "--control-builder", str((args.control_build_dir / "observation_builder_test.exe").resolve()),
        "--experiment-builder", str((args.experiment_build_dir / "observation_builder_test.exe").resolve()),
        "--output-json", str(args.fixture_json.resolve()),
    ]
    fixture_run = _run(fixture_command)
    fixture_report = json.loads(args.fixture_json.read_text(encoding="utf-8")) if args.fixture_json.is_file() else {}

    conformance_json = args.conformance_json.resolve()
    conformance_command = [
        sys.executable, "tools/m4/compare_build_modes.py",
        "--debug-worker", str(args.control_worker.resolve()),
        "--release-worker", str(args.experiment_worker.resolve()),
        "--master-seed", str(MASTER_SEED), "--games", str(GAMES), "--max-steps", str(MAX_STEPS),
        "--output-dir", str((args.run_root / "conformance").resolve()),
        "--output", str(conformance_json),
    ]
    conformance_run = _run(conformance_command) if fixture_run["exit_code"] == 0 else {
        "command": conformance_command, "exit_code": None, "stdout": "", "stderr": "",
        "stdout_sha256": hashlib.sha256(b"").hexdigest(), "stderr_sha256": hashlib.sha256(b"").hexdigest(),
    }
    conformance_report = json.loads(conformance_json.read_text(encoding="utf-8")) if conformance_json.is_file() else {}

    equivalence = {
        "fixture_comparison_pass": fixture_report.get("comparison", {}).get("pass") is True,
        "conformance_comparison_pass": conformance_report.get("comparison", {}).get("pass") is True,
        "fixture_runner": fixture_run,
        "conformance_runner": conformance_run,
    }
    repetitions: list[dict[str, Any]] = []
    operation_reference: dict[str, int] | None = None
    error_reference: dict[str, int] | None = None
    lifecycle_reference: dict[str, int] | None = None
    if all(equivalence[key] for key in ("fixture_comparison_pass", "conformance_comparison_pass")):
        for label, variant, worker in (
            ("A1", "control", args.control_worker),
            ("B1", "experiment", args.experiment_worker),
            ("A2", "control", args.control_worker),
            ("B2", "experiment", args.experiment_worker),
            ("A3", "control", args.control_worker),
            ("B3", "experiment", args.experiment_worker),
        ):
            sample = run_audit_sample(
                worker,
                master_seed=MASTER_SEED,
                games=GAMES,
                max_steps=MAX_STEPS,
                observation_mode=OBSERVATION_MODE,
                output_dir=args.run_root / "throughput" / label,
                require_primary_integrity=True,
            )
            result_evidence_path = args.run_root / "throughput" / label / "worker-results.json"
            result_evidence_path.write_text(
                json.dumps(sample["results"], ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            expected_mode = "ostringstream" if variant == "control" else "reserve_backed"
            summary = _sample_summary(
                sample,
                expected_mode=expected_mode,
                result_evidence_path=result_evidence_path,
            )
            if operation_reference is None:
                operation_reference = summary["operation_counters"]
                error_reference = summary["error_counters"]
            if summary["operation_counters"] != operation_reference:
                raise ValueError(f"operation counters changed in {label}")
            if summary["error_counters"] != error_reference:
                raise ValueError(f"error counters changed in {label}")
            if lifecycle_reference is None:
                lifecycle_reference = summary["lifecycle"]
            elif summary["lifecycle"] != lifecycle_reference:
                raise ValueError(f"serialization lifecycle changed in {label}")
            repetitions.append({"label": label, "variant": variant, "summary": summary})

    decision = _timing_decision(repetitions) if repetitions else {"pass": False, "reason": "equivalence gates did not pass"}
    status = "M4.3.5 PERFORMANCE EVIDENCE READY — REGRESSION GATES PENDING"
    equivalence_pass = all(equivalence[key] for key in ("fixture_comparison_pass", "conformance_comparison_pass"))
    if not equivalence_pass:
        status = "M4.3.5 REJECTED — EQUIVALENCE GATE FAILED"
    elif repetitions and not decision["pass"]:
        status = "M4.3.5 REJECTED — NO MATERIAL BENEFIT (REGRESSION GATES PENDING)"
    report = {
        "schema": "ocgforge.m4.m4_3_5_reserve_backed_serialization.v1",
        "status": status,
        "repository": {
            "starting_head": _run(["git", "rev-parse", "HEAD"])["stdout"].strip(),
            "worktree_status": _run(["git", "status", "--short"])["stdout"],
            "m4_3_4_freeze_commit": "76c1a028f1c99a8ed46b63da3c3b93b64cf3a0e8",
        },
        "builds": {
            "control": _build_identity(args.control_worker, args.control_build_dir,
                                         [row["summary"]["ready"][0] for row in repetitions if row["variant"] == "control"][:1],
                                         expected_variant="control"),
            "experiment": _build_identity(args.experiment_worker, args.experiment_build_dir,
                                           [row["summary"]["ready"][0] for row in repetitions if row["variant"] == "experiment"][:1],
                                           expected_variant="experiment"),
            "shape_instrumentation_used_for_timing": False,
            "release_flags_policy": "ordinary Release -O3 -DNDEBUG; no LTO/PGO/CPU-specific flags",
        },
        "workload": {
            "matchup": "Swordsoul Tenyi ML v1 vs Salamangreat ML v1",
            "master_seed": MASTER_SEED, "games": GAMES, "workers": WORKERS,
            "max_steps": MAX_STEPS, "observation_mode": OBSERVATION_MODE,
            "mode": THROUGHPUT_MODE, "trace_persistence": TRACE_PERSISTENCE,
        },
        "equivalence": equivalence,
        "repetitions": repetitions,
        "timing_decision": decision,
        "gates": {
            "full_ctest": "NOT_RUN",
            "repository_python": "NOT_RUN",
            "m3_python": "NOT_RUN",
            "m4_python": "NOT_RUN",
            "privacy": "NOT_RUN",
            "candidate_observation_consistency": "NOT_RUN",
            "canonical_fixed_deck_regression": "NOT_RUN",
            "worker_semantic_gate": "PASS" if equivalence["conformance_comparison_pass"] else "FAIL",
        },
        "acceptance_rule": {
            "requires_equivalence": True,
            "requires_all_gates_pass": True,
            "requires_material_timing": True,
            "materiality": decision,
        },
        "fixture_comparison_artifact": str(args.fixture_json.resolve()),
        "conformance_comparison_artifact": str(conformance_json),
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
    args.output_markdown.write_text(_render_markdown(report), encoding="utf-8")
    print(json.dumps({"status": status, "timing_decision": decision}, ensure_ascii=False, sort_keys=True))
    return 0 if equivalence_pass and decision.get("pass", False) else 1


if __name__ == "__main__":
    raise SystemExit(main())
