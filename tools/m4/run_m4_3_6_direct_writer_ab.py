"""Run the M4.3.6 direct canonical writer equivalence and A/B evidence."""

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
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.m4.performance_audit import run_audit_sample


MASTER_SEED = 20260815
GAMES = 16
WORKERS = 1
MAX_STEPS = 2200
OBSERVATION_MODE = "full"
THROUGHPUT_MODE = "throughput"
TRACE_PERSISTENCE = False
EXPECTED_OBSERVATIONS = 9_908
EXPECTED_CANONICAL_BYTES = 1_345_246_987
FIXTURE_DUMP_FILES = (
    ("rich", "canonical_without_hash", "rich.canonical_without_hash.bin"),
    ("rich", "canonical", "rich.canonical.bin"),
    ("terminal", "canonical_without_hash", "terminal.canonical_without_hash.bin"),
    ("terminal", "canonical", "terminal.canonical.bin"),
)
AUDIT_PREFIX = "M4_PERFORMANCE_AUDIT "
LIFECYCLE_PREFIX = "M4_SERIALIZATION_LIFECYCLE "
TIMING_NAMES = (
    "observation_query_field",
    "observation_query_location",
    "observation_query_individual",
    "observation_query_decode",
    "observation_zone_projection",
    "observation_entity_projection",
    "observation_relationship_projection",
    "observation_visibility_privacy",
    "observation_candidate_consistency",
    "observation_canonical_serialization",
    "observation_hash",
    "observation_other",
)
COUNTER_NAMES = (
    "ocg_duel_process",
    "ocg_duel_query",
    "ocg_duel_query_location",
    "ocg_duel_query_field",
    "ocg_duel_query_count",
    "script_reader_requests",
    "script_loads",
    "observations",
    "entities_projected",
    "candidate_sets",
    "candidate_total",
    "candidate_max",
    "semantic_hashes",
    "trace_bytes_serialized",
)
ERROR_NAMES = ("retries", "unsupported", "automatic", "truncated", "core_errors", "worker_errors")
LIFECYCLE_NAMES = (
    "serialize_without_hash_calls",
    "serialize_without_hash_bytes",
    "sha256_calls",
    "canonical_serialize_calls",
    "canonical_serialize_bytes",
    "same_mutation_epoch_duplicate_calls",
)


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


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
        "stdout_bytes": len(completed.stdout),
        "stderr_bytes": len(completed.stderr),
        "stdout_sha256": _sha256_bytes(completed.stdout),
        "stderr_sha256": _sha256_bytes(completed.stderr),
        "stdout": completed.stdout.decode("utf-8", errors="replace"),
        "stderr": completed.stderr.decode("utf-8", errors="replace"),
    }


def _parse_records(paths: Iterable[str | Path], prefix: str) -> dict[str, dict[str, Any]]:
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
                raise ValueError(f"{key} is not an integer")
            result[key] += value
    return result


def _sum_timing(sidecars: Iterable[Mapping[str, Any]], name: str) -> dict[str, int]:
    total_us = 0
    calls = 0
    for sidecar in sidecars:
        bucket = sidecar["observation_timing_us"][name]
        total_us += int(bucket["total_us"])
        calls += int(bucket["calls"])
    return {
        "total_us": total_us,
        "calls": calls,
        "mean_us_per_call": 0 if calls == 0 else total_us // calls,
    }


