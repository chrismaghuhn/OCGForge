"""M4 benchmark integrity, aggregation, and CLI contract helpers.

The coordinator owns process lifecycle and receipt timing.  This module only
turns already-published value results into a versioned report and refuses to
describe an incomplete or corrupted run as benchmark evidence.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import platform
from typing import Any, Iterable, Mapping, Sequence

from tools.m4.evidence_packaging import evidence_sha256, write_text_lf


REPORT_SCHEMA_VERSION = "ocgforge.m4.throughput_benchmark.v1"
NOT_MEASURED = "NOT_MEASURED"

NATIVE_ERROR_KEYS = (
    "retries",
    "unsupported",
    "automatic",
    "truncated",
    "core_errors",
    "worker_errors",
)
COORDINATOR_ERROR_KEYS = (
    "retries",
    "handshake",
    "malformed_protocol",
    "failed_games",
    "worker_crashes",
    "worker_restarts",
)
TIMING_KEYS = (
    "core_process",
    "protocol_candidate",
    "continuation",
    "observation",
    "trace_hash",
    "serialization",
    "other",
    "trace_persistence",
)
COUNTER_KEYS = (
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

BASELINE_SCHEMA_VERSION = "ocgforge.m4.baseline.v1"
ACCEPTANCE_EVIDENCE_SCHEMA_VERSION = "ocgforge.m4.acceptance.v1"
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MATRIX_WORKERS = (1, 2, 4, 8, 16, 32)
DEFAULT_OPTIONAL_WORKERS = (64, 128)
MATRIX_GAMES = 64
MATRIX_WARMUP_GAMES = 4
MATRIX_MASTER_SEED = 20260815
FINAL_ACCEPTANCE_GATES = (
    "parallel_determinism",
    "mode_equivalence",
    "failure_isolation",
    "handshake_identity",
    "integrity",
    "existing_regressions",
    "privacy",
    "candidate_observation",
    "final_build_and_ctest",
)
# The finalized M4 branch includes the two direct-writer equivalence fixtures
# in addition to the 92-test foundation suite.
FINAL_CTEST_EXPECTED_TOTAL = 94
SEMANTIC_RESULT_FIELDS = (
    "job_id",
    "terminal",
    "winner",
    "win_reason",
    "engine_steps",
    "interactive_decisions",
    "semantic_action_count",
    "gameplay_hash",
    "errors",
)


class BenchmarkIntegrityError(ValueError):
    """Raised when a run cannot be used as primary benchmark evidence."""


def _sha256_file(path: Path) -> str:
    return evidence_sha256(path)


def _repository_relative_path(path: Path) -> str | None:
    try:
        return path.resolve().relative_to(REPOSITORY_ROOT.resolve()).as_posix()
    except ValueError:
        return None


def _source_report_label(path: Path | None) -> str:
    if path is None:
        return "IN_MEMORY"
    return _repository_relative_path(path) or "EXTERNAL_SOURCE_NOT_COMMITTED"


def _resolve_repository_file(value: Any) -> Path | None:
    if not isinstance(value, str) or not value.strip():
        return None
    candidate = Path(value)
    if candidate.is_absolute():
        return None
    resolved = (REPOSITORY_ROOT / candidate).resolve()
    if _repository_relative_path(resolved) is None or not resolved.is_file():
        return None
    return resolved


def _nonnegative_int(value: str) -> int:
    try:
        parsed = int(value, 10)
    except ValueError as error:
        raise argparse.ArgumentTypeError("must be an integer") from error
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be nonnegative")
    return parsed


def _positive_int(value: str) -> int:
    parsed = _nonnegative_int(value)
    if parsed == 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def _observation_mode(value: str) -> str:
    if value == "full":
        return "full"
    if value == "off-diagnostic":
        return "off_diagnostic"
    raise argparse.ArgumentTypeError("must be full or off-diagnostic")


def _trace_persistence(value: str) -> bool:
    if value == "on":
        return True
    if value == "off":
        return False
    raise argparse.ArgumentTypeError("must be on or off")


def build_argument_parser() -> argparse.ArgumentParser:
    """Build the exact Task-6 benchmark command-line interface."""

    parser = argparse.ArgumentParser(description="Run the OCGForge M4 benchmark.")
    parser.add_argument("--worker-executable", required=True, type=Path)
    parser.add_argument("--games", required=True, type=_positive_int)
    parser.add_argument("--workers", required=True, type=_positive_int)
    parser.add_argument("--master-seed", required=True, type=int)
    parser.add_argument("--mode", required=True, choices=("conformance", "throughput"))
    parser.add_argument("--warmup-games", default=0, type=_nonnegative_int)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--starting-player-mode", choices=("balanced",), default="balanced")
    parser.add_argument("--seat-mode", choices=("balanced",), default="balanced")
    parser.add_argument("--instrument", action="store_true")
    parser.add_argument(
        "--observation-mode",
        default="full",
        type=_observation_mode,
        metavar="full|off-diagnostic",
    )
    parser.add_argument(
        "--trace-persistence",
        default=False,
        type=_trace_persistence,
        metavar="on|off",
    )
    parser.add_argument(
        "--result-timeout-seconds",
        default=120.0,
        type=float,
        metavar="SECONDS",
        help="maximum time allowed for one in-flight worker result (default: 120)",
    )
    return parser


def _require_nonnegative_number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BenchmarkIntegrityError(f"{name} must be a nonnegative number")
    converted = float(value)
    if not math.isfinite(converted) or converted < 0:
        raise BenchmarkIntegrityError(f"{name} must be a finite nonnegative number")
    return converted


def _require_counter_mapping(
    value: Any,
    keys: Sequence[str],
    name: str,
) -> Mapping[str, Any]:
    if not isinstance(value, Mapping) or set(value) != set(keys):
        raise BenchmarkIntegrityError(f"{name} has the wrong counter keys")
    for key in keys:
        number = value[key]
        if isinstance(number, bool) or not isinstance(number, int) or number < 0:
            raise BenchmarkIntegrityError(f"{name}.{key} is not a nonnegative integer")
    return value


def _is_sha256(value: Any) -> bool:
    if not isinstance(value, str) or len(value) != 64:
        return False
    return all(character in "0123456789abcdefABCDEF" for character in value)


def _require_zero_counter_mapping(value: Any, keys: Sequence[str], name: str) -> None:
    counters = _require_counter_mapping(value, keys, name)
    nonzero = [key for key in keys if counters[key] != 0]
    if nonzero:
        raise BenchmarkIntegrityError(
            f"{name} contains nonzero integrity counters: {', '.join(nonzero)}"
        )


def _validate_result(result: Mapping[str, Any], expected_job_id: str) -> None:
    if not isinstance(result, Mapping):
        raise BenchmarkIntegrityError("benchmark result is not an object")
    if result.get("job_id") != expected_job_id:
        raise BenchmarkIntegrityError(
            f"unexpected result job ID: {result.get('job_id')!r}; expected {expected_job_id!r}"
        )
    if result.get("status") != "passed" or result.get("terminal") is not True:
        raise BenchmarkIntegrityError(f"job {expected_job_id} is not a passed terminal game")
    _require_zero_counter_mapping(result.get("errors"), NATIVE_ERROR_KEYS, f"{expected_job_id}.errors")
    _require_zero_counter_mapping(
        result.get("coordinator_errors"),
        COORDINATOR_ERROR_KEYS,
        f"{expected_job_id}.coordinator_errors",
    )
    simulation_elapsed = result.get("simulation_elapsed_us")
    if isinstance(simulation_elapsed, bool) or not isinstance(simulation_elapsed, int) or simulation_elapsed <= 0:
        raise BenchmarkIntegrityError(f"job {expected_job_id} lacks positive simulation timing")
    coordinator_elapsed = result.get("coordinator_elapsed_us")
    if isinstance(coordinator_elapsed, bool) or not isinstance(coordinator_elapsed, int) or coordinator_elapsed < 0:
        raise BenchmarkIntegrityError(f"job {expected_job_id} lacks coordinator timing")
    if not _is_sha256(result.get("gameplay_hash")):
        raise BenchmarkIntegrityError(f"job {expected_job_id} lacks a gameplay equivalence hash")
    worker = result.get("worker")
    if not isinstance(worker, Mapping) or worker.get("crashed") is not False or worker.get("restarted") is not False:
        raise BenchmarkIntegrityError(f"job {expected_job_id} has worker lifecycle failure metadata")
    coordinator = result.get("coordinator")
    if not isinstance(coordinator, Mapping) or coordinator.get("worker_crashed") is not False or coordinator.get("worker_restarted") is not False:
        raise BenchmarkIntegrityError(f"job {expected_job_id} has coordinator lifecycle failure metadata")


def validate_complete_results(
    results: Sequence[Mapping[str, Any]],
    expected_job_ids: Sequence[str],
    metadata: Mapping[str, Any],
    *,
    expected_gameplay_hashes: Mapping[str, str] | None = None,
    require_trace_hash: bool = False,
) -> None:
    """Reject incomplete, failed, nonterminal, or semantically divergent rows."""

    expected = list(expected_job_ids)
    if len(expected) != len(set(expected)):
        raise BenchmarkIntegrityError("expected job IDs are not unique")
    actual = [result.get("job_id") if isinstance(result, Mapping) else None for result in results]
    duplicates = sorted({job_id for job_id in actual if actual.count(job_id) > 1})
    if duplicates:
        raise BenchmarkIntegrityError(f"duplicate result job IDs: {duplicates}")
    if set(actual) != set(expected) or len(actual) != len(expected):
        missing = sorted(set(expected).difference(actual))
        unexpected = sorted(set(actual).difference(expected))
        raise BenchmarkIntegrityError(f"result ID set mismatch: missing={missing}, unexpected={unexpected}")

    required_metadata_keys = (
        "handshake_errors",
        "malformed_protocol",
        "worker_crashes",
        "worker_restarts",
        "retries",
        "failed_games",
        "worker_errors",
    )
    for key in required_metadata_keys:
        value = metadata.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value != 0:
            raise BenchmarkIntegrityError(f"coordinator metadata has nonzero {key}")

    by_id = {result["job_id"]: result for result in results}
    for job_id in expected:
        result = by_id[job_id]
        _validate_result(result, job_id)
        if require_trace_hash and not _is_sha256(result.get("trace_hash")):
            raise BenchmarkIntegrityError(f"job {job_id} lacks a required trace hash")
        if expected_gameplay_hashes is not None:
            expected_hash = expected_gameplay_hashes.get(job_id)
            if not _is_sha256(expected_hash) or result.get("gameplay_hash") != expected_hash:
                raise BenchmarkIntegrityError(f"job {job_id} has a mismatched gameplay equivalence hash")


def percentile_summary(values: Iterable[int]) -> dict[str, int | float | str]:
    """Return mean and exact p50/p95/p99 values from the specified index rule."""

    samples = list(values)
    for value in samples:
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError("percentile samples must be nonnegative integers")
    if not samples:
        return {
            "sample_count": 0,
            "mean": NOT_MEASURED,
            "min": NOT_MEASURED,
            "p50": NOT_MEASURED,
            "p95": NOT_MEASURED,
            "p99": NOT_MEASURED,
            "max": NOT_MEASURED,
        }
    ordered = sorted(samples)

    def at(quantile: float) -> int:
        index = min(len(ordered) - 1, math.ceil(quantile * len(ordered)) - 1)
        return ordered[index]

    total = sum(ordered)
    mean: int | float = total // len(ordered) if total % len(ordered) == 0 else total / len(ordered)
    return {
        "sample_count": len(ordered),
        "mean": mean,
        "min": ordered[0],
        "p50": at(0.50),
        "p95": at(0.95),
        "p99": at(0.99),
        "max": ordered[-1],
    }


def _sum_field(results: Sequence[Mapping[str, Any]], field: str) -> int:
    total = 0
    for result in results:
        value = result.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise BenchmarkIntegrityError(f"result field {field} is not a nonnegative integer")
        total += value
    return total


def _sum_nested(results: Sequence[Mapping[str, Any]], field: str, keys: Sequence[str]) -> dict[str, int]:
    totals = {key: 0 for key in keys}
    for result in results:
        values = _require_counter_mapping(result.get(field), keys, field)
        for key in keys:
            totals[key] += int(values[key])
    return totals


def aggregate_results(
    results: Sequence[Mapping[str, Any]],
    metadata: Mapping[str, Any],
    *,
    wall_clock_seconds: float,
    games_requested: int | None = None,
    workers_requested: int | None = None,
    one_worker_games_per_second: float | None = None,
) -> dict[str, Any]:
    """Aggregate valid rows without mixing worker and coordinator time domains."""

    wall_seconds = _require_nonnegative_number(wall_clock_seconds, "wall_clock_seconds")
    if games_requested is None:
        games_requested = len(results)
    if isinstance(games_requested, bool) or not isinstance(games_requested, int) or games_requested < 0:
        raise ValueError("games_requested must be a nonnegative integer")
    if workers_requested is not None and (
        isinstance(workers_requested, bool) or not isinstance(workers_requested, int) or workers_requested <= 0
    ):
        raise ValueError("workers_requested must be a positive integer")

    native_errors = _sum_nested(results, "errors", NATIVE_ERROR_KEYS)
    coordinator_errors = _sum_nested(results, "coordinator_errors", COORDINATOR_ERROR_KEYS)
    timing = _sum_nested(results, "timing_us", TIMING_KEYS)
    counters = _sum_nested(results, "counters", COUNTER_KEYS)
    dispatch_to_receipt = _sum_field(results, "coordinator_elapsed_us")
    simulation_values = _sum_field(results, "simulation_elapsed_us")
    simulation_samples = [int(result["simulation_elapsed_us"]) for result in results]
    coordinator_samples = [int(result["coordinator_elapsed_us"]) for result in results]

    def rate(total: int) -> float:
        return total / wall_seconds if wall_seconds > 0 else 0.0

    games_completed = len(results)
    games_per_second = rate(games_completed)
    speedup: float | int | str = NOT_MEASURED
    efficiency: float | int | str = NOT_MEASURED
    if one_worker_games_per_second is not None:
        baseline = _require_nonnegative_number(one_worker_games_per_second, "one_worker_games_per_second")
        if baseline > 0:
            speedup = games_per_second / baseline
            if workers_requested is not None:
                efficiency = speedup / workers_requested
    elif workers_requested == 1 and games_per_second > 0:
        speedup = 1.0
        efficiency = 1.0

    timing_buckets = dict(timing)
    timing_buckets["dispatch_to_receipt"] = dispatch_to_receipt
    timing_buckets["coordinator_other"] = 0
    return {
        "games_requested": games_requested,
        "games_completed": games_completed,
        "terminal_games": sum(result.get("terminal") is True for result in results),
        "failed_games": sum(result.get("status") == "failed" for result in results),
        "wall_clock_seconds": wall_seconds,
        "games_per_second": games_per_second,
        "engine_steps_total": _sum_field(results, "engine_steps"),
        "engine_steps_per_second": rate(_sum_field(results, "engine_steps")),
        "interactive_decisions_total": _sum_field(results, "interactive_decisions"),
        "interactive_decisions_per_second": rate(_sum_field(results, "interactive_decisions")),
        "semantic_actions_total": _sum_field(results, "semantic_action_count"),
        "simulation_elapsed_us": percentile_summary(simulation_samples),
        "coordinator_elapsed_us": percentile_summary(coordinator_samples),
        "memory": metadata.get("memory", {
            "process_count": NOT_MEASURED,
            "peak_total_working_set_bytes": NOT_MEASURED,
            "peak_worker_working_set_bytes": NOT_MEASURED,
            "memory_per_active_environment_bytes": NOT_MEASURED,
        }),
        "errors": {
            **native_errors,
            "coordinator": coordinator_errors,
        },
        "timing_buckets_us": timing_buckets,
        "operation_counters": counters,
        "speedup": speedup,
        "parallel_efficiency": efficiency,
        "simulation_elapsed_total_us": simulation_values,
    }


def _load_baseline_report(
    value: Mapping[str, Any] | str | Path,
) -> tuple[dict[str, Any], Path | None]:
    if isinstance(value, Mapping):
        return _normalize_legacy_timing_labels(value), None
    path = Path(value)
    try:
        loaded = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BenchmarkIntegrityError(f"could not read benchmark report {path}: {error}") from error
    if not isinstance(loaded, Mapping):
        raise BenchmarkIntegrityError(f"benchmark report {path} is not an object")
    return _normalize_legacy_timing_labels(loaded), path


def _normalize_legacy_timing_labels(report: Mapping[str, Any]) -> dict[str, Any]:
    """Normalize pre-review aggregate labels before schema validation.

    M4 matrix rows produced before the review fix used the derived label
    ``coordinator_ipc``. Those rows are accepted only as historical input;
    newly generated reports and baseline evidence use the precise end-to-end
    ``dispatch_to_receipt`` domain.
    """

    steady_state = report.get("steady_state")
    if not isinstance(steady_state, Mapping):
        return dict(report)
    timing = steady_state.get("timing_buckets_us")
    percentages = steady_state.get("timing_percentages")
    if not isinstance(timing, Mapping):
        return dict(report)
    if "coordinator_ipc" not in timing and (
        not isinstance(percentages, Mapping) or "coordinator_ipc" not in percentages
    ):
        return dict(report)
    if "dispatch_to_receipt" in timing or (
        isinstance(percentages, Mapping) and "dispatch_to_receipt" in percentages
    ):
        raise BenchmarkIntegrityError("report contains both legacy and current coordinator timing labels")
    normalized = dict(report)
    normalized_steady = dict(steady_state)
    normalized_timing = dict(timing)
    normalized_percentages = dict(percentages) if isinstance(percentages, Mapping) else None
    normalized_timing["dispatch_to_receipt"] = normalized_timing.pop("coordinator_ipc", 0)
    normalized_steady["timing_buckets_us"] = normalized_timing
    if normalized_percentages is not None:
        normalized_percentages["dispatch_to_receipt"] = normalized_percentages.pop("coordinator_ipc", 0)
        normalized_steady["timing_percentages"] = normalized_percentages
    normalized["steady_state"] = normalized_steady
    return normalized


def _validate_declared_benchmark_schema(report: Mapping[str, Any]) -> None:
    schema_path = Path(__file__).resolve().parents[2] / "docs" / "m4" / "m4_benchmark_schema.json"
    try:
        schema = json.loads(schema_path.read_text(encoding="utf-8"))
        from jsonschema import Draft202012Validator
    except (OSError, json.JSONDecodeError, ImportError) as error:
        raise BenchmarkIntegrityError(f"declared benchmark schema cannot be loaded: {error}") from error
    errors = sorted(Draft202012Validator(schema).iter_errors(report), key=lambda error: list(error.path))
    if errors:
        raise BenchmarkIntegrityError(f"declared benchmark schema rejected row: {errors[0].message}")


def _positive_baseline_number(value: Any, name: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise BenchmarkIntegrityError(f"{name} must be a positive number")
    converted = float(value)
    if not math.isfinite(converted) or converted <= 0:
        raise BenchmarkIntegrityError(f"{name} must be a positive number")
    return converted


def _baseline_stderr_bytes(
    report: Mapping[str, Any],
) -> int:
    jobs = report.get("jobs")
    if not isinstance(jobs, Sequence) or isinstance(jobs, (str, bytes)):
        raise BenchmarkIntegrityError("jobs is not a list")
    total = 0
    for job in jobs:
        if not isinstance(job, Mapping):
            raise BenchmarkIntegrityError("job is not an object")
        coordinator = job.get("coordinator")
        stderr_path = coordinator.get("stderr_path") if isinstance(coordinator, Mapping) else None
        if not isinstance(stderr_path, str) or not stderr_path or stderr_path == NOT_MEASURED:
            raise BenchmarkIntegrityError(f"job {job.get('job_id')!r} lacks stderr evidence")
        path = Path(stderr_path)
        try:
            total += path.stat().st_size
        except OSError as error:
            raise BenchmarkIntegrityError(
                f"stderr evidence is unavailable for job {job.get('job_id')!r}: {stderr_path}"
            ) from error
    if total != 0:
        raise BenchmarkIntegrityError(f"worker stderr is non-empty: {total} bytes")
    return total


def _validate_matrix_report(
    report: Mapping[str, Any],
    workers: int,
    *,
    reference: Mapping[str, Any] | None = None,
) -> int:
    if not isinstance(report, Mapping):
        raise BenchmarkIntegrityError("matrix row is not an object")
    if report.get("schema_version") != REPORT_SCHEMA_VERSION:
        raise BenchmarkIntegrityError("matrix row has an unexpected schema version")
    if (
        report.get("mode") != "throughput"
        or report.get("observation_mode", "full") != "full"
        or report.get("instrumentation") is not True
        or report.get("trace_persistence") is not False
    ):
        raise BenchmarkIntegrityError("matrix row is not full-observation throughput")
    if report.get("workers_requested") != workers:
        raise BenchmarkIntegrityError("matrix row worker identity mismatch")
    if report.get("games_requested") != MATRIX_GAMES:
        raise BenchmarkIntegrityError("matrix row does not contain 64 requested games")

    warmup = report.get("warmup_policy")
    expected_warmup = {
        "warmup_games": MATRIX_WARMUP_GAMES,
        "master_seed": MATRIX_MASTER_SEED,
        "starting_player_mode": "balanced",
        "seat_mode": "balanced",
        "same_master_seed_for_warmup_and_steady": True,
    }
    if not isinstance(warmup, Mapping) or any(warmup.get(key) != value for key, value in expected_warmup.items()):
        raise BenchmarkIntegrityError("matrix row warmup/seed/policy mismatch")

    canonical_environment = report.get("canonical_environment")
    build = report.get("build")
    hardware = report.get("hardware")
    steady = report.get("steady_state")
    if not isinstance(canonical_environment, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks canonical environment")
    if not isinstance(build, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks build identity")
    if not isinstance(hardware, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks hardware identity")
    if not isinstance(steady, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks steady-state data")
    if reference is not None:
        if canonical_environment != reference.get("canonical_environment"):
            raise BenchmarkIntegrityError("matrix row canonical environment mismatch")
        reference_build = reference.get("build")
        if not isinstance(reference_build, Mapping):
            raise BenchmarkIntegrityError("reference row lacks build identity")
        identity_keys = (
            "protocol_schema",
            "protocol_version",
            "worker_identity",
            "compiler_identity",
            "build_type",
            "platform",
        )
        if any(build.get(key) != reference_build.get(key) for key in identity_keys):
            raise BenchmarkIntegrityError("matrix row handshake/build identity mismatch")
        reference_warmup = reference.get("warmup_policy")
        if warmup != reference_warmup:
            raise BenchmarkIntegrityError("matrix row warmup policy mismatch")

    for key in ("protocol_schema", "protocol_version", "worker_identity", "compiler_identity", "build_type", "platform"):
        if not isinstance(build.get(key), str) or not build[key]:
            raise BenchmarkIntegrityError(f"matrix row build.{key} is missing")
    _positive_baseline_number(build.get("result_timeout_seconds", 120.0), "result_timeout_seconds")

    physical_memory = hardware.get("physical_memory_bytes")
    if isinstance(physical_memory, bool) or not isinstance(physical_memory, int) or physical_memory <= 0:
        raise BenchmarkIntegrityError("matrix row lacks measured physical memory")

    if steady.get("games_requested") != MATRIX_GAMES:
        raise BenchmarkIntegrityError("matrix steady state has the wrong requested-game count")
    if steady.get("games_completed") != MATRIX_GAMES or steady.get("terminal_games") != MATRIX_GAMES:
        raise BenchmarkIntegrityError("matrix row is incomplete or nonterminal")
    if steady.get("failed_games") != 0:
        raise BenchmarkIntegrityError("matrix row contains failed games")
    errors = steady.get("errors")
    if not isinstance(errors, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks error counters")
    coordinator_errors = errors.get("coordinator")
    if not isinstance(coordinator_errors, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks coordinator error counters")
    for key in NATIVE_ERROR_KEYS:
        if errors.get(key) != 0:
            raise BenchmarkIntegrityError(f"matrix row has nonzero {key}")
    for key in COORDINATOR_ERROR_KEYS:
        if coordinator_errors.get(key) != 0:
            raise BenchmarkIntegrityError(f"matrix row has nonzero coordinator.{key}")

    memory = steady.get("memory")
    if not isinstance(memory, Mapping):
        raise BenchmarkIntegrityError("matrix row lacks memory measurements")
    for key in (
        "process_count",
        "peak_total_working_set_bytes",
        "peak_worker_working_set_bytes",
        "memory_per_active_environment_bytes",
    ):
        value = memory.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise BenchmarkIntegrityError(f"matrix row memory.{key} is not measured")

    jobs = report.get("jobs")
    expected_ids = [f"m4-{index:06d}" for index in range(MATRIX_WARMUP_GAMES, MATRIX_WARMUP_GAMES + MATRIX_GAMES)]
    if not isinstance(jobs, list) or [job.get("job_id") for job in jobs if isinstance(job, Mapping)] != expected_ids:
        raise BenchmarkIntegrityError("matrix row job IDs are missing, duplicated, or out of order")
    metadata = {
        "handshake_errors": coordinator_errors["handshake"],
        "malformed_protocol": coordinator_errors["malformed_protocol"],
        "worker_crashes": coordinator_errors["worker_crashes"],
        "worker_restarts": coordinator_errors["worker_restarts"],
        "retries": coordinator_errors["retries"],
        "failed_games": coordinator_errors["failed_games"],
        "worker_errors": errors["worker_errors"],
    }
    validate_complete_results(jobs, expected_ids, metadata)
    return _baseline_stderr_bytes(report)


def _semantic_baseline_fingerprint(report: Mapping[str, Any]) -> dict[str, tuple[Any, ...]]:
    jobs = report["jobs"]
    fingerprint: dict[str, tuple[Any, ...]] = {}
    for job in jobs:
        values: list[Any] = []
        for field in SEMANTIC_RESULT_FIELDS:
            value = job.get(field)
            if field == "errors" and isinstance(value, Mapping):
                value = tuple(sorted(value.items()))
            values.append(value)
        fingerprint[str(job["job_id"])] = tuple(values)
    return fingerprint


def _timing_percentages(timing: Mapping[str, Any]) -> dict[str, float | str]:
    keys = (
        "core_process",
        "protocol_candidate",
        "continuation",
        "observation",
        "trace_hash",
        "serialization",
        "other",
        "trace_persistence",
    )
    total = sum(int(timing.get(key, 0)) for key in keys)
    if total <= 0:
        native = {key: NOT_MEASURED for key in keys}
    else:
        native = {key: round(float(timing.get(key, 0)) * 100.0 / total, 6) for key in keys}
    coordinator_keys = ("dispatch_to_receipt", "coordinator_other")
    coordinator_total = sum(int(timing.get(key, 0)) for key in coordinator_keys)
    if coordinator_total <= 0:
        coordinator = {key: NOT_MEASURED for key in coordinator_keys}
    else:
        coordinator = {
            key: round(float(timing.get(key, 0)) * 100.0 / coordinator_total, 6)
            for key in coordinator_keys
        }
    return {**native, **coordinator}


def _audit_candidates(
    timing: Mapping[str, Any],
    counters: Mapping[str, Any],
) -> list[dict[str, Any]]:
    candidates: list[dict[str, Any]] = []
    if int(timing.get("observation", 0)) > 0:
        candidates.append({"candidate": "observation-path audit", "basis": "measured observation timing bucket"})
    if int(timing.get("dispatch_to_receipt", 0)) > 0:
        candidates.append(
            {
                "candidate": "dispatch/result latency audit",
                "basis": "measured dispatch_to_receipt timing bucket",
            }
        )
    if int(counters.get("script_loads", 0)) > 0:
        candidates.append({"candidate": "script-load audit", "basis": "measured script_loads counter"})
    if int(counters.get("candidate_total", 0)) > 0:
        candidates.append({"candidate": "candidate-set audit", "basis": "measured candidate_total counter"})
    return candidates


def _baseline_scaling_row(
    report: Mapping[str, Any],
    *,
    stderr_bytes: int,
    baseline_games_per_second: float,
) -> dict[str, Any]:
    steady = report["steady_state"]
    workers = int(report["workers_requested"])
    games_per_second = float(steady["games_per_second"])
    speedup = games_per_second / baseline_games_per_second if baseline_games_per_second > 0 else NOT_MEASURED
    efficiency = speedup / workers if isinstance(speedup, (int, float)) else NOT_MEASURED
    memory = dict(steady["memory"])
    physical = int(report["hardware"]["physical_memory_bytes"])
    memory["physical_memory_bytes"] = physical
    memory["peak_percent_of_physical_memory"] = round(
        float(memory["peak_total_working_set_bytes"]) * 100.0 / physical,
        6,
    )
    timing = dict(steady["timing_buckets_us"])
    counters = dict(steady["operation_counters"])
    return {
        "workers": workers,
        "games": int(steady["games_completed"]),
        "wall_clock_seconds": float(steady["wall_clock_seconds"]),
        "games_per_second": games_per_second,
        "engine_steps_per_second": float(steady["engine_steps_per_second"]),
        "interactive_decisions_per_second": float(steady["interactive_decisions_per_second"]),
        "speedup": speedup,
        "parallel_efficiency": efficiency,
        "simulation_elapsed_us": dict(steady["simulation_elapsed_us"]),
        "memory": memory,
        "errors": {
            **{key: int(steady["errors"][key]) for key in NATIVE_ERROR_KEYS},
            **{f"coordinator_{key}": int(steady["errors"]["coordinator"][key]) for key in COORDINATOR_ERROR_KEYS},
        },
        "timing_buckets_us": timing,
        "timing_percentages": _timing_percentages(timing),
        "operation_counters": counters,
        "stderr_bytes": stderr_bytes,
        "result_timeout_seconds": float(report["build"].get("result_timeout_seconds", 120.0)),
    }


def _baseline_run_identity(
    loaded: Mapping[int, tuple[Mapping[str, Any], Path | None, int]],
    reference_build: Mapping[str, Any],
) -> str | None:
    """Return a digest binding acceptance evidence to the exact validated inputs."""

    schema_path = REPOSITORY_ROOT / "docs" / "m4" / "m4_benchmark_schema.json"
    schema_relative = _repository_relative_path(schema_path)
    if schema_relative is None or not schema_path.is_file():
        return None
    source_manifest: dict[str, dict[str, str]] = {}
    for source_path in (
        REPOSITORY_ROOT / "tools" / "m4" / "report.py",
        REPOSITORY_ROOT / "tools" / "m4" / "benchmark.py",
        REPOSITORY_ROOT / "tools" / "m4" / "worker_protocol_contract.py",
        REPOSITORY_ROOT / "CMakeLists.txt",
    ):
        source_relative = _repository_relative_path(source_path)
        if source_relative is None or not source_path.is_file():
            return None
        source_manifest[source_relative] = {
            "path": source_relative,
            "sha256": _sha256_file(source_path),
        }
    worker_path = _resolve_repository_file(reference_build.get("worker_executable"))
    worker_relative = _repository_relative_path(worker_path) if worker_path is not None else None
    if worker_path is None or worker_relative is None:
        return None
    report_manifest: dict[str, dict[str, str]] = {}
    for workers in sorted(loaded):
        path = loaded[workers][1]
        if path is None or not path.is_file():
            return None
        relative = _repository_relative_path(path)
        if relative is None:
            return None
        report_manifest[str(workers)] = {
            "path": relative,
            "sha256": _sha256_file(path),
        }
    payload = {
        "schema": {
            "path": schema_relative,
            "sha256": _sha256_file(schema_path),
        },
        "sources": source_manifest,
        "reports": report_manifest,
        "build_identity": {
            key: reference_build[key]
            for key in (
                "protocol_schema",
                "protocol_version",
                "worker_identity",
                "compiler_identity",
                "build_type",
                "platform",
            )
        },
        "worker_executable": {
            "path": worker_relative,
            "sha256": _sha256_file(worker_path),
        },
    }
    return hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    ).hexdigest()


def _verified_acceptance_artifacts(
    entry: Mapping[str, Any],
) -> tuple[list[Path], list[dict[str, str]]] | None:
    artifacts = entry.get("artifacts")
    if not isinstance(artifacts, Sequence) or isinstance(artifacts, (str, bytes)) or not artifacts:
        return None
    verified_paths: list[Path] = []
    normalized: list[dict[str, str]] = []
    for artifact in artifacts:
        if not isinstance(artifact, Mapping):
            return None
        path = _resolve_repository_file(artifact.get("path"))
        digest = artifact.get("sha256")
        if path is None or not _is_sha256(digest):
            return None
        try:
            actual = _sha256_file(path)
        except OSError:
            return None
        if actual.lower() != digest.lower():
            return None
        verified_paths.append(path)
        normalized.append(
            {
                "path": _repository_relative_path(path) or "",
                "sha256": actual,
            }
        )
    return verified_paths, normalized


def _acceptance_commands_passed(verification: Mapping[str, Any]) -> bool:
    commands = verification.get("commands")
    if not isinstance(commands, Sequence) or isinstance(commands, (str, bytes)) or not commands:
        return False
    for command in commands:
        if not isinstance(command, Mapping) or command.get("exit_code") != 0:
            return False
        passed = command.get("passed")
        total = command.get("total")
        if (
            isinstance(passed, bool)
            or not isinstance(passed, int)
            or isinstance(total, bool)
            or not isinstance(total, int)
            or passed != total
            or total < 0
        ):
            return False
    return True


def _canonical_manifest_digest(manifest: Mapping[str, Any]) -> str:
    unsigned = dict(manifest)
    unsigned["manifest_sha256"] = ""
    return hashlib.sha256(
        json.dumps(unsigned, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    ).hexdigest()


def _verification_document(paths: Sequence[Path]) -> Mapping[str, Any] | None:
    for path in paths:
        if path.name != "m4_final_verification.json":
            continue
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return None
        return loaded if isinstance(loaded, Mapping) else None
    return None


def _acceptance_commands_are_backed(
    verification: Mapping[str, Any],
    paths: Sequence[Path],
) -> bool:
    document = _verification_document(paths)
    commands = verification.get("commands")
    source_commands = document.get("commands") if isinstance(document, Mapping) else None
    if (
        not isinstance(commands, Sequence)
        or isinstance(commands, (str, bytes))
        or not isinstance(source_commands, Sequence)
        or isinstance(source_commands, (str, bytes))
    ):
        return False
    by_name = {
        command.get("name"): command
        for command in source_commands
        if isinstance(command, Mapping) and isinstance(command.get("name"), str)
    }
    for command in commands:
        if not isinstance(command, Mapping):
            return False
        source = by_name.get(command.get("name"))
        if not isinstance(source, Mapping):
            return False
        for key in ("exit_code", "passed", "total"):
            if source.get(key) != command.get(key):
                return False
    return True


def _canonical_acceptance_result_is_valid(verification: Mapping[str, Any], paths: Sequence[Path]) -> bool:
    result = verification.get("canonical_result")
    if not isinstance(result, Mapping):
        return False
    result_path = _resolve_repository_file(result.get("path"))
    if result_path is None or result_path not in paths:
        return False
    try:
        loaded = json.loads(result_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    games = loaded.get("results") if isinstance(loaded, Mapping) else None
    return (
        isinstance(loaded, Mapping)
        and loaded.get("complete_games") == 16
        and loaded.get("required_complete_games") == 16
        and loaded.get("both_start_player_partitions") is True
        and loaded.get("candidate_truncation_count") == 0
        and loaded.get("automatic_decision_count") == 0
        and loaded.get("core_error_count") == 0
        and isinstance(games, list)
        and len(games) == 16
        and all(
            isinstance(game, Mapping) and game.get("status") == "PASS" and game.get("terminal") is True
            for game in games
        )
    )


def _validate_acceptance_evidence(
    manifest: Mapping[str, Any] | None,
    *,
    run_identity: str | None,
    loaded: Mapping[int, tuple[Mapping[str, Any], Path | None, int]],
    semantic_comparisons: Sequence[Mapping[str, Any]],
) -> tuple[dict[str, Any] | None, str]:
    """Validate a repository-backed acceptance manifest and derive gate status."""

    if run_identity is None:
        return None, "acceptance evidence lacks a repository-backed run identity"
    if not isinstance(manifest, Mapping):
        return None, "acceptance evidence lacks a structured manifest"
    if manifest.get("schema_version") != ACCEPTANCE_EVIDENCE_SCHEMA_VERSION:
        return None, "acceptance evidence has no recognized schema version"
    if manifest.get("run_identity") != run_identity:
        return None, "acceptance evidence run identity does not match validated reports"
    manifest_path = _resolve_repository_file(manifest.get("manifest_path"))
    manifest_digest = manifest.get("manifest_sha256")
    if manifest_path is None or not _is_sha256(manifest_digest):
        return None, "acceptance evidence lacks a hashed manifest file"
    try:
        file_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None, "acceptance evidence manifest cannot be read"
    if not isinstance(file_manifest, Mapping) or dict(file_manifest) != dict(manifest):
        return None, "acceptance evidence mapping is not the hashed manifest content"
    if file_manifest.get("manifest_sha256") != manifest_digest:
        return None, "acceptance evidence manifest digest is not self-consistent"
    if _canonical_manifest_digest(file_manifest).lower() != manifest_digest.lower():
        return None, "acceptance evidence manifest digest is stale"

    gates = manifest.get("gates")
    if not isinstance(gates, Mapping) or set(gates) != set(FINAL_ACCEPTANCE_GATES):
        return None, "acceptance evidence does not cover every final gate"

    expected_workers = sorted(loaded)
    normalized_gates: dict[str, dict[str, Any]] = {}
    for gate in FINAL_ACCEPTANCE_GATES:
        entry = gates.get(gate)
        if not isinstance(entry, Mapping):
            return None, f"acceptance evidence gate {gate} is not structured"
        evidence_text = entry.get("evidence")
        verification = entry.get("verification")
        verified = _verified_acceptance_artifacts(entry)
        if (
            not isinstance(evidence_text, str)
            or not evidence_text.strip()
            or not isinstance(verification, Mapping)
            or verified is None
            or not _acceptance_commands_passed(verification)
            or not _acceptance_commands_are_backed(verification, verified[0])
        ):
            return None, f"acceptance evidence gate {gate} lacks independently verifiable proof"
        paths, artifacts = verified
        gate_valid = True
        if gate == "parallel_determinism":
            gate_valid = (
                verification.get("workers") == expected_workers
                and verification.get("mismatches") == []
                and all(comparison.get("mismatches") == [] for comparison in semantic_comparisons)
            )
        elif gate == "mode_equivalence":
            experiment_reports: list[Mapping[str, Any]] = []
            for path in paths:
                if path.name not in {
                    "conformance.json",
                    "throughput-no-persistence.json",
                    "throughput-off-diagnostic.json",
                }:
                    continue
                try:
                    loaded_report = json.loads(path.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError):
                    loaded_report = None
                if isinstance(loaded_report, Mapping):
                    experiment_reports.append(loaded_report)
            fingerprints = [_semantic_baseline_fingerprint(report) for report in experiment_reports]
            gate_valid = (
                len(fingerprints) == 3
                and all(fingerprint == fingerprints[0] for fingerprint in fingerprints[1:])
                and all(
                    report.get("instrumentation") is True and report.get("mode") in {"conformance", "throughput"}
                    for report in experiment_reports
                )
                and any(report.get("observation_mode") == "off_diagnostic" for report in experiment_reports)
                and any(report.get("trace_persistence") is True for report in experiment_reports)
            )
        elif gate == "failure_isolation":
            timeout_path = _resolve_repository_file(verification.get("timeout_log_path"))
            if timeout_path is None or timeout_path not in paths:
                gate_valid = False
            else:
                try:
                    timeout_text = timeout_path.read_text(encoding="utf-8")
                except OSError:
                    gate_valid = False
                else:
                    gate_valid = all(
                        marker in timeout_text
                        for marker in (
                            "failure_class=worker_timeout",
                            "report_written=false",
                            "python_main_return_code=2",
                        )
                    )
        elif gate == "handshake_identity":
            gate_valid = all(
                int(report["steady_state"]["errors"]["coordinator"]["handshake"]) == 0
                for report, _, _ in loaded.values()
            )
        elif gate == "integrity":
            gate_valid = True
        elif gate == "privacy":
            gate_valid = all(
                stderr_bytes == 0 and report.get("trace_persistence") is False
                for report, _, stderr_bytes in loaded.values()
            )
        elif gate == "candidate_observation":
            gate_valid = _canonical_acceptance_result_is_valid(verification, paths)
        elif gate == "final_build_and_ctest":
            verification_document = _verification_document(paths)
            ctest = verification_document.get("ctest") if isinstance(verification_document, Mapping) else None
            gate_valid = (
                isinstance(ctest, Mapping)
                and ctest.get("passed") == ctest.get("total") == FINAL_CTEST_EXPECTED_TOTAL
                and verification.get("ctest_passed") == ctest.get("passed")
                and verification.get("ctest_total") == ctest.get("total")
            )
        if not gate_valid:
            return None, f"acceptance evidence gate {gate} failed independent verification"
        normalized_gates[gate] = {
            "status": "PASS",
            "fresh": True,
            "evidence": evidence_text.strip(),
            "run_identity": run_identity,
            "artifacts": artifacts,
            "verification": dict(verification),
        }
    normalized = {
        "schema_version": ACCEPTANCE_EVIDENCE_SCHEMA_VERSION,
        "run_identity": run_identity,
        "manifest_path": _repository_relative_path(manifest_path) or "",
        "manifest_sha256": manifest_digest.lower(),
        "gates": normalized_gates,
    }
    return normalized, "all independently verified acceptance gates passed"


def build_baseline(
    rows: Mapping[int, Mapping[str, Any] | str | Path],
    *,
    required_workers: Sequence[int] = DEFAULT_MATRIX_WORKERS,
    optional_workers: Sequence[int] = DEFAULT_OPTIONAL_WORKERS,
    skipped_rows: Mapping[int, str] | None = None,
    acceptance_evidence: Mapping[str, Mapping[str, Any]] | None = None,
) -> dict[str, Any]:
    """Validate matrix reports and build the evidence-only baseline handoff."""

    required = tuple(required_workers)
    optional = tuple(optional_workers)
    missing = [workers for workers in required if workers not in rows]
    if missing:
        raise BenchmarkIntegrityError(f"missing required matrix row(s): {missing}")
    if len(set(required)) != len(required) or any(
        isinstance(workers, bool) or not isinstance(workers, int) or workers <= 0
        for workers in required + optional
    ):
        raise BenchmarkIntegrityError("matrix worker list is invalid")

    loaded: dict[int, tuple[dict[str, Any], Path | None, int]] = {}
    reference: Mapping[str, Any] | None = None
    for workers in required + tuple(workers for workers in optional if workers in rows):
        try:
            report, path = _load_baseline_report(rows[workers])
            _validate_declared_benchmark_schema(report)
            stderr_bytes = _validate_matrix_report(report, workers, reference=reference)
        except BenchmarkIntegrityError as error:
            raise BenchmarkIntegrityError(f"invalid matrix row {workers}: {error}") from error
        loaded[workers] = (report, path, stderr_bytes)
        if reference is None:
            reference = report

    assert reference is not None
    baseline_games_per_second = float(loaded[required[0]][0]["steady_state"]["games_per_second"])
    scaling = [
        _baseline_scaling_row(
            loaded[workers][0],
            stderr_bytes=loaded[workers][2],
            baseline_games_per_second=baseline_games_per_second,
        )
        for workers in sorted(loaded)
    ]
    semantic_reference = _semantic_baseline_fingerprint(reference)
    semantic_comparisons: list[dict[str, Any]] = []
    for workers in sorted(loaded):
        current = _semantic_baseline_fingerprint(loaded[workers][0])
        mismatches = [
            job_id
            for job_id in sorted(set(semantic_reference) | set(current))
            if semantic_reference.get(job_id) != current.get(job_id)
        ]
        semantic_comparisons.append({"workers": workers, "mismatches": mismatches})
        if mismatches:
            raise BenchmarkIntegrityError(
                f"M4 BLOCKED — PARALLEL DETERMINISM FAILURE at {workers} workers: {mismatches}"
            )

    skipped = dict(skipped_rows or {})
    optional_status: list[dict[str, Any]] = []
    for workers in optional:
        if workers in loaded:
            optional_status.append({"workers": workers, "status": "MEASURED"})
        else:
            reason = skipped.get(workers)
            if not isinstance(reason, str) or not reason.strip():
                raise BenchmarkIntegrityError(f"missing NOT_RUN reason for optional row {workers}")
            optional_status.append({"workers": workers, "status": "NOT_RUN", "reason": reason.strip()})

    reference_build = reference["build"]
    run_identity = _baseline_run_identity(loaded, reference_build)
    validated_acceptance_evidence, status_reason = _validate_acceptance_evidence(
        acceptance_evidence,
        run_identity=run_identity,
        loaded=loaded,
        semantic_comparisons=semantic_comparisons,
    )
    evidence_is_complete = validated_acceptance_evidence is not None
    status = "M4 BASELINE PASS — PERFORMANCE AUDIT READY" if evidence_is_complete else "M4 BASELINE ACCEPTANCE PENDING"
    evidence_by_worker = {str(row["workers"]): row for row in scaling}
    return {
        "schema_version": BASELINE_SCHEMA_VERSION,
        "status": status,
        "status_reason": status_reason,
        "acceptance_evidence": validated_acceptance_evidence,
        "evidence_identity": run_identity or "UNVERIFIED",
        "required_workers": list(required),
        "optional_rows": optional_status,
        "canonical_environment": dict(reference["canonical_environment"]),
        "hardware": dict(reference["hardware"]),
        "build_identity": {
            key: reference_build[key]
            for key in (
                "protocol_schema",
                "protocol_version",
                "worker_identity",
                "compiler_identity",
                "build_type",
                "platform",
            )
        },
        "matrix_policy": {
            "games": MATRIX_GAMES,
            "warmup_games": MATRIX_WARMUP_GAMES,
            "master_seed": MATRIX_MASTER_SEED,
            "mode": "throughput",
            "observation_mode": "full",
            "instrumentation": True,
            "trace_persistence": False,
            "starting_player_mode": "balanced",
            "seat_mode": "balanced",
            "instrumentation": True,
        },
        "scaling": scaling,
        "semantic_gate": {
            "status": "PASS",
            "baseline_workers": required[0],
            "comparisons": semantic_comparisons,
            "compared_fields": list(SEMANTIC_RESULT_FIELDS),
        },
        "evidence": {
            "rows_by_worker": evidence_by_worker,
            "timing_domains": {
                "simulation_elapsed_us": "worker-local fresh CoreHost through result assembly",
                "coordinator_elapsed_us": (
                    "end-to-end dispatch write/flush through validated result receipt; "
                    "includes worker-compute wait and is not isolated IPC CPU"
                ),
                "games_per_second": "steady-state coordinator wall clock",
            },
            "performance_audit_candidates": _audit_candidates(
                evidence_by_worker[str(required[0])]["timing_buckets_us"],
                evidence_by_worker[str(required[0])]["operation_counters"],
            ),
        },
        "source_reports": {
            str(workers): _source_report_label(loaded[workers][1])
            for workers in sorted(loaded)
        },
    }


def render_baseline_markdown(baseline: Mapping[str, Any]) -> str:
    """Render the baseline handoff as a compact auditable Markdown document."""

    def fmt(value: Any) -> str:
        if isinstance(value, float):
            return f"{value:.6f}"
        if value == NOT_MEASURED:
            return NOT_MEASURED
        return str(value)

    lines = [
        "# OCGForge M4 Baseline",
        "",
        f"**{baseline['status']}**",
        "",
        baseline.get("status_reason", ""),
        "",
        "## Scaling",
        "",
        "| workers | games | wall s | games/s | engine steps/s | decisions/s | speedup | efficiency | sim mean us | p50 us | p95 us | p99 us | peak working set | physical-memory % | result timeout s |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in baseline["scaling"]:
        sim = row["simulation_elapsed_us"]
        memory = row["memory"]
        lines.append(
            "| "
            + " | ".join(
                fmt(value)
                for value in (
                    row["workers"],
                    row["games"],
                    row["wall_clock_seconds"],
                    row["games_per_second"],
                    row["engine_steps_per_second"],
                    row["interactive_decisions_per_second"],
                    row["speedup"],
                    row["parallel_efficiency"],
                    sim["mean"],
                    sim["p50"],
                    sim["p95"],
                    sim["p99"],
                    memory["peak_total_working_set_bytes"],
                    memory["peak_percent_of_physical_memory"],
                    row["result_timeout_seconds"],
                )
            )
            + " |"
        )

    lines.extend(["", "## Optional worker rows", "", "| workers | status | reason |", "|---:|---|---|"])
    for row in baseline["optional_rows"]:
        lines.append(f"| {row['workers']} | {row['status']} | {row.get('reason', '')} |")

    lines.extend(["", "## Integrity and semantic gate", ""])
    semantic_workers = [comparison["workers"] for comparison in baseline["semantic_gate"]["comparisons"]]
    lines.append(f"- Semantic gate: **{baseline['semantic_gate']['status']}** across workers {', '.join(map(str, semantic_workers))}.")
    lines.append("- Every accepted row has 64/64 terminal games, zero integrity counters, sorted unique job IDs, empty worker stderr, and the canonical handshake/environment.")
    lines.append("- Result timeout is an operational guard, not a simulation-policy input; the measured 32/64-worker rows use documented longer guards because the 120-second control run timed out.")
    lines.append("- Semantic comparisons cover: " + ", ".join(baseline["semantic_gate"]["compared_fields"]) + ".")

    lines.extend(["", "## Error counters", "", "| workers | retries | unsupported | automatic | truncated | core errors | worker errors | handshake | malformed protocol | failed games | worker crashes | worker restarts |", "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"])
    for row in baseline["scaling"]:
        errors = row["errors"]
        lines.append("| " + " | ".join(fmt(errors.get(key, row["workers"])) for key in (
            "workers", "retries", "unsupported", "automatic", "truncated", "core_errors", "worker_errors", "coordinator_handshake", "coordinator_malformed_protocol", "coordinator_failed_games", "coordinator_worker_crashes", "coordinator_worker_restarts"
        )) + " |")

    one = baseline["evidence"]["rows_by_worker"][str(baseline["required_workers"][0])]
    lines.extend(["", "## Timing percentages — one-worker reference", "", "| bucket | measured percent |", "|---|---:|"])
    for key, value in one["timing_percentages"].items():
        lines.append(f"| {key} | {fmt(value)} |")
    lines.extend(["", "## Operation counters — one-worker reference", "", "| counter | value |", "|---|---:|"])
    for key, value in one["operation_counters"].items():
        lines.append(f"| {key} | {fmt(value)} |")
    lines.extend(["", "## PERFORMANCE AUDIT CANDIDATES", ""])
    candidates = baseline["evidence"]["performance_audit_candidates"]
    if candidates:
        for candidate in candidates:
            lines.append(f"- {candidate['candidate']}: {candidate['basis']}")
    else:
        lines.append("- None supported by measured buckets/counters.")
    lines.extend(["", "No candidate is implemented by this baseline handoff.", ""])
    return "\n".join(lines)


def write_baseline(path: str | Path, baseline: Mapping[str, Any]) -> None:
    destination = Path(path)
    write_text_lf(
        destination,
        json.dumps(baseline, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n",
    )


def write_baseline_markdown(path: str | Path, baseline: Mapping[str, Any]) -> None:
    destination = Path(path)
    write_text_lf(destination, render_baseline_markdown(baseline).rstrip("\n") + "\n")


def build_report(
    *,
    canonical_environment: Mapping[str, Any],
    hardware: Mapping[str, Any],
    build: Mapping[str, Any],
    warmup_policy: Mapping[str, Any],
    mode: str,
    observation_mode: str = "full",
    instrumentation: bool = False,
    trace_persistence: bool = False,
    games_requested: int,
    workers_requested: int,
    cold_start: Mapping[str, Any],
    steady_state: Mapping[str, Any],
    jobs: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
    if mode not in {"conformance", "throughput"}:
        raise ValueError("mode must be conformance or throughput")
    if observation_mode not in {"full", "off_diagnostic"}:
        raise ValueError("observation_mode must be full or off_diagnostic")
    if not isinstance(instrumentation, bool) or not isinstance(trace_persistence, bool):
        raise ValueError("instrumentation and trace_persistence must be booleans")
    report = {
        "schema_version": REPORT_SCHEMA_VERSION,
        "canonical_environment": dict(canonical_environment),
        "hardware": dict(hardware),
        "build": dict(build),
        "warmup_policy": dict(warmup_policy),
        "mode": mode,
        "observation_mode": observation_mode,
        "instrumentation": instrumentation,
        "trace_persistence": trace_persistence,
        "games_requested": games_requested,
        "workers_requested": workers_requested,
        "cold_start": dict(cold_start),
        "steady_state": dict(steady_state),
        "jobs": [dict(job) for job in jobs],
    }
    if observation_mode == "off_diagnostic":
        report["classification_label"] = "DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT"
    return report


def write_report(path: str | Path, report: Mapping[str, Any]) -> None:
    """Write one deterministic, finite JSON report."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def default_hardware_metadata() -> dict[str, Any]:
    physical_memory = read_physical_memory_bytes()
    return {
        "platform": platform.platform() or NOT_MEASURED,
        "system": platform.system() or NOT_MEASURED,
        "machine": platform.machine() or NOT_MEASURED,
        "processor": platform.processor() or NOT_MEASURED,
        "python_version": platform.python_version() or NOT_MEASURED,
        "physical_memory_bytes": (
            physical_memory if physical_memory is not None else NOT_MEASURED
        ),
    }