def _summarize_sample(sample: Mapping[str, Any]) -> dict[str, Any]:
    jobs = sample["jobs"]
    results = sample["results"]
    sidecars = list(sample["sidecars"].values())
    expected_ids = [str(job["job_id"]) for job in jobs]
    lifecycle = _parse_records(sample["worker_stderr_paths"], LIFECYCLE_PREFIX)
    if sorted(lifecycle) != sorted(expected_ids):
        raise ValueError("serialization lifecycle job IDs do not close against the workload")

    lifecycle_totals = _sum_numeric_maps(lifecycle.values(), LIFECYCLE_NAMES)
    operations = _sum_numeric_maps((result["counters"] for result in results), COUNTER_NAMES)
    errors = _sum_numeric_maps((result["errors"] for result in results), ERROR_NAMES)
    timing = {name: _sum_timing(sidecars, name) for name in TIMING_NAMES}
    worker_us = sum(int(result["simulation_elapsed_us"]) for result in results)
    observation_us = sum(int(sidecar["observation_total_us"]) for sidecar in sidecars)

    if operations["observations"] != EXPECTED_OBSERVATIONS:
        raise ValueError(f"expected {EXPECTED_OBSERVATIONS} observations, got {operations['observations']}")
    if lifecycle_totals["serialize_without_hash_calls"] != EXPECTED_OBSERVATIONS:
        raise ValueError("serialize_without_hash call count diverged")
    if lifecycle_totals["serialize_without_hash_bytes"] != EXPECTED_CANONICAL_BYTES:
        raise ValueError("canonical byte total diverged")
    if lifecycle_totals["sha256_calls"] != EXPECTED_OBSERVATIONS:
        raise ValueError("SHA-256 call count diverged")
    if lifecycle_totals["canonical_serialize_calls"] != 0 or lifecycle_totals["canonical_serialize_bytes"] != 0:
        raise ValueError("canonical_serialize was consumed by throughput")
    if lifecycle_totals["same_mutation_epoch_duplicate_calls"] != 0:
        raise ValueError("same-epoch duplicate materialization was observed")
    if worker_us <= 0:
        raise ValueError("worker-local runtime is not positive")

    return {
        "games": len(results),
        "worker_local_simulation_us": worker_us,
        "games_per_second": len(results) / (worker_us / 1_000_000),
        "outer_observation_us": observation_us,
        "observation_fraction_of_worker_percent": observation_us / worker_us * 100.0,
        "observation_timing_us": timing,
        "lifecycle": lifecycle_totals,
        "operation_counters": operations,
        "error_counters": errors,
        "job_ids": expected_ids,
        "coordinator_timing": sample["coordinator_timing"],
        "ready": sample["ready_messages"],
        "worker_stderr": [
            {
                "path": str(Path(path)),
                "bytes": Path(path).stat().st_size,
                "sha256": _sha256_file(Path(path)),
            }
            for path in sample["worker_stderr_paths"]
        ],
    }


def _run_executable(path: Path, extra_args: Iterable[str | Path] = ()) -> dict[str, Any]:
    resolved = path.resolve()
    if not resolved.is_file():
        return {"path": str(resolved), "exit_code": None, "error": "missing executable"}
    command = [str(resolved), *(str(value) for value in extra_args)]
    result = _run(command)
    return {
        "path": str(resolved),
        "command": command,
        "exit_code": result["exit_code"],
        "stdout_bytes": result["stdout_bytes"],
        "stderr_bytes": result["stderr_bytes"],
        "stdout_sha256": result["stdout_sha256"],
        "stderr_sha256": result["stderr_sha256"],
        "stdout": result["stdout"],
        "stderr": result["stderr"],
    }


def _file_record(path: Path) -> dict[str, Any]:
    resolved = path.resolve()
    if not resolved.is_file():
        return {
            "path": str(resolved),
            "exists": False,
            "bytes": None,
            "sha256": None,
        }
    raw = resolved.read_bytes()
    return {
        "path": str(resolved),
        "exists": True,
        "bytes": len(raw),
        "sha256": _sha256_bytes(raw),
    }


def _compare_fixture_dump_dirs(control_dump_dir: Path, experiment_dump_dir: Path) -> dict[str, Any]:
    files: list[dict[str, Any]] = []
    for fixture, artifact, file_name in FIXTURE_DUMP_FILES:
        control_path = control_dump_dir / file_name
        experiment_path = experiment_dump_dir / file_name
        control = _file_record(control_path)
        experiment = _file_record(experiment_path)
        control_bytes = control_path.resolve().read_bytes() if control["exists"] else None
        experiment_bytes = experiment_path.resolve().read_bytes() if experiment["exists"] else None
        comparison = {
            "control_exists": control["exists"],
            "experiment_exists": experiment["exists"],
            "size_equal": (
                control["bytes"] is not None
                and control["bytes"] == experiment["bytes"]
            ),
            "sha256_equal": (
                control["sha256"] is not None
                and control["sha256"] == experiment["sha256"]
            ),
            "byte_exact": (
                control_bytes is not None
                and experiment_bytes is not None
                and control_bytes == experiment_bytes
            ),
        }
        comparison["pass"] = all(comparison.values())
        files.append({
            "fixture": fixture,
            "artifact": artifact,
            "file_name": file_name,
            "control": control,
            "experiment": experiment,
            "comparison": comparison,
        })
    return {
        "control_dump_dir": str(control_dump_dir.resolve()),
        "experiment_dump_dir": str(experiment_dump_dir.resolve()),
        "files": files,
        "pass": all(row["comparison"]["pass"] for row in files),
    }


def _pair_tests(
    control_dir: Path,
    experiment_dir: Path,
    fixture_dump_root: Path | None = None,
) -> dict[str, Any]:
    names = (
        "m4_3_5_serialization_fixture_test",
        "m4_3_6_direct_writer_test",
        "observation_builder_test",
        "m2_1_xyz_api_test",
        "privacy_projection_test",
        "continuation_privacy_test",
        "sha256_known_answer_test",
    )
    if fixture_dump_root is None:
        fixture_dump_root = control_dir / "m4_3_6_fixture_byte_dumps"
    fixture_dump_root = fixture_dump_root.resolve()
    control_dump_dir = fixture_dump_root / "control"
    experiment_dump_dir = fixture_dump_root / "experiment"
    control_dump_dir.mkdir(parents=True, exist_ok=True)
    experiment_dump_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    for name in names:
        fixture_args_control: tuple[str | Path, ...] = ()
        fixture_args_experiment: tuple[str | Path, ...] = ()
        if name == "m4_3_5_serialization_fixture_test":
            fixture_args_control = ("--dump-dir", control_dump_dir)
            fixture_args_experiment = ("--dump-dir", experiment_dump_dir)
        left = _run_executable(control_dir / f"{name}.exe", fixture_args_control)
        right = _run_executable(experiment_dir / f"{name}.exe", fixture_args_experiment)
        equal = {
            "exit_codes_equal": left.get("exit_code") == right.get("exit_code"),
            "both_exit_codes_zero": left.get("exit_code") == 0 and right.get("exit_code") == 0,
            "stdout_byte_exact": left.get("stdout") == right.get("stdout"),
            "stderr_byte_exact": left.get("stderr") == right.get("stderr"),
        }
        rows.append({
            "name": name,
            "control": {key: value for key, value in left.items() if key not in {"stdout", "stderr"}},
            "experiment": {key: value for key, value in right.items() if key not in {"stdout", "stderr"}},
            "comparison": {**equal, "pass": all(equal.values())},
        })
    fixture_dumps = _compare_fixture_dump_dirs(control_dump_dir, experiment_dump_dir)
    return {
        "tests": rows,
        "fixture_dumps": fixture_dumps,
        "pass": all(row["comparison"]["pass"] for row in rows) and fixture_dumps["pass"],
    }


def _cache_fields(build_dir: Path) -> dict[str, str]:
    cache = build_dir / "CMakeCache.txt"
    text = cache.read_text(encoding="utf-8", errors="replace") if cache.is_file() else ""
    names = (
        "CMAKE_BUILD_TYPE",
        "CMAKE_CXX_COMPILER",
        "CMAKE_CXX_FLAGS",
        "CMAKE_GENERATOR",
        "YGO_M4_DIRECT_CANONICAL_WRITER",
    )
    fields: dict[str, str] = {}
    for name in names:
        prefix = name + ":"
        fields[name] = next(
            (line.split("=", 1)[1] for line in text.splitlines() if line.startswith(prefix) and "=" in line),
            "NOT_RECORDED",
        )
    return fields


def _build_identity(worker: Path, build_dir: Path, *, expected_direct: bool, ready: list[dict[str, Any]]) -> dict[str, Any]:
    ninja = build_dir / "build.ninja"
    ninja_text = ninja.read_text(encoding="utf-8", errors="replace") if ninja.is_file() else ""
    lower_flags = ninja_text.lower()
    direct_present = "ygo_m4_direct_canonical_writer" in lower_flags
    shape_present = "ygo_m4_serialization_shape_audit" in lower_flags
    reserve_present = "ygo_m4_reserve_backed_serialization" in lower_flags
    forbidden_tokens = (
        "-flto", "/gl", "pgo", "-fprofile", "-march=", "-mcpu=", "-mtune=",
        "/arch:", "-mavx", "-msse", "ffast-math",
    )
    forbidden = sorted(token for token in forbidden_tokens if token in lower_flags)
    if direct_present != expected_direct:
        raise ValueError(f"{build_dir} direct-writer macro presence does not match the variant")
    if shape_present or reserve_present or forbidden:
        raise ValueError(f"{build_dir} contains forbidden instrumentation/optimization flags")
    if "-o3" not in lower_flags or "-dndebug" not in lower_flags:
        raise ValueError(f"{build_dir} lacks ordinary Release -O3 -DNDEBUG evidence")
    if not ready or not isinstance(ready[0].get("compiler_identity"), str):
        raise ValueError(f"{build_dir} lacks compiler identity in worker ready evidence")
    return {
        "worker": str(worker.resolve()),
        "worker_sha256": _sha256_file(worker),
        "build_dir": str(build_dir.resolve()),
        "cmake_cache": _cache_fields(build_dir),
        "release_flags_observed": "-O3 -DNDEBUG",
        "compiler_identity": ready[0]["compiler_identity"],
        "direct_writer_macro_present": direct_present,
        "shape_instrumentation_present": shape_present,
        "reserve_experiment_macro_present": reserve_present,
        "forbidden_flags": forbidden,
        "ordinary_release_policy_pass": True,
    }


def _percent_speedup(control: float, experiment: float) -> float:
    return 0.0 if control == 0 else (control - experiment) / control * 100.0