def read_physical_memory_bytes() -> int | None:
    """Return the host's total physical memory when the platform exposes it."""

    if os.name != "nt":
        return None
    try:
        class MemoryStatusEx(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]

        status = MemoryStatusEx()
        status.dwLength = ctypes.sizeof(status)
        global_memory_status_ex = ctypes.windll.kernel32.GlobalMemoryStatusEx
        global_memory_status_ex.argtypes = [ctypes.POINTER(MemoryStatusEx)]
        global_memory_status_ex.restype = ctypes.c_bool
        if global_memory_status_ex(ctypes.byref(status)):
            return int(status.ullTotalPhys)
    except Exception:
        return None
    return None


__all__ = [
    "BenchmarkIntegrityError",
    "ACCEPTANCE_EVIDENCE_SCHEMA_VERSION",
    "BASELINE_SCHEMA_VERSION",
    "FINAL_ACCEPTANCE_GATES",
    "COORDINATOR_ERROR_KEYS",
    "COUNTER_KEYS",
    "NATIVE_ERROR_KEYS",
    "NOT_MEASURED",
    "REPORT_SCHEMA_VERSION",
    "TIMING_KEYS",
    "aggregate_results",
    "build_baseline",
    "build_argument_parser",
    "build_report",
    "default_hardware_metadata",
    "percentile_summary",
    "read_physical_memory_bytes",
    "render_baseline_markdown",
    "validate_complete_results",
    "write_report",
    "write_baseline",
    "write_baseline_markdown",
]