def _timing_decision(repetitions: list[dict[str, Any]]) -> dict[str, Any]:
    controls = [row for row in repetitions if row["variant"] == "control"]
    experiments = [row for row in repetitions if row["variant"] == "experiment"]
    if len(controls) != 3 or len(experiments) != 3:
        return {"pass": False, "reason": "expected three repetitions per variant"}

    def value(row: Mapping[str, Any], path: tuple[str, ...]) -> float:
        current: Any = row["summary"]
        for key in path:
            current = current[key]
        return float(current)

    metrics = {
        "worker_local_simulation_us": ("worker_local_simulation_us",),
        "serializer_us": ("observation_timing_us", "observation_canonical_serialization", "total_us"),
        "observation_us": ("outer_observation_us",),
        "hash_us": ("observation_timing_us", "observation_hash", "total_us"),
    }
    medians: dict[str, dict[str, float]] = {}
    ranges: dict[str, dict[str, float]] = {}
    speedups: dict[str, float] = {}
    for name, path in metrics.items():
        left = [value(row, path) for row in controls]
        right = [value(row, path) for row in experiments]
        medians[name] = {"control": statistics.median(left), "experiment": statistics.median(right)}
        ranges[name] = {
            "control_min": min(left), "control_max": max(left),
            "experiment_min": min(right), "experiment_max": max(right),
        }
        speedups[name] = _percent_speedup(medians[name]["control"], medians[name]["experiment"])

    paired = []
    for left, right in zip(controls, experiments):
        paired.append({
            "control": left["label"],
            "experiment": right["label"],
            "worker_improved": value(right, metrics["worker_local_simulation_us"]) < value(left, metrics["worker_local_simulation_us"]),
            "serializer_improved": value(right, metrics["serializer_us"]) < value(left, metrics["serializer_us"]),
        })
    worker_improvements = sum(row["worker_improved"] for row in paired)
    serializer_improvements = sum(row["serializer_improved"] for row in paired)
    return {
        "materiality_rule": {
            "serializer_median_improvement_percent_min": 5.0,
            "worker_median_improvement_percent_min": 3.0,
            "paired_repetitions_improving_min": 2,
        },
        "medians": medians,
        "ranges": ranges,
        "speedup_percent": speedups,
        "paired": paired,
        "paired_worker_improvements": worker_improvements,
        "paired_serializer_improvements": serializer_improvements,
        "pass": (
            speedups["serializer_us"] >= 5.0
            and speedups["worker_local_simulation_us"] >= 3.0
            and worker_improvements >= 2
            and serializer_improvements >= 2
        ),
    }


def _markdown(report: Mapping[str, Any]) -> str:
    decision = report["timing_decision"]
    gates = report.get("gates", {})

    def gate_status(name: str) -> str:
        value = gates.get(name, "NOT_RECORDED")
        if isinstance(value, Mapping):
            return str(value.get("status", "NOT_RECORDED"))
        return str(value)

    def format_metric(value: Any, games_per_second: bool = False) -> str:
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            return f"{value:.9f}" if games_per_second else f"{value:.0f}"
        return str(value)

    lines = [
        "# M4.3.6 Direct Canonical Writer A/B",
        "",
        f"**Status:** {report['status']}",
        "",
        "## Scope and exact workload",
        "",
        "This experiment changes only primitive canonical rendering behind an internal build switch. "
        "Canonical ordering, preparation, escaping contract, observation schema, event history, SHA-256, privacy, and engine inputs are unchanged.",
        "",
        "| Field | Value |",
        "|---|---|",
        "| Matchup | Swordsoul Tenyi ML v1 vs Salamangreat ML v1 |",
        f"| Seed / games / workers / max steps | `{MASTER_SEED}` / `{GAMES}` / `{WORKERS}` / `{MAX_STEPS}` |",
        "| Observation / mode / trace persistence | FULL / throughput / off |",
        f"| Starting HEAD | `{report['repository']['starting_head']}` |",
        "",
        "## Implementation boundary",
        "",
        "- Control: the existing `std::ostringstream` canonical output path.",
        "- Experiment: private `DirectCanonicalWriter` selected only by the PRIVATE `YGO_M4_DIRECT_CANONICAL_WRITER` build definition.",
        "- Integer primitives use `std::to_chars`; literals and punctuation append directly to `std::string`.",
        "- `json_escape_impl()` remains unchanged and continues to use its existing temporary `std::ostringstream`; escaping is therefore a controlled, unchanged variable in this experiment.",
        "- Preparation, copies, sorting, field order, event history, schema, hashing, privacy, queries, and engine inputs are unchanged.",
        "",
        "## Equivalence",
        "",
        f"- Focused cross-build fixture/unit/privacy tests: **{'PASS' if report['equivalence']['focused_tests']['pass'] else 'FAIL'}**.",
        f"- Deterministic worker conformance and trace comparison: **{'PASS' if report['equivalence']['worker_conformance']['pass'] else 'FAIL'}**.",
        "",
    ]
    fixture_dumps = report["equivalence"]["focused_tests"].get("fixture_dumps", {})
    lines.extend([
        "### Raw canonical fixture dumps",
        "",
        f"- Exact bytewise fixture gate: **{'PASS' if fixture_dumps.get('pass') is True else 'FAIL'}**.",
        f"- Control dump directory: `{fixture_dumps.get('control_dump_dir', 'NOT_RECORDED')}`.",
        f"- Experiment dump directory: `{fixture_dumps.get('experiment_dump_dir', 'NOT_RECORDED')}`.",
        "",
        "| Fixture | Artifact | Control bytes | Control SHA-256 | Experiment bytes | Experiment SHA-256 | Exact bytes |",
        "|---|---|---:|---|---:|---|---|",
    ])
    for row in fixture_dumps.get("files", []):
        control = row.get("control", {})
        experiment = row.get("experiment", {})
        comparison = row.get("comparison", {})
        lines.append(
            f"| {row.get('fixture', 'NOT_RECORDED')} | {row.get('artifact', 'NOT_RECORDED')} | "
            f"{control.get('bytes', 'NOT_FOUND')} | `{control.get('sha256', 'NOT_FOUND')}` | "
            f"{experiment.get('bytes', 'NOT_FOUND')} | `{experiment.get('sha256', 'NOT_FOUND')}` | "
            f"{'PASS' if comparison.get('byte_exact') is True else 'FAIL'} |"
        )
    lines.extend([
        "",
        "## Raw alternating Release repetitions",
        "",
        "| Run | Variant | Worker-local us | Games/s | Serializer us | Observation us | Hash us |",
        "|---|---|---:|---:|---:|---:|---:|",
    ])
    for row in report["repetitions"]:
        summary = row["summary"]
        lines.append(
            f"| {row['label']} | {row['variant']} | {summary['worker_local_simulation_us']} | "
            f"{summary['games_per_second']:.9f} | "
            f"{summary['observation_timing_us']['observation_canonical_serialization']['total_us']} | "
            f"{summary['outer_observation_us']} | "
            f"{summary['observation_timing_us']['observation_hash']['total_us']} |"
        )
    lines.extend([
        "",
        "## Median and range",
        "",
        "| Metric | Control median | Experiment median | Control min–max | Experiment min–max |",
        "|---|---:|---:|---:|---:|",
        f"| Worker-local simulation (us) | {format_metric(decision.get('medians', {}).get('worker_local_simulation_us', {}).get('control', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('worker_local_simulation_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('medians', {}).get('worker_local_simulation_us', {}).get('experiment', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('worker_local_simulation_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('worker_local_simulation_us', {}).get('control_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('worker_local_simulation_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('worker_local_simulation_us', {}).get('control_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('worker_local_simulation_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('worker_local_simulation_us', {}).get('experiment_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('worker_local_simulation_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('worker_local_simulation_us', {}).get('experiment_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('worker_local_simulation_us'), Mapping) else 'NOT_RUN')} |",
        f"| Games/s | {format_metric(decision.get('medians', {}).get('games_per_second', {}).get('control', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('games_per_second'), Mapping) else 'NOT_RUN', True)} | {format_metric(decision.get('medians', {}).get('games_per_second', {}).get('experiment', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('games_per_second'), Mapping) else 'NOT_RUN', True)} | {format_metric(decision.get('ranges', {}).get('games_per_second', {}).get('control_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('games_per_second'), Mapping) else 'NOT_RUN', True)}–{format_metric(decision.get('ranges', {}).get('games_per_second', {}).get('control_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('games_per_second'), Mapping) else 'NOT_RUN', True)} | {format_metric(decision.get('ranges', {}).get('games_per_second', {}).get('experiment_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('games_per_second'), Mapping) else 'NOT_RUN', True)}–{format_metric(decision.get('ranges', {}).get('games_per_second', {}).get('experiment_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('games_per_second'), Mapping) else 'NOT_RUN', True)} |",
        f"| Serializer (us) | {format_metric(decision.get('medians', {}).get('serializer_us', {}).get('control', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('serializer_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('medians', {}).get('serializer_us', {}).get('experiment', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('serializer_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('serializer_us', {}).get('control_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('serializer_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('serializer_us', {}).get('control_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('serializer_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('serializer_us', {}).get('experiment_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('serializer_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('serializer_us', {}).get('experiment_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('serializer_us'), Mapping) else 'NOT_RUN')} |",
        f"| Observation (us) | {format_metric(decision.get('medians', {}).get('observation_us', {}).get('control', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('observation_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('medians', {}).get('observation_us', {}).get('experiment', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('observation_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('observation_us', {}).get('control_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('observation_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('observation_us', {}).get('control_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('observation_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('observation_us', {}).get('experiment_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('observation_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('observation_us', {}).get('experiment_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('observation_us'), Mapping) else 'NOT_RUN')} |",
        f"| Observation hash (us) | {format_metric(decision.get('medians', {}).get('hash_us', {}).get('control', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('hash_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('medians', {}).get('hash_us', {}).get('experiment', 'NOT_RUN') if isinstance(decision.get('medians', {}).get('hash_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('hash_us', {}).get('control_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('hash_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('hash_us', {}).get('control_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('hash_us'), Mapping) else 'NOT_RUN')} | {format_metric(decision.get('ranges', {}).get('hash_us', {}).get('experiment_min', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('hash_us'), Mapping) else 'NOT_RUN')}–{format_metric(decision.get('ranges', {}).get('hash_us', {}).get('experiment_max', 'NOT_RUN') if isinstance(decision.get('ranges', {}).get('hash_us'), Mapping) else 'NOT_RUN')} |",
        "",
        f"- Serializer median speedup: `{decision.get('speedup_percent', {}).get('serializer_us', 'NOT_RUN'):.6f}%`.",
        f"- Worker median speedup: `{decision.get('speedup_percent', {}).get('worker_local_simulation_us', 'NOT_RUN'):.6f}%`.",
        f"- Paired serializer improvements: `{decision.get('paired_serializer_improvements', 'NOT_RUN')}/3`.",
        f"- Paired worker improvements: `{decision.get('paired_worker_improvements', 'NOT_RUN')}/3`.",
        "",
        "## Structural closure",
        "",
        f"- Observations: `{EXPECTED_OBSERVATIONS}`.",
        f"- Canonical-without-hash bytes: `{EXPECTED_CANONICAL_BYTES}`.",
        "- Serialization calls, SHA calls, query/entity/candidate/script counters are required identical across all repetitions.",
        "- `canonical_serialize()` consumption in THROUGHPUT remains zero.",
        "",
        "## Regression gates",
        "",
        "| Gate | Status | Evidence |",
        "|---|---|---|",
        f"| Full CTest — control | **{gate_status('full_ctest_control')}** | `{gates.get('full_ctest_control', {}).get('artifact', 'NOT_RECORDED') if isinstance(gates.get('full_ctest_control'), Mapping) else 'NOT_RECORDED'}` |",
        f"| Full CTest — experiment | **{gate_status('full_ctest_experiment')}** | `{gates.get('full_ctest_experiment', {}).get('artifact', 'NOT_RECORDED') if isinstance(gates.get('full_ctest_experiment'), Mapping) else 'NOT_RECORDED'}` |",
        f"| Repository Python | **{gate_status('repository_python')}** | `{gates.get('repository_python', {}).get('artifact', 'NOT_RECORDED') if isinstance(gates.get('repository_python'), Mapping) else 'NOT_RECORDED'}` |",
        f"| M3 Python | **{gate_status('m3_python')}** | `{gates.get('m3_python', {}).get('artifact', 'NOT_RECORDED') if isinstance(gates.get('m3_python'), Mapping) else 'NOT_RECORDED'}` |",
        f"| M4 Python | **{gate_status('m4_python')}** | `{gates.get('m4_python', {}).get('artifact', 'NOT_RECORDED') if isinstance(gates.get('m4_python'), Mapping) else 'NOT_RECORDED'}` |",
        f"| Privacy — control / experiment | **{gate_status('privacy_control')} / {gate_status('privacy_experiment')}** | focused fixtures plus full CTest |",
        f"| Candidate/observation consistency | **{gate_status('candidate_observation_consistency')}** | full CTest / worker structural counters |",
        f"| Fixed-deck regression — control / experiment | **{gate_status('canonical_fixed_deck_regression')}** | fixed-deck artifacts |",
        f"| Deterministic worker gate | **{gate_status('deterministic_worker_gate')}** | worker conformance and M3 determinism artifacts |",
        "",
    ])
    m4_runs = report.get("regression_evidence", {}).get("m4_python_runs", {})
    if m4_runs:
        lines.extend([
            "The first full M4 Python invocation had one scheduling-sensitive failure in `test_result_then_exit_never_publishes_passed_under_repeated_scheduling`; the isolated retry passed, and the complete second invocation passed. The first failure is retained as evidence and is not promoted to a gate pass.",
            "",
            "| M4 Python evidence | Result |",
            "|---|---|",
            f"| First full invocation | **{m4_runs.get('first_full_run', {}).get('status', 'NOT_RECORDED')}** ({m4_runs.get('first_full_run', {}).get('tests', 'NOT_RECORDED')} tests) |",
            f"| Isolated retry | **{m4_runs.get('isolated_retry', {}).get('status', 'NOT_RECORDED')}** |",
            f"| Second full invocation | **{m4_runs.get('second_full_run', {}).get('status', 'NOT_RECORDED')}** ({m4_runs.get('second_full_run', {}).get('tests', 'NOT_RECORDED')} tests, {m4_runs.get('second_full_run', {}).get('skipped', 'NOT_RECORDED')} skipped) |",
            "",
        ])
    remaining = report.get("remaining_release_observation_buckets", [])
    if remaining:
        lines.extend([
            "## Remaining Release observation buckets",
            "",
            "The direct writer removes the ostream primitive-rendering bottleneck in this A/B. The next measured buckets are reported descriptively; no follow-up optimization is started here.",
            "",
            "| Rank | Bucket | Median us | Fraction of experiment observation median |",
            "|---:|---|---:|---:|",
        ])
        for row in remaining:
            lines.append(
                f"| {row.get('rank', 'NOT_RECORDED')} | `{row.get('bucket', 'NOT_RECORDED')}` | "
                f"{row.get('median_us', 'NOT_RECORDED')} | {row.get('fraction_of_observation_median_percent', 'NOT_RECORDED'):.3f}% |"
            )
        lines.append("")
    lines.extend([
        "## Acceptance",
        "",
        f"**{report['status']}**. The M4.3.5 benchmark was not rerun. The direct writer remains an internal opt-in build experiment; the default build remains on the restored control serializer until a separate integration decision.",
        "",
        "## Scope boundary",
        "",
        "No visible-event history change, schema change, event delta, hash change, query optimization, ocgcore change, or M5 work was performed.",
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
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-markdown", required=True, type=Path)
    args = parser.parse_args(arguments)
    args.run_root.mkdir(parents=True, exist_ok=True)

    focused_tests = _pair_tests(
        args.control_build_dir,
        args.experiment_build_dir,
        args.run_root / "fixture_byte_dumps",
    )
    conformance_json = args.run_root / "worker_conformance.json"
    conformance_command = [
        sys.executable,
        "tools/m4/compare_build_modes.py",
        "--debug-worker", str(args.control_worker.resolve()),
        "--release-worker", str(args.experiment_worker.resolve()),
        "--master-seed", str(MASTER_SEED),
        "--games", str(GAMES),
        "--max-steps", str(MAX_STEPS),
        "--output-dir", str((args.run_root / "conformance").resolve()),
        "--output", str(conformance_json.resolve()),
    ]
    conformance_run = _run(conformance_command)
    worker_conformance = json.loads(conformance_json.read_text(encoding="utf-8")) if conformance_json.is_file() else {}
    worker_comparison = worker_conformance.get("comparison", {})
    equivalence_pass = focused_tests["pass"] and worker_comparison.get("pass") is True

    repetitions: list[dict[str, Any]] = []
    operation_reference: dict[str, int] | None = None
    lifecycle_reference: dict[str, int] | None = None
    error_reference: dict[str, int] | None = None
    if equivalence_pass:
        for label, variant, worker in (
            ("A1", "control", args.control_worker),
            ("B1", "experiment", args.experiment_worker),
            ("A2", "control", args.control_worker),
            ("B2", "experiment", args.experiment_worker),
            ("A3", "control", args.control_worker),
            ("B3", "experiment", args.experiment_worker),
        ):
            sample_dir = args.run_root / "throughput" / label
            sample = run_audit_sample(
                worker,
                master_seed=MASTER_SEED,
                games=GAMES,
                max_steps=MAX_STEPS,
                observation_mode=OBSERVATION_MODE,
                output_dir=sample_dir,
                require_primary_integrity=True,
            )
            summary = _summarize_sample(sample)
            if operation_reference is None:
                operation_reference = summary["operation_counters"]
                lifecycle_reference = summary["lifecycle"]
                error_reference = summary["error_counters"]
            else:
                if summary["operation_counters"] != operation_reference:
                    raise ValueError(f"operation counters changed in {label}")
                if summary["lifecycle"] != lifecycle_reference:
                    raise ValueError(f"serialization lifecycle changed in {label}")
                if summary["error_counters"] != error_reference:
                    raise ValueError(f"error counters changed in {label}")
            result_evidence = sample_dir / "worker-results.json"
            result_evidence.write_text(
                json.dumps(sample["results"], ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            summary["worker_result_evidence"] = {
                "path": str(result_evidence.resolve()),
                "bytes": result_evidence.stat().st_size,
                "sha256": _sha256_file(result_evidence),
            }
            repetitions.append({"label": label, "variant": variant, "summary": summary})

    decision = _timing_decision(repetitions) if repetitions else {"pass": False, "reason": "equivalence gates did not pass"}
    if not focused_tests["pass"]:
        status = "M4.3.6 REJECTED — CANONICAL BYTE DIVERGENCE"
    elif worker_comparison.get("pass") is not True:
        status = "M4.3.6 REJECTED — SEMANTIC DIVERGENCE"
    elif repetitions and not decision["pass"]:
        status = "M4.3.6 REJECTED — NO MATERIAL BENEFIT (REGRESSION GATES PENDING)"
    else:
        status = "M4.3.6 PERFORMANCE EVIDENCE READY — REGRESSION GATES PENDING"

    diff_check = _run(["git", "diff", "--check"])
    report: dict[str, Any] = {
        "schema": "ocgforge.m4.m4_3_6_direct_canonical_writer.v1",
        "status": status,
        "repository": {
            "starting_head": _run(["git", "rev-parse", "HEAD"])["stdout"].strip(),
            "worktree_status": _run(["git", "status", "--short"])["stdout"],
            "git_diff_check_pass": diff_check["exit_code"] == 0,
            "m4_3_5_report_preserved": all(
                (_REPO_ROOT / path).is_file()
                for path in (
                    "docs/m4/M4_3_5_RESERVE_BACKED_SERIALIZATION.md",
                    "docs/m4/m4_3_5_reserve_backed_serialization.json",
                )
            ),
        },
        "builds": {
            "control": _build_identity(
                args.control_worker,
                args.control_build_dir,
                expected_direct=False,
                ready=repetitions[0]["summary"]["ready"] if repetitions else [],
            ),
            "experiment": _build_identity(
                args.experiment_worker,
                args.experiment_build_dir,
                expected_direct=True,
                ready=repetitions[1]["summary"]["ready"] if len(repetitions) > 1 else [],
            ),
            "same_source_release_policy": "Clang 19.1.7 via Zig 0.14.1; ordinary -O3 -DNDEBUG; no LTO/PGO/CPU-specific flags",
            "shape_instrumentation_used_for_timing": False,
        },
        "workload": {
            "matchup": "Swordsoul Tenyi ML v1 vs Salamangreat ML v1",
            "master_seed": MASTER_SEED,
            "games": GAMES,
            "workers": WORKERS,
            "max_steps": MAX_STEPS,
            "observation_mode": OBSERVATION_MODE,
            "mode": THROUGHPUT_MODE,
            "trace_persistence": TRACE_PERSISTENCE,
        },
        "equivalence": {
            "focused_tests": focused_tests,
            "worker_conformance": {
                "pass": worker_comparison.get("pass") is True,
                "runner": {
                    "command": conformance_command,
                    "exit_code": conformance_run["exit_code"],
                    "stdout_sha256": conformance_run["stdout_sha256"],
                    "stderr_sha256": conformance_run["stderr_sha256"],
                },
                "comparison": worker_comparison,
                "artifact": str(conformance_json.resolve()),
            },
        },
        "repetitions": repetitions,
        "timing_decision": decision,
        "structural_reference": {
            "observations": EXPECTED_OBSERVATIONS,
            "serialize_without_hash_calls": EXPECTED_OBSERVATIONS,
            "serialize_without_hash_bytes": EXPECTED_CANONICAL_BYTES,
            "sha256_calls": EXPECTED_OBSERVATIONS,
            "canonical_serialize_calls": 0,
            "canonical_serialize_bytes": 0,
            "same_mutation_epoch_duplicate_calls": 0,
        },
        "gates": {
            "full_ctest_control": "NOT_RUN",
            "full_ctest_experiment": "NOT_RUN",
            "repository_python": "NOT_RUN",
            "m3_python": "NOT_RUN",
            "m4_python": "NOT_RUN",
            "privacy_control": "PASS" if focused_tests["pass"] else "FAIL",
            "privacy_experiment": "PASS" if focused_tests["pass"] else "FAIL",
            "candidate_observation_consistency": "NOT_RUN",
            "canonical_fixed_deck_regression": "NOT_RUN",
            "deterministic_worker_gate": "PASS" if worker_comparison.get("pass") is True else "FAIL",
        },
        "acceptance_rule": {
            "requires_byte_equivalence": True,
            "requires_semantic_and_privacy_equivalence": True,
            "requires_material_timing": True,
            "materiality": decision.get("materiality_rule", {}),
        },
        "m4_3_5_benchmark_rerun": False,
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
    args.output_markdown.write_text(_markdown(report), encoding="utf-8")
    print(json.dumps({"status": status, "timing_decision": decision}, ensure_ascii=False, sort_keys=True))
    return 0 if equivalence_pass and bool(repetitions) else 1


if __name__ == "__main__":
    raise SystemExit(main())
