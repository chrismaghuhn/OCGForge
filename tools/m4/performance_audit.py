"""Audit-only coordinator for the M4.2 observation-path measurement.

The normal M4 benchmark is intentionally left untouched.  This module wraps
``PersistentWorkerPool`` and measures coordinator domains around the same
worker protocol and lifecycle gates that the baseline uses.  Native audit
telemetry is carried on stderr so the primary stdout JSONL contract remains
unchanged.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
import math
from pathlib import Path
import queue
import tempfile
import threading
import time
from typing import Any, Iterable, Mapping, Sequence

try:  # pragma: no cover - direct module execution uses the fallback branch
    from . import benchmark as _benchmark
    from .benchmark import PersistentWorkerPool
    from .job_generation import derive_job_with_options
    from .performance_audit_contract import (
        AUXILIARY_TIMING_KEYS,
        COORDINATOR_TIMING_KEYS,
        ENTITY_ZONE_KEYS,
        OBSERVATION_COUNTER_KEYS,
        OBSERVATION_DETAIL_COUNTER_KEYS,
        OBSERVATION_TIMING_KEYS,
        PerformanceAuditContractError,
        SETUP_TIMING_KEYS,
        default_coordinator_timing_us,
        validate_audit_telemetry,
    )
    from .report import (
        aggregate_results,
        default_hardware_metadata,
        validate_complete_results,
    )
    from .worker_protocol import assert_primary_integrity, validate_ready
except ImportError:  # pragma: no cover - exercised by direct CLI imports
    from tools.m4 import benchmark as _benchmark
    from tools.m4.benchmark import PersistentWorkerPool
    from tools.m4.job_generation import derive_job_with_options
    from tools.m4.performance_audit_contract import (
        AUXILIARY_TIMING_KEYS,
        COORDINATOR_TIMING_KEYS,
        ENTITY_ZONE_KEYS,
        OBSERVATION_COUNTER_KEYS,
        OBSERVATION_DETAIL_COUNTER_KEYS,
        OBSERVATION_TIMING_KEYS,
        PerformanceAuditContractError,
        SETUP_TIMING_KEYS,
        default_coordinator_timing_us,
        validate_audit_telemetry,
    )
    from tools.m4.report import (
        aggregate_results,
        default_hardware_metadata,
        validate_complete_results,
    )
    from tools.m4.worker_protocol import assert_primary_integrity, validate_ready


AUDIT_SIDECAR_PREFIX = "M4_PERFORMANCE_AUDIT "
AUDIT_SIDECAR_SCHEMA = "ocgforge.m4.performance_audit.v1"
AUDIT_SIDECAR_TYPE = "performance_audit"
OFF_DIAGNOSTIC_LABEL = "DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT"
UINT64_MAX = (1 << 64) - 1
_REQUIRED_SIDECAR_KEYS = frozenset(
    {
        "schema",
        "type",
        "job_id",
        "observation_total_us",
        "observation_timing_us",
        "observation_counters",
        "observation_detail_counters",
        "setup_timing_us",
        "auxiliary_timing_us",
        "entities_by_zone",
    }
)
_FUTURE_TOP_LEVEL_PREFIX = "future_"
_SCRIPT_LOAD_SETUP_KEY = "script_load"
_COMPLETE_SETUP_TIMING_KEYS = tuple(
    dict.fromkeys((*SETUP_TIMING_KEYS, _SCRIPT_LOAD_SETUP_KEY))
)


class PerformanceAuditRunnerError(RuntimeError):
    """Base error for fail-closed audit-runner failures."""


class PerformanceAuditSidecarError(PerformanceAuditRunnerError):
    """Raised when native audit stderr telemetry is incomplete or malformed."""


class CoordinatorTimingError(PerformanceAuditRunnerError):
    """Raised when coordinator timing data cannot be classified safely."""


class _CumulativeNonblockingTimer:
    """Accumulate short nonblocking operation windows without per-call loss.

    ``GetThreadTimes`` on Windows advances in coarse quanta.  JSON encoding
    and decoding do not block on worker computation, so a high-resolution
    ``perf_counter_ns`` window is a safe CPU-proxy for those operations.  The
    elapsed windows are accumulated before publication instead of relying on
    a single short call to cross the thread-clock quantum.
    """

    def __init__(self, *, clock: Any = time.perf_counter_ns) -> None:
        self._clock = clock
        self.total_ns = 0
        self.calls = 0
        self.last_elapsed_ns = 0

    def reset(self) -> None:
        self.total_ns = 0
        self.calls = 0
        self.last_elapsed_ns = 0

    def measure(self, operation: Any) -> Any:
        start = int(self._clock())
        try:
            return operation()
        finally:
            elapsed = int(self._clock()) - start
            self.last_elapsed_ns = max(0, elapsed)
            self.total_ns += self.last_elapsed_ns
            self.calls += 1


def _thread_cpu_ns() -> int:
    """Return CPU time for the current Python thread, excluding blocking waits."""

    clock = getattr(time, "thread_time_ns", None)
    if clock is not None:
        return int(clock())
    # Python versions supported by this repository provide thread_time_ns, but
    # process_time_ns is a conservative fallback for older interpreters.
    return int(time.process_time_ns())


def _require_uint64(value: Any, name: str) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < 0
        or value > UINT64_MAX
    ):
        raise PerformanceAuditSidecarError(
            f"{name} must be a nonnegative unsigned integer"
        )
    return value


def _strict_object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PerformanceAuditSidecarError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _parse_json_object(payload: str, source: str) -> dict[str, Any]:
    try:
        value = json.loads(
            payload,
            object_pairs_hook=_strict_object_pairs,
            parse_constant=lambda token: (_ for _ in ()).throw(
                ValueError(f"non-finite JSON constant: {token}")
            ),
        )
    except (TypeError, ValueError, json.JSONDecodeError, RecursionError) as error:
        raise PerformanceAuditSidecarError(
            f"malformed audit sidecar JSON in {source}: {error}"
        ) from error
    if not isinstance(value, dict):
        raise PerformanceAuditSidecarError(
            f"audit sidecar root must be an object in {source}"
        )
    return value


def _validate_timing_object(
    value: Any,
    name: str,
    *,
    required_keys: Iterable[str] | None = None,
) -> dict[str, dict[str, int]]:
    if not isinstance(value, Mapping):
        raise PerformanceAuditSidecarError(f"{name} must be an object")
    if required_keys is not None and set(value) != set(required_keys):
        raise PerformanceAuditSidecarError(f"{name} has the wrong keys")

    validated: dict[str, dict[str, int]] = {}
    for bucket_name, bucket_value in value.items():
        if not isinstance(bucket_name, str) or not isinstance(bucket_value, Mapping):
            raise PerformanceAuditSidecarError(
                f"{name}.{bucket_name!r} must be a timing object"
            )
        if set(bucket_value) != {"total_us", "calls", "mean_us_per_call"}:
            raise PerformanceAuditSidecarError(
                f"{name}.{bucket_name} has the wrong keys"
            )
        total_us = _require_uint64(bucket_value["total_us"], f"{name}.{bucket_name}.total_us")
        calls = _require_uint64(bucket_value["calls"], f"{name}.{bucket_name}.calls")
        mean_us = _require_uint64(
            bucket_value["mean_us_per_call"],
            f"{name}.{bucket_name}.mean_us_per_call",
        )
        expected_mean = 0 if calls == 0 else total_us // calls
        if mean_us != expected_mean:
            raise PerformanceAuditSidecarError(
                f"{name}.{bucket_name}.mean_us_per_call is inconsistent"
            )
        validated[bucket_name] = {
            "total_us": total_us,
            "calls": calls,
            "mean_us_per_call": mean_us,
        }
    return validated


def _validate_counter_object(
    value: Any,
    name: str,
    *,
    required_keys: Iterable[str] | None = None,
) -> dict[str, int]:
    if not isinstance(value, Mapping):
        raise PerformanceAuditSidecarError(f"{name} must be an object")
    if required_keys is not None and set(value) != set(required_keys):
        raise PerformanceAuditSidecarError(f"{name} has the wrong keys")
    return {
        str(key): _require_uint64(item, f"{name}.{key}")
        for key, item in value.items()
    }


def _validate_required_detail_fields(sidecar: Mapping[str, Any]) -> dict[str, Any]:
    """Validate every native detail group required for an M4.2 report."""

    for field in (
        "observation_detail_counters",
        "setup_timing_us",
        "auxiliary_timing_us",
        "entities_by_zone",
    ):
        if field not in sidecar:
            raise PerformanceAuditSidecarError(
                f"{field} is required native audit evidence"
            )

    setup_timing = sidecar["setup_timing_us"]
    if not isinstance(setup_timing, Mapping):
        raise PerformanceAuditSidecarError("setup_timing_us must be an object")
    setup_keys = set(setup_timing)
    legacy_setup_timing = setup_keys == set(SETUP_TIMING_KEYS) and (
        _SCRIPT_LOAD_SETUP_KEY not in setup_keys
    )
    if (
        setup_keys != set(SETUP_TIMING_KEYS)
        and setup_keys != set(_COMPLETE_SETUP_TIMING_KEYS)
    ):
        raise PerformanceAuditSidecarError(
            "setup_timing_us has the wrong keys; script_load timing is required"
        )

    normalized = {
        "observation_detail_counters": _validate_counter_object(
            sidecar["observation_detail_counters"],
            "observation_detail_counters",
            required_keys=OBSERVATION_DETAIL_COUNTER_KEYS,
        ),
        "setup_timing_us": _validate_timing_object(
            sidecar["setup_timing_us"],
            "setup_timing_us",
            required_keys=tuple(setup_keys),
        ),
        "auxiliary_timing_us": _validate_timing_object(
            sidecar["auxiliary_timing_us"],
            "auxiliary_timing_us",
            required_keys=AUXILIARY_TIMING_KEYS,
        ),
    }
    if legacy_setup_timing:
        normalized["setup_timing_us"][_SCRIPT_LOAD_SETUP_KEY] = {
            "total_us": 0,
            "calls": 0,
            "mean_us_per_call": 0,
        }
        normalized["_legacy_setup_timing_missing_script_load"] = True

    zones = sidecar["entities_by_zone"]
    if not isinstance(zones, Mapping):
        raise PerformanceAuditSidecarError("entities_by_zone must be an object")
    if set(zones) != set(ENTITY_ZONE_KEYS):
        raise PerformanceAuditSidecarError("entities_by_zone has the wrong keys")
    normalized_zones: dict[str, dict[str, int]] = {}
    for zone in ENTITY_ZONE_KEYS:
        counters = zones[zone]
        if not isinstance(counters, Mapping) or set(counters) != {
            "entities_projected",
            "identity_known",
            "redacted",
        }:
            raise PerformanceAuditSidecarError(
                f"entities_by_zone.{zone} has the wrong keys"
            )
        normalized_zones[zone] = {
            key: _require_uint64(value, f"entities_by_zone.{zone}.{key}")
            for key, value in counters.items()
        }
    normalized["entities_by_zone"] = normalized_zones
    return normalized


def parse_audit_sidecar_line(
    line: str,
    *,
    source: str = "worker stderr",
    expected_job_id: str | None = None,
) -> dict[str, Any]:
    """Parse one exact ``M4_PERFORMANCE_AUDIT`` stderr record.

    The prefix is deliberately mandatory.  A raw JSON object without the
    prefix is not treated as telemetry, which prevents a native emitter drift
    from being silently accepted as a complete audit.
    """

    if not isinstance(line, str):
        raise PerformanceAuditSidecarError("audit sidecar line must be text")
    line = line.rstrip("\r\n")
    if not line.startswith(AUDIT_SIDECAR_PREFIX):
        raise PerformanceAuditSidecarError(
            f"audit sidecar line has no {AUDIT_SIDECAR_PREFIX.strip()} prefix"
        )
    sidecar = _parse_json_object(line[len(AUDIT_SIDECAR_PREFIX) :], source)

    missing_top_level = sorted(_REQUIRED_SIDECAR_KEYS - set(sidecar))
    if missing_top_level:
        raise PerformanceAuditSidecarError(
            "audit sidecar is missing required top-level evidence: "
            + ", ".join(missing_top_level)
        )
    unexpected_top_level = sorted(
        key
        for key in set(sidecar) - _REQUIRED_SIDECAR_KEYS
        if not isinstance(key, str) or not key.startswith(_FUTURE_TOP_LEVEL_PREFIX)
    )
    if unexpected_top_level:
        raise PerformanceAuditSidecarError(
            "audit sidecar has unexpected top-level fields: "
            + ", ".join(str(key) for key in unexpected_top_level)
        )

    if sidecar.get("schema") != AUDIT_SIDECAR_SCHEMA:
        raise PerformanceAuditSidecarError("audit sidecar schema mismatch")
    if sidecar.get("type") != AUDIT_SIDECAR_TYPE:
        raise PerformanceAuditSidecarError("audit sidecar type mismatch")
    job_id = sidecar.get("job_id")
    if not isinstance(job_id, str) or not job_id:
        raise PerformanceAuditSidecarError("audit sidecar has no job_id")
    if expected_job_id is not None and job_id != expected_job_id:
        raise PerformanceAuditSidecarError(
            f"audit sidecar job_id mismatch: expected {expected_job_id}, got {job_id}"
        )
    observation_total_us = _require_uint64(
        sidecar.get("observation_total_us"),
        "observation_total_us",
    )
    timing = _validate_timing_object(
        sidecar.get("observation_timing_us"),
        "observation_timing_us",
        required_keys=OBSERVATION_TIMING_KEYS,
    )
    counters = _validate_counter_object(
        sidecar.get("observation_counters"),
        "observation_counters",
        required_keys=OBSERVATION_COUNTER_KEYS,
    )
    detail_fields = _validate_required_detail_fields(sidecar)

    if sum(bucket["total_us"] for bucket in timing.values()) != observation_total_us:
        raise PerformanceAuditSidecarError(
            "observation timing buckets must exactly equal observation_total_us"
        )

    # The native sidecar owns observation data.  Coordinator timing is added
    # by this Python wrapper, but the existing audit contract validates all
    # three groups together.  Validate the native portion through that same
    # contract with a zero coordinator group before returning the rich shape.
    telemetry = {
        "observation_timing_us": {
            key: bucket["total_us"] for key, bucket in timing.items()
        },
        "observation_counters": counters,
        "coordinator_timing_us": default_coordinator_timing_us(),
    }
    try:
        validate_audit_telemetry(
            telemetry,
            require=True,
            outer_observation_us=observation_total_us,
        )
    except PerformanceAuditContractError as error:
        raise PerformanceAuditSidecarError(str(error)) from error

    normalized = dict(sidecar)
    normalized.update(
        {
            "job_id": job_id,
            "observation_total_us": observation_total_us,
            "observation_timing_us": timing,
            "observation_counters": counters,
        }
    )
    normalized.update(detail_fields)
    return normalized


def parse_audit_sidecars(
    stderr_paths: Iterable[str | Path],
    expected_job_ids: Iterable[str],
) -> dict[str, dict[str, Any]]:
    """Read all worker stderr files and require one sidecar per job ID."""

    expected = list(expected_job_ids)
    if any(not isinstance(job_id, str) or not job_id for job_id in expected):
        raise PerformanceAuditSidecarError("expected job IDs must be nonempty strings")
    if len(set(expected)) != len(expected):
        raise PerformanceAuditSidecarError("expected job IDs must be unique")

    records: dict[str, dict[str, Any]] = {}
    for raw_path in stderr_paths:
        path = Path(raw_path)
        if not path.is_file():
            raise PerformanceAuditSidecarError(f"worker stderr path is missing: {path}")
        try:
            with path.open("r", encoding="utf-8", errors="strict", newline="") as stream:
                lines = stream.readlines()
        except (OSError, UnicodeError) as error:
            raise PerformanceAuditSidecarError(
                f"cannot read worker stderr path {path}: {error}"
            ) from error
        for line_number, line in enumerate(lines, start=1):
            if not line.startswith(AUDIT_SIDECAR_PREFIX):
                # Worker diagnostics are allowed beside the audit records.
                # A prefixed line, however, must parse completely below.
                continue
            source = f"{path}:{line_number}"
            record = parse_audit_sidecar_line(line, source=source)
            job_id = record["job_id"]
            if job_id in records:
                raise PerformanceAuditSidecarError(
                    f"duplicate audit sidecar for job_id {job_id}"
                )
            records[job_id] = record

    expected_set = set(expected)
    actual_set = set(records)
    missing = sorted(expected_set - actual_set)
    unexpected = sorted(actual_set - expected_set)
    if missing:
        raise PerformanceAuditSidecarError(
            "missing audit sidecars for job IDs: " + ", ".join(missing)
        )
    if unexpected:
        raise PerformanceAuditSidecarError(
            "unexpected audit sidecars for job IDs: " + ", ".join(unexpected)
        )
    return {job_id: records[job_id] for job_id in expected}


@dataclass(frozen=True)
class CoordinatorTimingSnapshot:
    """One sample of coordinator timing domains, stored in nanoseconds."""

    wall_clock_ns: int
    coordinator_cpu_ns: int
    worker_compute_wait_ns: int
    pipe_read_write_cpu_ns: int
    json_encode_decode_cpu_ns: int
    dispatch_queue_overhead_ns: int
    other_cpu_ns: int

    def __post_init__(self) -> None:
        for name, value in self.__dict__.items():
            if (
                isinstance(value, bool)
                or not isinstance(value, int)
                or value < 0
                or value > UINT64_MAX
            ):
                raise CoordinatorTimingError(
                    f"{name} must be a nonnegative unsigned integer"
                )

    def as_microseconds(self) -> dict[str, Any]:
        def convert(value: int) -> int:
            return value // 1_000

        timing = {
            "worker_compute_wait": convert(self.worker_compute_wait_ns),
            "pipe_read_write_cpu": convert(self.pipe_read_write_cpu_ns),
            "json_encode_decode_cpu": convert(self.json_encode_decode_cpu_ns),
            "dispatch_queue_overhead": convert(self.dispatch_queue_overhead_ns),
            "other": convert(self.other_cpu_ns),
        }
        return {
            "wall_clock_us": convert(self.wall_clock_ns),
            "coordinator_cpu_us": convert(self.coordinator_cpu_ns),
            "coordinator_timing_us": timing,
            "coordinator_timing_stats": {
                key: {"total_us": value, "calls": 1, "mean_us_per_call": value}
                for key, value in timing.items()
            },
        }


class _CoordinatorTimingAccumulator:
    """Thread-safe raw timing accumulator used by ``AuditWorkerPool``."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self.reset()

    def reset(self) -> None:
        with self._lock:
            self.wall_clock_ns = 0
            self.main_cpu_ns = 0
            self.lifecycle_wall_ns = 0
            self.lifecycle_cpu_ns = 0
            self.reader_cpu_ns = 0
            self.pipe_write_cpu_ns = 0
            self.json_cpu_ns = 0
            self.dispatch_queue_cpu_ns = 0
            self.worker_wait_ns = 0

    def add(self, name: str, value: int) -> None:
        value = _require_uint64(value, name)
        with self._lock:
            if name == "reader_cpu_ns":
                self.reader_cpu_ns += value
            elif name == "pipe_write_cpu_ns":
                self.pipe_write_cpu_ns += value
            elif name == "json_cpu_ns":
                self.json_cpu_ns += value
            elif name == "dispatch_queue_cpu_ns":
                self.dispatch_queue_cpu_ns += value
            elif name == "worker_wait_ns":
                self.worker_wait_ns += value
            else:
                raise CoordinatorTimingError(f"unknown timing accumulator field: {name}")

    def finish(self, wall_clock_ns: int, main_cpu_ns: int) -> None:
        with self._lock:
            wall_clock_ns = _require_uint64(wall_clock_ns, "wall_clock_ns")
            main_cpu_ns = _require_uint64(main_cpu_ns, "main_cpu_ns")
            self.wall_clock_ns = max(0, wall_clock_ns - self.lifecycle_wall_ns)
            self.main_cpu_ns = max(0, main_cpu_ns - self.lifecycle_cpu_ns)

    def add_lifecycle_time(self, *, wall_clock_ns: int, cpu_ns: int) -> None:
        """Keep explicitly measured lifecycle barriers outside the audit sample."""

        wall_clock_ns = _require_uint64(wall_clock_ns, "lifecycle_wall_clock_ns")
        cpu_ns = _require_uint64(cpu_ns, "lifecycle_cpu_ns")
        with self._lock:
            self.lifecycle_wall_ns += wall_clock_ns
            self.lifecycle_cpu_ns += cpu_ns

    def snapshot(self) -> CoordinatorTimingSnapshot:
        with self._lock:
            coordinator_cpu_ns = self.main_cpu_ns + self.reader_cpu_ns
            pipe_ns = self.pipe_write_cpu_ns + self.reader_cpu_ns
            classified_ns = (
                pipe_ns + self.json_cpu_ns + self.dispatch_queue_cpu_ns
            )
            other_ns = max(0, coordinator_cpu_ns - classified_ns)
            return CoordinatorTimingSnapshot(
                wall_clock_ns=self.wall_clock_ns,
                coordinator_cpu_ns=coordinator_cpu_ns,
                worker_compute_wait_ns=self.worker_wait_ns,
                pipe_read_write_cpu_ns=pipe_ns,
                json_encode_decode_cpu_ns=self.json_cpu_ns,
                dispatch_queue_overhead_ns=self.dispatch_queue_cpu_ns,
                other_cpu_ns=other_ns,
            )


class AuditWorkerPool(PersistentWorkerPool):
    """PersistentWorkerPool wrapper with M4.2 coordinator-only timing."""

    _HOOK_LOCK = threading.RLock()

    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, **kwargs)
        self._audit_timing = _CoordinatorTimingAccumulator()
        self._json_timer = _CumulativeNonblockingTimer()

    def _record_pipe_write(self, value: int) -> None:
        self._audit_timing.add("pipe_write_cpu_ns", value)

    def _record_reader_cpu(self, value: int) -> None:
        self._audit_timing.add("reader_cpu_ns", value)

    def _record_json_cpu(self, value: int) -> None:
        self._audit_timing.add("json_cpu_ns", value)

    def _record_dispatch_queue_cpu(self, value: int) -> None:
        self._audit_timing.add("dispatch_queue_cpu_ns", value)

    def _record_worker_wait(self, value: int) -> None:
        self._audit_timing.add("worker_wait_ns", value)

    def _launch(self, state: Any) -> None:
        # The base launch method dynamically resolves _read_stdout.  Attach
        # the owner before it starts the reader thread.
        state._audit_pool = self
        super()._launch(state)

    @staticmethod
    def _read_stdout(state: Any) -> None:
        owner = getattr(state, "_audit_pool", None)
        reader_cpu_start = _thread_cpu_ns()
        nonblocking_wall_ns = 0

        def measure_nonblocking(operation: Any) -> Any:
            nonlocal nonblocking_wall_ns
            start = time.perf_counter_ns()
            try:
                return operation()
            finally:
                nonblocking_wall_ns += max(0, time.perf_counter_ns() - start)

        def finish_reader_timing() -> None:
            if owner is None:
                return
            reader_cpu_ns = max(0, _thread_cpu_ns() - reader_cpu_start)
            if reader_cpu_ns > 0:
                # A thread CPU window spans all reads and queue operations, so
                # Windows clock quanta cannot erase every short operation.
                owner._record_reader_cpu(reader_cpu_ns)
            elif nonblocking_wall_ns > 0:
                # Safe fallback only covers completed-read decode/peek/queue
                # work.  Blocking readline time is never converted to wall
                # time and therefore cannot become fake pipe CPU.
                owner._record_reader_cpu(nonblocking_wall_ns)

        # Keep the baseline reader's ordering and failure behavior.  The
        # blocking readline calls are outside the fallback windows; the
        # reader-thread CPU window excludes their blocking waits.
        eof_observed = False
        try:
            assert state.process is not None
            assert state.process.stdout is not None
            assert state.events is not None
            while True:
                raw_line = state.process.stdout.readline()
                if raw_line == b"":
                    eof_observed = True
                    break
                try:
                    line = measure_nonblocking(lambda: raw_line.decode("utf-8"))
                except UnicodeDecodeError as error:
                    state.events.put(("reader_error", f"invalid UTF-8 on worker stdout: {error}"))
                    return
                pending_lines = [line.rstrip("\r\n")]
                while measure_nonblocking(
                    lambda: PersistentWorkerPool._stdout_pending_status(state)
                ) == "data":
                    raw_line = state.process.stdout.readline()
                    if raw_line == b"":
                        eof_observed = True
                        break
                    try:
                        pending_lines.append(
                            measure_nonblocking(
                                lambda: raw_line.decode("utf-8").rstrip("\r\n")
                            )
                        )
                    except UnicodeDecodeError as error:
                        state.events.put(("reader_error", f"invalid UTF-8 on worker stdout: {error}"))
                        return
                if eof_observed:
                    def publish_eof() -> None:
                        state.events.put(("eof", None))
                        for pending_line in pending_lines:
                            state.events.put(("line", pending_line))

                    measure_nonblocking(publish_eof)
                    return

                def publish_lines() -> None:
                    for pending_line in pending_lines:
                        state.events.put(("line", pending_line))

                measure_nonblocking(publish_lines)
        except Exception as error:  # pragma: no cover - OS-specific reader failure
            state.events.put(("reader_error", str(error)))
        finally:
            if not eof_observed:
                state.events.put(("eof", None))
            finish_reader_timing()

    def _dispatch(self, state: Any, job: dict[str, Any], encoded_job: str) -> None:
        # A single CPU window covers state setup plus write/flush.  It excludes
        # any blocking pipe wait because the Windows thread CPU clock pauses
        # while the coordinator is blocked in the OS pipe call.  There is no
        # wall-clock fallback here: a pipe write is not provably nonblocking.
        cpu_start = _thread_cpu_ns()
        try:
            return super()._dispatch(state, job, encoded_job)
        finally:
            self._record_pipe_write(max(0, _thread_cpu_ns() - cpu_start))

    def _next_event(self) -> Any:
        """Wait for a worker event and separate explicit polling sleeps."""

        timeout_ns = max(0, int(self.result_timeout_seconds * 1_000_000_000))
        wall_start = time.perf_counter_ns()
        cpu_start = _thread_cpu_ns()
        blocked_wait_ns = 0
        try:
            while True:
                now_ns = time.perf_counter_ns()
                outstanding = [
                    state
                    for state in self._states
                    if state.alive and state.in_flight is not None
                ]
                expired = [
                    state
                    for state in outstanding
                    if now_ns - state.in_flight.dispatched_ns >= timeout_ns
                ]
                if expired:
                    workers = ", ".join(str(state.index) for state in expired)
                    message = f"timed out waiting for a worker result (worker(s): {workers})"
                    self._fail_closed(message, code="worker_timeout")
                    raise _benchmark.WorkerRuntimeError(message)
                for state in self._states:
                    if state.events is None:
                        continue
                    try:
                        event = state.events.get_nowait()
                    except queue.Empty:
                        continue
                    if event[0] == "line":
                        failure = self._queued_failure_event(state)
                        if failure is not None:
                            return state, failure
                    return state, event
                for state in self._states:
                    if state.alive and state.process is not None and state.process.poll() is not None:
                        return state, ("eof", None)
                if not outstanding:
                    raise _benchmark.CoordinatorError(
                        "no in-flight worker job while waiting for an event"
                    )
                next_deadline_ns = min(
                    state.in_flight.dispatched_ns + timeout_ns for state in outstanding
                )
                remaining_seconds = max(
                    0.0,
                    (next_deadline_ns - time.perf_counter_ns()) / 1_000_000_000,
                )
                sleep_start = time.perf_counter_ns()
                try:
                    time.sleep(min(0.001, remaining_seconds))
                finally:
                    elapsed = max(0, time.perf_counter_ns() - sleep_start)
                    blocked_wait_ns += elapsed
                    self._record_worker_wait(elapsed)
        finally:
            cpu_elapsed = max(0, _thread_cpu_ns() - cpu_start)
            nonblocking_wall_ns = max(
                0,
                time.perf_counter_ns() - wall_start - blocked_wait_ns,
            )
            # If a short queue/poll operation is below the Windows thread
            # clock quantum, use only its explicitly nonblocking wall window.
            self._record_dispatch_queue_cpu(
                cpu_elapsed if cpu_elapsed > 0 else nonblocking_wall_ns
            )

    def _with_lifecycle_exclusion(self) -> Any:
        """Measure lifecycle barriers separately, never as worker wait."""

        owner = self
        original_staged = self._settle_staged_result
        original_completed = self._settle_completed_run

        def timed_staged(state: Any) -> Any:
            wall_start = time.perf_counter_ns()
            cpu_start = _thread_cpu_ns()
            try:
                return original_staged(state)
            finally:
                owner._audit_timing.add_lifecycle_time(
                    wall_clock_ns=max(0, time.perf_counter_ns() - wall_start),
                    cpu_ns=max(0, _thread_cpu_ns() - cpu_start),
                )

        def timed_completed() -> Any:
            wall_start = time.perf_counter_ns()
            cpu_start = _thread_cpu_ns()
            try:
                return original_completed()
            finally:
                owner._audit_timing.add_lifecycle_time(
                    wall_clock_ns=max(0, time.perf_counter_ns() - wall_start),
                    cpu_ns=max(0, _thread_cpu_ns() - cpu_start),
                )

        class _LifecycleContext:
            def __enter__(context_self: Any) -> None:
                owner._settle_staged_result = timed_staged
                owner._settle_completed_run = timed_completed

            def __exit__(
                context_self: Any,
                exc_type: Any,
                exc_value: Any,
                traceback: Any,
            ) -> None:
                del owner._settle_staged_result
                del owner._settle_completed_run

        return _LifecycleContext()

    def _with_protocol_timing(self) -> Any:
        original_encode = _benchmark.encode_job
        original_decode = _benchmark.decode_line
        owner = self

        def timed_encode(job: Mapping[str, Any]) -> str:
            try:
                return owner._json_timer.measure(lambda: original_encode(job))
            finally:
                owner._record_json_cpu(owner._json_timer.last_elapsed_ns)

        def timed_decode(line: str) -> dict[str, Any]:
            try:
                return owner._json_timer.measure(lambda: original_decode(line))
            finally:
                owner._record_json_cpu(owner._json_timer.last_elapsed_ns)

        class _ProtocolHookContext:
            def __enter__(context_self: Any) -> None:
                _benchmark.encode_job = timed_encode
                _benchmark.decode_line = timed_decode

            def __exit__(context_self: Any, exc_type: Any, exc_value: Any, traceback: Any) -> None:
                _benchmark.encode_job = original_encode
                _benchmark.decode_line = original_decode

        return _ProtocolHookContext()

    def run(self, *args: Any, **kwargs: Any) -> list[dict[str, Any]]:
        if kwargs.get("require_primary_integrity") is not True:
            raise PerformanceAuditRunnerError(
                "M4.2 audit runs require primary integrity validation"
            )
        self._audit_timing.reset()
        self._json_timer.reset()
        wall_start = time.perf_counter_ns()
        cpu_start = _thread_cpu_ns()
        with self._HOOK_LOCK:
            try:
                with self._with_lifecycle_exclusion():
                    with self._with_protocol_timing():
                        return super().run(*args, **kwargs)
            finally:
                self._audit_timing.finish(
                    time.perf_counter_ns() - wall_start,
                    _thread_cpu_ns() - cpu_start,
                )

    def coordinator_timing_snapshot(self) -> CoordinatorTimingSnapshot:
        return self._audit_timing.snapshot()


def _coordinator_snapshot_from_value(value: Any) -> CoordinatorTimingSnapshot:
    if isinstance(value, CoordinatorTimingSnapshot):
        return value
    if not isinstance(value, Mapping):
        raise CoordinatorTimingError("timing sample must be a snapshot or object")
    required = {
        "wall_clock_ns",
        "coordinator_cpu_ns",
        "worker_compute_wait_ns",
        "pipe_read_write_cpu_ns",
        "json_encode_decode_cpu_ns",
        "dispatch_queue_overhead_ns",
        "other_cpu_ns",
    }
    if set(value) != required:
        raise CoordinatorTimingError("timing sample has the wrong keys")
    try:
        return CoordinatorTimingSnapshot(**{key: value[key] for key in required})
    except (TypeError, ValueError) as error:
        raise CoordinatorTimingError(str(error)) from error


def aggregate_coordinator_timing(
    samples: Iterable[CoordinatorTimingSnapshot | Mapping[str, Any]],
) -> dict[str, Any]:
    """Aggregate independently measured samples into audit report fields."""

    snapshots = [_coordinator_snapshot_from_value(sample) for sample in samples]
    fields = (
        "wall_clock_ns",
        "coordinator_cpu_ns",
        "worker_compute_wait_ns",
        "pipe_read_write_cpu_ns",
        "json_encode_decode_cpu_ns",
        "dispatch_queue_overhead_ns",
        "other_cpu_ns",
    )
    totals = {field: sum(getattr(sample, field) for sample in snapshots) for field in fields}
    count = len(snapshots)

    def as_us(value_ns: int) -> int:
        return value_ns // 1_000

    coordinator_timing_us = {
        "worker_compute_wait": as_us(totals["worker_compute_wait_ns"]),
        "pipe_read_write_cpu": as_us(totals["pipe_read_write_cpu_ns"]),
        "json_encode_decode_cpu": as_us(totals["json_encode_decode_cpu_ns"]),
        "dispatch_queue_overhead": as_us(totals["dispatch_queue_overhead_ns"]),
        "other": as_us(totals["other_cpu_ns"]),
    }
    return {
        "samples": count,
        "wall_clock_us": as_us(totals["wall_clock_ns"]),
        "coordinator_cpu_us": as_us(totals["coordinator_cpu_ns"]),
        "coordinator_timing_us": coordinator_timing_us,
        "coordinator_timing_stats": {
            key: {
                "total_us": value,
                "calls": count,
                "mean_us_per_call": 0 if count == 0 else value // count,
            }
            for key, value in coordinator_timing_us.items()
        },
    }


def build_audit_jobs(
    master_seed: int,
    job_count: int,
    *,
    start_index: int = 0,
    max_steps: int = 2200,
    observation_mode: str = "full",
) -> list[dict[str, Any]]:
    """Build deterministic canonical jobs for a full or diagnostic sample."""

    if isinstance(job_count, bool) or not isinstance(job_count, int) or job_count < 0:
        raise ValueError("job_count must be a nonnegative integer")
    if isinstance(start_index, bool) or not isinstance(start_index, int) or start_index < 0:
        raise ValueError("start_index must be a nonnegative integer")
    if observation_mode not in {"full", "off_diagnostic"}:
        raise ValueError("observation_mode must be full or off_diagnostic")
    jobs = [
        derive_job_with_options(
            master_seed,
            start_index + offset,
            max_steps,
            mode="throughput",
            observation_mode=observation_mode,
            instrumentation=False,
            persist_trace=False,
        )
        for offset in range(job_count)
    ]
    if len({job["job_id"] for job in jobs}) != len(jobs):
        raise PerformanceAuditRunnerError("audit job generation produced duplicate IDs")
    return jobs


def run_audit_sample(
    worker_executable: str | Path | Sequence[str],
    *,
    master_seed: int,
    games: int,
    max_steps: int = 2200,
    observation_mode: str = "full",
    output_dir: str | Path | None = None,
    result_timeout_seconds: float = 120.0,
    require_primary_integrity: bool = True,
) -> dict[str, Any]:
    """Run one deterministic one-worker audit sample and parse its sidecars."""

    if require_primary_integrity is not True:
        raise PerformanceAuditRunnerError(
            "M4.2 audit runs require primary integrity validation"
        )
    jobs = build_audit_jobs(
        master_seed,
        games,
        max_steps=max_steps,
        observation_mode=observation_mode,
    )
    worker_output_dir = (
        Path(output_dir)
        if output_dir is not None
        else Path(tempfile.mkdtemp(prefix="ocgforge-m4-performance-audit-"))
    )
    pool = AuditWorkerPool(
        worker_executable,
        worker_count=1,
        output_dir=worker_output_dir,
        result_timeout_seconds=result_timeout_seconds,
    )
    results: list[dict[str, Any]] = []
    ready_messages: tuple[dict[str, Any], ...] = ()
    try:
        pool.start()
        ready_messages = pool.ready_messages
        results = pool.run(
            jobs,
            require_primary_integrity=require_primary_integrity,
        )
        timing_snapshot = pool.coordinator_timing_snapshot()
    finally:
        pool.close()

    expected_ids = [str(job["job_id"]) for job in jobs]
    actual_ids = [str(result.get("job_id")) for result in results]
    if actual_ids != sorted(expected_ids, key=_job_sort_key):
        raise PerformanceAuditRunnerError(
            "worker result IDs do not match the deterministic audit workload"
        )
    stderr_paths = [
        worker["stderr_path"]
        for worker in pool.last_run_metadata.get("workers", [])
        if isinstance(worker, Mapping) and isinstance(worker.get("stderr_path"), str)
    ]
    sidecars = parse_audit_sidecars(stderr_paths, expected_ids)
    observation_mode_label = (
        OFF_DIAGNOSTIC_LABEL if observation_mode == "off_diagnostic" else "FULL"
    )
    return {
        "jobs": jobs,
        "results": results,
        "ready_messages": [dict(message) for message in ready_messages],
        "metadata": pool.last_run_metadata,
        "worker_stderr_paths": stderr_paths,
        "sidecars": sidecars,
        "coordinator_timing": timing_snapshot.as_microseconds(),
        "observation_mode": observation_mode,
        "observation_mode_label": observation_mode_label,
        "training_throughput_eligible": observation_mode == "full",
    }


def _job_sort_key(job_id: str) -> tuple[int, str]:
    try:
        prefix, suffix = job_id.rsplit("-", 1)
        if prefix == "m4" and suffix.isdigit():
            return int(suffix), job_id
    except (AttributeError, ValueError):
        pass
    return (2**63 - 1, job_id)


# ---------------------------------------------------------------------------
# M4.2 report/schema slice
# ---------------------------------------------------------------------------

PERFORMANCE_AUDIT_SCHEMA_VERSION = "ocgforge.m4.performance_audit.v1"
PERFORMANCE_AUDIT_STATUS = "M4.2 PERFORMANCE AUDIT PASS"
PERFORMANCE_AUDIT_MATCHUP = (
    "Swordsoul Tenyi ML v1 vs Salamangreat ML v1"
)
PERFORMANCE_AUDIT_REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
_PRIMARY_TIMING_KEYS = (
    "core_process",
    "protocol_candidate",
    "continuation",
    "observation",
    "trace_hash",
    "serialization",
    "other",
    "trace_persistence",
)
_PRIMARY_COUNTER_KEYS = (
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
_READY_IDENTITY_KEYS = (
    "schema",
    "protocol_version",
    "worker_identity",
    "rules_bundle_id",
    "core_patchset_sha256",
    "deck_hashes",
    "format_id",
    "duel_mode_name",
    "duel_flags",
    "compiler_identity",
    "build_type",
)
_WORKLOAD_IDENTITY_KEYS = (
    "job_id",
    "seed",
    "seat_assignment",
    "starting_player",
    "max_steps",
    "canonical_rules_id",
    "mode",
    "instrumentation",
    "persist_trace",
)
_CPU_COORDINATOR_KEYS = (
    "pipe_read_write_cpu",
    "json_encode_decode_cpu",
    "dispatch_queue_overhead",
    "other",
)
_QUERY_CALL_SITE_CLASSIFICATION = (
    {
        "call_site": "observation_builder.cpp: query_field",
        "query_type": "OCG_DuelQueryField",
        "calls_per_observation": 1,
        "purpose": "PlayerObservation field snapshot",
        "same_state_duplicate": False,
        "duplicate_classification": "not duplicate within PlayerObservation",
        "safe_candidate_for_reuse": (
            "Only after semantic equivalence is proved for the field snapshot"
        ),
    },
    {
        "call_site": "canonical_simulation.cpp: public_state_hash terminal/decision",
        "query_type": "OCG_DuelQueryField",
        "calls_per_observation": 1,
        "purpose": "Observation-equivalent terminal/decision public-state hash",
        "same_state_duplicate": True,
        "duplicate_classification": (
            "same-state duplicate relative to PlayerObservation in the decision path"
        ),
        "safe_candidate_for_reuse": (
            "Only with a hash-equivalence proof including perspective and state lifetime"
        ),
    },
    {
        "call_site": "observation_builder.cpp: query_location loops",
        "query_type": "OCG_DuelQueryLocation",
        "calls_per_observation": 12,
        "purpose": (
            "Privacy-safe zone/entity snapshot: HAND/MZONE/SZONE/GRAVE/REMOVED "
            "for both players plus EXTRA for both players"
        ),
        "same_state_duplicate": False,
        "duplicate_classification": "distinct zone/player snapshots; do not eliminate",
        "safe_candidate_for_reuse": "No elimination candidate; preserve all zone queries",
    },
    {
        "call_site": "observation_builder.cpp: query_card",
        "query_type": "OCG_DuelQuery",
        "calls_per_observation": 0,
        "purpose": "Individual card/overlay/relationship enrichment",
        "same_state_duplicate": False,
        "duplicate_classification": (
            "Observed only when nonzero; no calls in the canonical baseline"
        ),
        "safe_candidate_for_reuse": "No change proposed; sidecar tracks any future nonzero path",
    },
)


class PerformanceAuditReportError(RuntimeError):
    """Raised when an M4.2 report cannot be treated as audit evidence."""


def _report_uint(value: Any, name: str, *, positive: bool = False) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < (1 if positive else 0)
        or value > UINT64_MAX
    ):
        qualifier = "positive" if positive else "nonnegative"
        raise PerformanceAuditReportError(f"{name} must be a {qualifier} integer")
    return value


def _report_mapping(value: Any, name: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise PerformanceAuditReportError(f"{name} must be an object")
    return value


def _report_sequence(value: Any, name: str) -> Sequence[Any]:
    if not isinstance(value, Sequence) or isinstance(value, (str, bytes, bytearray)):
        raise PerformanceAuditReportError(f"{name} must be an array")
    return value


def _report_timing(total_us: int, calls: int) -> dict[str, int]:
    return {
        "total_us": total_us,
        "calls": calls,
        "mean_us_per_call": 0 if calls == 0 else total_us // calls,
    }


def _report_timing_group(
    values: Mapping[str, Any],
    name: str,
    keys: Sequence[str],
) -> dict[str, dict[str, int]]:
    if set(values) != set(keys):
        raise PerformanceAuditReportError(f"{name} has the wrong keys")
    totals: dict[str, int] = {}
    calls: dict[str, int] = {}
    for key in keys:
        bucket = _report_mapping(values[key], f"{name}.{key}")
        if set(bucket) != {"total_us", "calls", "mean_us_per_call"}:
            raise PerformanceAuditReportError(f"{name}.{key} has the wrong keys")
        total = _report_uint(bucket["total_us"], f"{name}.{key}.total_us")
        call_count = _report_uint(bucket["calls"], f"{name}.{key}.calls")
        mean = _report_uint(bucket["mean_us_per_call"], f"{name}.{key}.mean_us_per_call")
        expected_mean = 0 if call_count == 0 else total // call_count
        if mean != expected_mean:
            raise PerformanceAuditReportError(
                f"{name}.{key}.mean_us_per_call is inconsistent"
            )
        totals[key] = total
        calls[key] = call_count
    return {key: _report_timing(totals[key], calls[key]) for key in keys}


def _report_counter_group(
    values: Mapping[str, Any],
    name: str,
    keys: Sequence[str],
) -> dict[str, int]:
    if set(values) != set(keys):
        raise PerformanceAuditReportError(f"{name} has the wrong keys")
    return {key: _report_uint(values[key], f"{name}.{key}") for key in keys}


def _aggregate_sidecar_timing(
    sidecars: Mapping[str, Mapping[str, Any]],
    field: str,
    keys: Sequence[str],
) -> dict[str, dict[str, int]]:
    totals = {key: 0 for key in keys}
    calls = {key: 0 for key in keys}
    for job_id, sidecar in sidecars.items():
        group = _report_timing_group(
            _report_mapping(sidecar.get(field), f"sidecars[{job_id}].{field}"),
            f"sidecars[{job_id}].{field}",
            keys,
        )
        for key in keys:
            totals[key] += group[key]["total_us"]
            calls[key] += group[key]["calls"]
    return {key: _report_timing(totals[key], calls[key]) for key in keys}


def _aggregate_sidecar_counters(
    sidecars: Mapping[str, Mapping[str, Any]],
    field: str,
    keys: Sequence[str],
) -> dict[str, int]:
    totals = {key: 0 for key in keys}
    for job_id, sidecar in sidecars.items():
        group = _report_counter_group(
            _report_mapping(sidecar.get(field), f"sidecars[{job_id}].{field}"),
            f"sidecars[{job_id}].{field}",
            keys,
        )
        for key in keys:
            totals[key] += group[key]
    return totals


def _aggregate_sidecar_zones(
    sidecars: Mapping[str, Mapping[str, Any]],
) -> dict[str, dict[str, int]]:
    totals = {
        zone: {"entities_projected": 0, "identity_known": 0, "redacted": 0}
        for zone in ENTITY_ZONE_KEYS
    }
    for job_id, sidecar in sidecars.items():
        zones = _report_mapping(
            sidecar.get("entities_by_zone"), f"sidecars[{job_id}].entities_by_zone"
        )
        if set(zones) != set(ENTITY_ZONE_KEYS):
            raise PerformanceAuditReportError(
                f"sidecars[{job_id}].entities_by_zone has the wrong keys"
            )
        for zone in ENTITY_ZONE_KEYS:
            counters = _report_mapping(zones[zone], f"entities_by_zone.{zone}")
            if set(counters) != {"entities_projected", "identity_known", "redacted"}:
                raise PerformanceAuditReportError(
                    f"entities_by_zone.{zone} has the wrong keys"
                )
            for key in counters:
                totals[zone][key] += _report_uint(
                    counters[key], f"entities_by_zone.{zone}.{key}"
                )
    for zone, counters in totals.items():
        if counters["identity_known"] + counters["redacted"] != counters["entities_projected"]:
            raise PerformanceAuditReportError(
                f"privacy identity partition does not close for zone {zone}"
            )
    return totals


def _canonical_ready_identity(ready: Mapping[str, Any]) -> dict[str, Any]:
    try:
        validate_ready(ready)
    except Exception as error:
        raise PerformanceAuditReportError(
            f"worker ready identity gate failed: {error}"
        ) from error
    return {key: ready[key] for key in _READY_IDENTITY_KEYS}


def _canonical_job_identity(job: Mapping[str, Any]) -> dict[str, Any]:
    if set(_WORKLOAD_IDENTITY_KEYS).difference(job):
        raise PerformanceAuditReportError("audit job is missing canonical identity fields")
    return {key: job[key] for key in _WORKLOAD_IDENTITY_KEYS}


def _validate_coordinator_timing(sample: Mapping[str, Any]) -> dict[str, Any]:
    value = _report_mapping(sample.get("coordinator_timing"), "coordinator_timing")
    required = {
        "wall_clock_us",
        "coordinator_cpu_us",
        "coordinator_timing_us",
        "coordinator_timing_stats",
    }
    if set(value) != required:
        raise PerformanceAuditReportError("coordinator_timing has the wrong keys")
    wall_clock_us = _report_uint(
        value["wall_clock_us"], "coordinator_timing.wall_clock_us", positive=True
    )
    coordinator_cpu_us = _report_uint(
        value["coordinator_cpu_us"], "coordinator_timing.coordinator_cpu_us"
    )
    domains = _report_counter_group(
        _report_mapping(value["coordinator_timing_us"], "coordinator_timing_us"),
        "coordinator_timing_us",
        COORDINATOR_TIMING_KEYS,
    )
    stats = _report_timing_group(
        _report_mapping(value["coordinator_timing_stats"], "coordinator_timing_stats"),
        "coordinator_timing_stats",
        COORDINATOR_TIMING_KEYS,
    )
    for key in COORDINATOR_TIMING_KEYS:
        if stats[key]["total_us"] != domains[key] or stats[key]["calls"] != 1:
            raise PerformanceAuditReportError(
                f"coordinator timing stats do not match coordinator_timing_us.{key}"
            )
    cpu_domain_total_us = sum(domains[key] for key in _CPU_COORDINATOR_KEYS)
    if cpu_domain_total_us > coordinator_cpu_us:
        raise PerformanceAuditReportError(
            "coordinator CPU domains exceed coordinator CPU time"
        )
    return {
        "wall_clock_us": wall_clock_us,
        "coordinator_cpu_us": coordinator_cpu_us,
        "domains": stats,
        "cpu_domain_total_us": cpu_domain_total_us,
        "cpu_domain_gap_us": coordinator_cpu_us - cpu_domain_total_us,
    }


def _validate_sidecar_completeness(
    sidecars: Mapping[str, Any],
    expected_job_ids: Sequence[str],
) -> dict[str, Mapping[str, Any]]:
    if set(sidecars) != set(expected_job_ids) or len(sidecars) != len(expected_job_ids):
        raise PerformanceAuditReportError("native sidecar set does not match the workload")
    normalized: dict[str, Mapping[str, Any]] = {}
    for job_id in expected_job_ids:
        sidecar = _report_mapping(sidecars[job_id], f"sidecars[{job_id}]")
        if sidecar.get("job_id") != job_id:
            raise PerformanceAuditReportError(f"sidecar job_id mismatch for {job_id}")
        if sidecar.get("schema") != AUDIT_SIDECAR_SCHEMA:
            raise PerformanceAuditReportError(f"sidecar schema mismatch for {job_id}")
        if sidecar.get("type") != AUDIT_SIDECAR_TYPE:
            raise PerformanceAuditReportError(f"sidecar type mismatch for {job_id}")
        if sidecar.get("_legacy_setup_timing_missing_script_load") is True:
            raise PerformanceAuditReportError(
                f"sidecar {job_id} lacks measured script_load timing"
            )
        setup = _report_mapping(
            sidecar.get("setup_timing_us"),
            f"sidecars[{job_id}].setup_timing_us",
        )
        if set(setup) != set(_COMPLETE_SETUP_TIMING_KEYS):
            raise PerformanceAuditReportError(
                f"sidecar {job_id} lacks complete setup/script timing"
            )
        normalized[job_id] = sidecar
    return normalized


def _build_sample_summary(
    sample: Mapping[str, Any],
    *,
    expected_jobs: Sequence[Mapping[str, Any]],
    observation_mode: str,
) -> tuple[dict[str, Any], dict[str, Any]]:
    if sample.get("observation_mode") != observation_mode:
        raise PerformanceAuditReportError(
            f"sample observation mode mismatch: expected {observation_mode}"
        )
    expected_label = (
        OFF_DIAGNOSTIC_LABEL if observation_mode == "off_diagnostic" else "FULL"
    )
    if sample.get("observation_mode_label") != expected_label:
        raise PerformanceAuditReportError("sample classification label mismatch")
    if sample.get("training_throughput_eligible") is not (observation_mode == "full"):
        raise PerformanceAuditReportError(
            "sample training-throughput classification mismatch"
        )

    jobs_value = _report_sequence(sample.get("jobs"), f"{observation_mode}.jobs")
    jobs = [
        _report_mapping(job, f"{observation_mode}.jobs[{index}]")
        for index, job in enumerate(jobs_value)
    ]
    if [dict(job) for job in jobs] != [dict(job) for job in expected_jobs]:
        raise PerformanceAuditReportError(
            "sample jobs do not match the canonical workload"
        )
    expected_job_ids = [str(job["job_id"]) for job in expected_jobs]

    results_value = _report_sequence(sample.get("results"), f"{observation_mode}.results")
    results = [
        _report_mapping(result, f"{observation_mode}.results[{index}]")
        for index, result in enumerate(results_value)
    ]
    metadata = _report_mapping(sample.get("metadata"), f"{observation_mode}.metadata")
    if metadata.get("worker_count") != 1:
        raise PerformanceAuditReportError("M4.2 report requires exactly one worker")
    try:
        validate_complete_results(results, expected_job_ids, metadata)
        for result in results:
            assert_primary_integrity(result)
    except Exception as error:
        raise PerformanceAuditReportError(
            f"{observation_mode} primary result/integrity gate failed: {error}"
        ) from error

    ready_messages = _report_sequence(
        sample.get("ready_messages"), f"{observation_mode}.ready_messages"
    )
    if len(ready_messages) != 1:
        raise PerformanceAuditReportError("one-worker audit requires one ready identity")
    ready_identity = _canonical_ready_identity(
        _report_mapping(ready_messages[0], "ready")
    )

    sidecars = _validate_sidecar_completeness(
        _report_mapping(sample.get("sidecars"), f"{observation_mode}.sidecars"),
        expected_job_ids,
    )
    observation_timing = _aggregate_sidecar_timing(
        sidecars, "observation_timing_us", OBSERVATION_TIMING_KEYS
    )
    observation_counters = _aggregate_sidecar_counters(
        sidecars, "observation_counters", OBSERVATION_COUNTER_KEYS
    )
    detail_counters = _aggregate_sidecar_counters(
        sidecars, "observation_detail_counters", OBSERVATION_DETAIL_COUNTER_KEYS
    )
    setup_timing = _aggregate_sidecar_timing(
        sidecars, "setup_timing_us", _COMPLETE_SETUP_TIMING_KEYS
    )
    auxiliary_timing = _aggregate_sidecar_timing(
        sidecars, "auxiliary_timing_us", AUXILIARY_TIMING_KEYS
    )
    entities_by_zone = _aggregate_sidecar_zones(sidecars)
    observation_total_us = sum(
        _report_uint(
            sidecar.get("observation_total_us"),
            f"{job_id}.observation_total_us",
        )
        for job_id, sidecar in sidecars.items()
    )
    if (
        sum(bucket["total_us"] for bucket in observation_timing.values())
        != observation_total_us
    ):
        raise PerformanceAuditReportError(
            "observation timing buckets do not close over outer observation time"
        )

    coordinator = _validate_coordinator_timing(sample)
    coordinator_group = {
        key: coordinator["domains"][key]["total_us"]
        for key in COORDINATOR_TIMING_KEYS
    }
    telemetry = {
        "observation_timing_us": {
            key: bucket["total_us"] for key, bucket in observation_timing.items()
        },
        "observation_counters": observation_counters,
        "coordinator_timing_us": coordinator_group,
    }
    try:
        validate_audit_telemetry(
            telemetry,
            require=True,
            outer_observation_us=observation_total_us,
        )
    except PerformanceAuditContractError as error:
        raise PerformanceAuditReportError(str(error)) from error

    primary = aggregate_results(
        results,
        metadata,
        wall_clock_seconds=coordinator["wall_clock_us"] / 1_000_000,
        games_requested=len(expected_jobs),
        workers_requested=1,
    )
    primary_timing = {
        key: _report_uint(
            primary["timing_buckets_us"][key],
            f"{observation_mode}.primary.timing_buckets_us.{key}",
        )
        for key in _PRIMARY_TIMING_KEYS
    }
    primary_counters = {
        key: _report_uint(
            primary["operation_counters"][key],
            f"{observation_mode}.primary.operation_counters.{key}",
        )
        for key in _PRIMARY_COUNTER_KEYS
    }

    sidecar_to_primary = {
        "query_field_calls": "ocg_duel_query_field",
        "query_location_calls": "ocg_duel_query_location",
        "query_individual_calls": "ocg_duel_query",
        "observations": "observations",
        "entities_projected": "entities_projected",
        "script_loads": "script_loads",
    }
    for sidecar_key, primary_key in sidecar_to_primary.items():
        if observation_counters[sidecar_key] != primary_counters[primary_key]:
            raise PerformanceAuditReportError(
                f"sidecar/result counter mismatch for {sidecar_key}"
            )
    if detail_counters["script_reader_requests"] != primary_counters["script_reader_requests"]:
        raise PerformanceAuditReportError(
            "sidecar/result counter mismatch for script_reader_requests"
        )
    if observation_counters["query_field_calls"] != (
        detail_counters["observation_query_field_calls"]
        + detail_counters["public_state_hash_query_field_calls"]
    ):
        raise PerformanceAuditReportError("query-field detail counters do not close")
    if (
        observation_counters["identity_known_entities"]
        + observation_counters["redacted_entities"]
        != observation_counters["entities_projected"]
    ):
        raise PerformanceAuditReportError("observation privacy counters do not close")
    if setup_timing[_SCRIPT_LOAD_SETUP_KEY]["calls"] != observation_counters["script_loads"]:
        raise PerformanceAuditReportError(
            "script-load timing/count evidence does not close"
        )

    if observation_mode == "full":
        observations = observation_counters["observations"]
        if observations <= 0:
            raise PerformanceAuditReportError("FULL sample has no observations")
        if observation_counters["query_field_calls"] != 2 * observations:
            raise PerformanceAuditReportError(
                "FULL sample does not explain 2 field queries per observation"
            )
        if observation_counters["query_location_calls"] != 12 * observations:
            raise PerformanceAuditReportError(
                "FULL sample does not explain 12 location queries per observation"
            )
        if detail_counters["observation_query_field_calls"] != observations:
            raise PerformanceAuditReportError(
                "PlayerObservation field query count is not one per observation"
            )
        if detail_counters["public_state_hash_query_field_calls"] != observations:
            raise PerformanceAuditReportError(
                "public_state_hash field query count is not one per observation-equivalent call"
            )
    else:
        if observation_total_us != 0 or any(
            bucket["total_us"] != 0 for bucket in observation_timing.values()
        ):
            raise PerformanceAuditReportError(
                "off_diagnostic sample contains observation timing"
            )
        if (
            observation_counters["observations"] != 0
            or observation_counters["entities_projected"] != 0
        ):
            raise PerformanceAuditReportError(
                "off_diagnostic sample contains observation counters"
            )

    simulation_total_us = _report_uint(
        primary["simulation_elapsed_total_us"],
        f"{observation_mode}.primary.simulation_elapsed_total_us",
        positive=True,
    )
    primary_observation_total_us = primary_timing["observation"]
    protocol_candidate_total_us = primary_timing["protocol_candidate"]

    def percentage(numerator: int, denominator: int) -> float:
        return round((numerator * 100.0) / denominator, 6) if denominator else 0.0

    primary_hashes = [
        {
            "job_id": str(result["job_id"]),
            "gameplay_hash": str(result["gameplay_hash"]),
        }
        for result in sorted(
            results, key=lambda item: _job_sort_key(str(item["job_id"]))
        )
    ]
    summary = {
        "observation_mode": observation_mode,
        "classification_label": expected_label,
        "training_throughput_eligible": observation_mode == "full",
        "primary": {
            "games_requested": primary["games_requested"],
            "games_completed": primary["games_completed"],
            "terminal_games": primary["terminal_games"],
            "failed_games": primary["failed_games"],
            "job_ids": expected_job_ids,
            "gameplay_hashes": primary_hashes,
            "integrity_gate": True,
            "lifecycle_gate": True,
            "timing_buckets_us": primary_timing,
            "operation_counters": primary_counters,
            "errors": primary["errors"],
        },
        "timing_us": {
            "outer_observation": _report_timing(
                observation_total_us, observation_counters["observations"]
            ),
            "observation": observation_timing,
            "setup": setup_timing,
            "auxiliary": auxiliary_timing,
            "coordinator": coordinator["domains"],
            "coordinator_wall_clock_us": coordinator["wall_clock_us"],
            "coordinator_cpu_us": coordinator["coordinator_cpu_us"],
            "coordinator_cpu_domain_total_us": coordinator["cpu_domain_total_us"],
            "coordinator_cpu_domain_gap_us": coordinator["cpu_domain_gap_us"],
        },
        "counters": {
            "observation": observation_counters,
            "detail": detail_counters,
            "entities_by_zone": entities_by_zone,
        },
        "runtime": {
            "worker_local_simulation_elapsed_total_us": simulation_total_us,
            "primary_observation_total_us": primary_observation_total_us,
            "outer_observation_total_us": observation_total_us,
            "observation_fraction_of_simulation_percent": percentage(
                primary_observation_total_us, simulation_total_us
            ),
            "outer_observation_fraction_of_simulation_percent": percentage(
                observation_total_us, simulation_total_us
            ),
            "protocol_candidate_fraction_of_simulation_percent": percentage(
                protocol_candidate_total_us, simulation_total_us
            ),
            "script_loads": observation_counters["script_loads"],
            "script_reader_requests": detail_counters["script_reader_requests"],
            "worker_compute_wait_is_not_cpu": True,
        },
    }
    return summary, ready_identity


def _load_baseline_candidate_evidence() -> tuple[int, float]:
    path = PERFORMANCE_AUDIT_REPOSITORY_ROOT / "docs" / "m4" / "m4_baseline.json"
    try:
        baseline = json.loads(path.read_text(encoding="utf-8"))
        row = baseline["evidence"]["rows_by_worker"]["1"]
        counters = row["operation_counters"]
        percentages = row["timing_percentages"]
        candidate_max = int(counters["candidate_max"])
        protocol_fraction = float(percentages["protocol_candidate"])
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise PerformanceAuditReportError(
            f"M4 baseline candidate evidence is unavailable: {error}"
        ) from error
    if candidate_max != 1344:
        raise PerformanceAuditReportError(
            f"M4 baseline candidate_max evidence drifted: expected 1344, got {candidate_max}"
        )
    if not math.isfinite(protocol_fraction) or protocol_fraction < 0:
        raise PerformanceAuditReportError(
            "M4 baseline protocol_candidate fraction is invalid"
        )
    return candidate_max, protocol_fraction


def _build_query_call_site_table(full: Mapping[str, Any]) -> list[dict[str, Any]]:
    counters = full["counters"]["observation"]
    detail = full["counters"]["detail"]
    observations = counters["observations"]
    measured = (
        detail["observation_query_field_calls"],
        detail["public_state_hash_query_field_calls"],
        counters["query_location_calls"],
        counters["query_individual_calls"],
    )
    result: list[dict[str, Any]] = []
    for entry, total_calls in zip(_QUERY_CALL_SITE_CLASSIFICATION, measured):
        item = dict(entry)
        item["measured_total_calls"] = total_calls
        item["measured_calls_per_observation"] = (
            round(total_calls / observations, 6) if observations else 0.0
        )
        result.append(item)
    return result


def _build_entity_audit(full: Mapping[str, Any]) -> dict[str, Any]:
    counters = full["counters"]["observation"]
    detail = full["counters"]["detail"]
    zones = full["counters"]["entities_by_zone"]
    projected = counters["entities_projected"]
    known = counters["identity_known_entities"]
    redacted = counters["redacted_entities"]
    static_lookups = counters["static_card_data_lookups"]
    return {
        "every_zone_reported": set(zones) == set(ENTITY_ZONE_KEYS),
        "entities_by_zone": zones,
        "entities_projected": projected,
        "identity_known": known,
        "redacted": redacted,
        "static_printed_metadata_lookups": static_lookups,
        "current_property_projections": counters["current_property_projections"],
        "relationship_objects": counters["relationship_objects"],
        "relationship_resolution_events": detail["relationship_resolution_events"],
        "allocation_copy_events": counters["allocation_copy_events"],
        "immutable_printed_metadata_reconstructed": static_lookups > 0,
        "evidence": (
            f"The audit counted {static_lookups} repeated static_card_data lookups "
            "during entity projection; immutable printed metadata is therefore "
            "reconstructed in the measured observation path. No representation change "
            "was implemented."
        ),
        "representation_changed": False,
        "privacy_partition_closes": known + redacted == projected,
    }


def _build_optimization_candidates(full: Mapping[str, Any]) -> list[dict[str, Any]]:
    outer = full["timing_us"]["outer_observation"]["total_us"]
    buckets = full["timing_us"]["observation"]
    candidates = [
        {
            "candidate": "entity_projection_static_metadata_reuse",
            "bucket": "observation_entity_projection",
            "semantic_risk": "high",
            "complexity": "high",
            "suspected_redundant_work": "Repeated immutable printed-card metadata lookups while projecting entities",
            "proposed_optimization_concept": "Reuse immutable printed metadata only after visibility and state-equivalence proof",
            "semantic_privacy_risk": "Could leak or attach metadata to a redacted entity if identity boundaries drift",
            "required_equivalence_test": "Full canonical observation byte/hash equivalence plus known/redacted per-zone privacy fixture",
        },
        {
            "candidate": "query_field_reuse_for_public_state_hash",
            "bucket": "observation_query_field",
            "semantic_risk": "high",
            "complexity": "medium",
            "suspected_redundant_work": "The public_state_hash path repeats a field query in the decision path",
            "proposed_optimization_concept": "Reuse the already captured field bytes only after hash/perspective/lifetime equivalence proof",
            "semantic_privacy_risk": "A stale or wrong-perspective field snapshot would change public hashes or expose hidden state",
            "required_equivalence_test": "Per-decision field bytes, public_state_hash, PlayerObservation hash, gameplay hash, and privacy equivalence",
        },
        {
            "candidate": "canonical_serialization_copy_reduction",
            "bucket": "observation_canonical_serialization",
            "semantic_risk": "medium",
            "complexity": "medium",
            "suspected_redundant_work": "Canonical serialization copies/sorts observation collections before hashing",
            "proposed_optimization_concept": "Reduce only proven temporary copies while preserving canonical ordering and hash bytes",
            "semantic_privacy_risk": "Ordering or mutation mistakes would alter observation hashes",
            "required_equivalence_test": "Canonical serialized bytes and observation_hash equality across all deterministic sample rows",
        },
        {
            "candidate": "relationship_projection_reuse",
            "bucket": "observation_relationship_projection",
            "semantic_risk": "high",
            "complexity": "medium",
            "suspected_redundant_work": "Relationship resolution and relationship-object construction are repeated across observations",
            "proposed_optimization_concept": "Reuse only immutable relationship evidence after lifecycle and locator equivalence proof",
            "semantic_privacy_risk": "Incorrect relationship freshness could expose hidden attachments or stale links",
            "required_equivalence_test": "Relationship object byte equality and overlay/privacy fixture equivalence",
        },
        {
            "candidate": "visibility_privacy_projection_reduction",
            "bucket": "observation_visibility_privacy",
            "semantic_risk": "high",
            "complexity": "high",
            "suspected_redundant_work": "Visibility and redaction decisions are reevaluated for every projected entity",
            "proposed_optimization_concept": "Narrowly reduce repeated predicates only with a public-state/privacy proof",
            "semantic_privacy_risk": "Any false positive identity visibility is a hard privacy failure",
            "required_equivalence_test": "Known/redacted matrix over every zone and opponent hidden-information fixture",
        },
    ]
    ranked: list[dict[str, Any]] = []
    risk_order = {"low": 0, "medium": 1, "high": 2}
    complexity_order = {"low": 0, "medium": 1, "high": 2}
    for candidate in candidates:
        total_us = buckets[candidate["bucket"]]["total_us"]
        if total_us == 0:
            continue
        item = dict(candidate)
        item["measured_total_us"] = total_us
        item["measured_runtime_fraction_percent"] = round(
            total_us * 100.0 / outer, 6
        ) if outer else 0.0
        ranked.append(item)
    ranked.sort(
        key=lambda item: (
            -item["measured_runtime_fraction_percent"],
            risk_order[item["semantic_risk"]],
            complexity_order[item["complexity"]],
            item["candidate"],
        )
    )
    for rank, item in enumerate(ranked, start=1):
        item["rank"] = rank
    return ranked


def build_performance_audit_report(
    full_sample: Mapping[str, Any],
    off_diagnostic_sample: Mapping[str, Any],
    *,
    master_seed: int,
    games: int,
    max_steps: int,
    worker_executable: str | Path | Sequence[str],
    matchup: str = PERFORMANCE_AUDIT_MATCHUP,
) -> dict[str, Any]:
    """Build one strict M4.2 artifact from two completed native samples.

    The function raises before returning a report for missing sidecars, failed
    primary results, identity mismatch, privacy mismatch, or incomplete
    setup/script timing. Callers cannot write a placeholder artifact.
    """

    if isinstance(master_seed, bool) or not isinstance(master_seed, int) or master_seed < 0:
        raise PerformanceAuditReportError("master_seed must be a nonnegative integer")
    if isinstance(games, bool) or not isinstance(games, int) or games <= 0:
        raise PerformanceAuditReportError("games must be a positive integer")
    if isinstance(max_steps, bool) or not isinstance(max_steps, int) or max_steps <= 0:
        raise PerformanceAuditReportError("max_steps must be a positive integer")
    if not isinstance(matchup, str) or matchup != PERFORMANCE_AUDIT_MATCHUP:
        raise PerformanceAuditReportError("M4.2 requires the canonical matchup identity")

    full_jobs = build_audit_jobs(
        master_seed, games, max_steps=max_steps, observation_mode="full"
    )
    off_jobs = build_audit_jobs(
        master_seed, games, max_steps=max_steps, observation_mode="off_diagnostic"
    )
    full, full_ready = _build_sample_summary(
        _report_mapping(full_sample, "full_sample"),
        expected_jobs=full_jobs,
        observation_mode="full",
    )
    off, off_ready = _build_sample_summary(
        _report_mapping(off_diagnostic_sample, "off_diagnostic_sample"),
        expected_jobs=off_jobs,
        observation_mode="off_diagnostic",
    )
    if full_ready != off_ready:
        raise PerformanceAuditReportError("FULL and off_diagnostic worker identities differ")
    if [_canonical_job_identity(job) for job in full_jobs] != [
        _canonical_job_identity(job) for job in off_jobs
    ]:
        raise PerformanceAuditReportError("FULL and off_diagnostic workloads differ")
    if full["primary"]["gameplay_hashes"] != off["primary"]["gameplay_hashes"]:
        raise PerformanceAuditReportError("FULL and off_diagnostic gameplay hashes differ")

    candidate_max, baseline_protocol_fraction = _load_baseline_candidate_evidence()
    measured_protocol_fraction = full["runtime"][
        "protocol_candidate_fraction_of_simulation_percent"
    ]
    full_results = _report_sequence(
        _report_mapping(full_sample, "full_sample").get("results"),
        "full_sample.results",
    )
    candidate_audit = {
        "candidate_max": candidate_max,
        "baseline_evidence": "docs/m4/m4_baseline.json:evidence.rows_by_worker.1.operation_counters.candidate_max",
        "sample_candidate_max_sum": full["primary"]["operation_counters"]["candidate_max"],
        "sample_candidate_max_per_game": max(
            int(_report_mapping(result, "full_sample.result")["counters"]["candidate_max"])
            for result in full_results
        ),
        "baseline_protocol_candidate_fraction_percent": baseline_protocol_fraction,
        "measured_protocol_candidate_fraction_percent": measured_protocol_fraction,
        "candidate_generation_optimization_proposed": False,
        "classification": (
            "Candidate generation remains out of scope: protocol_candidate is "
            "reported, while the canonical baseline evidence remains the decision "
            "gate for not optimizing this path in M4.2."
        ),
    }
    entity_audit = _build_entity_audit(full)
    optimization_candidates = _build_optimization_candidates(full)
    if not optimization_candidates:
        raise PerformanceAuditReportError("FULL sample produced no measurable observation bucket")
    first_candidate = optimization_candidates[0]
    worker_command = (
        [str(value) for value in worker_executable]
        if not isinstance(worker_executable, (str, Path))
        else [str(worker_executable)]
    )
    if not worker_command or any(not value for value in worker_command):
        raise PerformanceAuditReportError("worker_executable must be explicit and nonempty")

    report: dict[str, Any] = {
        "schema_version": PERFORMANCE_AUDIT_SCHEMA_VERSION,
        "status": PERFORMANCE_AUDIT_STATUS,
        "optimization_implemented": False,
        "begin_m5": False,
        "off_diagnostic_label": OFF_DIAGNOSTIC_LABEL,
        "workload": {
            "matchup": matchup,
            "master_seed": master_seed,
            "games": games,
            "max_steps": max_steps,
            "workers": 1,
            "mode": "throughput",
            "instrumentation": False,
            "persist_trace": False,
            "job_index_range": {"start": 0, "stop": games, "count": games},
        },
        "provenance": {
            "worker_command": worker_command,
            "native_sidecar_prefix": AUDIT_SIDECAR_PREFIX,
            "native_sidecar_schema": AUDIT_SIDECAR_SCHEMA,
            "hardware": default_hardware_metadata(),
        },
        "canonical_identity": {
            "full": full_ready,
            "off_diagnostic": off_ready,
            "same_across_samples": True,
        },
        "samples": {"full": full, "off_diagnostic": off},
        "query_call_site_classification": _build_query_call_site_table(full),
        "entity_projection_audit": entity_audit,
        "candidate_audit": candidate_audit,
        "optimization_candidates": optimization_candidates,
        "cross_check": {
            "same_canonical_identity": True,
            "same_canonical_workload": True,
            "gameplay_hashes_equal": True,
            "full_worker_local_runtime_us": full["runtime"]["worker_local_simulation_elapsed_total_us"],
            "off_worker_local_runtime_us": off["runtime"]["worker_local_simulation_elapsed_total_us"],
            "worker_local_runtime_delta_us": (
                full["runtime"]["worker_local_simulation_elapsed_total_us"]
                - off["runtime"]["worker_local_simulation_elapsed_total_us"]
            ),
            "full_outer_observation_total_us": full["runtime"]["outer_observation_total_us"],
            "off_observation_total_us": off["runtime"]["outer_observation_total_us"],
            "off_is_diagnostic_only": True,
            "off_training_throughput_eligible": False,
        },
        "gates": {
            "full_primary_results": True,
            "off_primary_results": True,
            "lifecycle_clean": True,
            "sidecars_complete": True,
            "canonical_identity": True,
            "canonical_workload": True,
            "gameplay_equivalence": True,
            "query_count_explanation": True,
            "privacy_partition": True,
            "off_diagnostic_label": True,
            "optimization_implemented": False,
            "begin_m5": False,
            "schema_validation": True,
        },
        "recommendation": {
            "first_m4_3_experiment": first_candidate["candidate"],
            "basis": (
                "Highest measured observation-path fraction: "
                f"{first_candidate['measured_runtime_fraction_percent']:.6f}% in "
                f"{first_candidate['bucket']}. This is an experiment target only; "
                "no speedup is estimated."
            ),
            "required_equivalence_test": first_candidate["required_equivalence_test"],
            "expected_affected_bucket": first_candidate["bucket"],
        },
    }
    validate_performance_audit_artifact(report)
    return report


def _schema_path(path: str | Path | None = None) -> Path:
    if path is not None:
        return Path(path)
    return (
        PERFORMANCE_AUDIT_REPOSITORY_ROOT
        / "docs"
        / "m4"
        / "m4_performance_audit_schema.json"
    )


def validate_performance_audit_artifact(
    report: Mapping[str, Any],
    *,
    schema_path: str | Path | None = None,
) -> None:
    """Validate the complete artifact with the Draft 2020-12 schema."""

    if not isinstance(report, Mapping):
        raise PerformanceAuditReportError(
            "performance audit artifact must be an object"
        )
    try:
        json.dumps(report, ensure_ascii=False, allow_nan=False)
    except (TypeError, ValueError) as error:
        raise PerformanceAuditReportError(
            f"audit artifact is not strict JSON: {error}"
        ) from error
    try:
        schema = json.loads(_schema_path(schema_path).read_text(encoding="utf-8"))
        from jsonschema import Draft202012Validator
    except (OSError, UnicodeError, json.JSONDecodeError, ImportError) as error:
        raise PerformanceAuditReportError(
            f"audit schema cannot be loaded: {error}"
        ) from error
    errors = sorted(
        Draft202012Validator(schema).iter_errors(report),
        key=lambda error: list(error.path),
    )
    if errors:
        first = errors[0]
        location = ".".join(str(part) for part in first.path)
        raise PerformanceAuditReportError(
            f"audit schema rejected artifact at {location or '<root>'}: {first.message}"
        )


def render_performance_audit_markdown(report: Mapping[str, Any]) -> str:
    """Render a validated audit artifact without running or mutating a sample."""

    validate_performance_audit_artifact(report)
    full = report["samples"]["full"]
    off = report["samples"]["off_diagnostic"]
    lines = [
        "# OCGForge M4.2 — Observation-Path Performance Audit",
        "",
        f"Status: **{report['status']}**",
        "",
        "No optimization was implemented and M5 was not started.",
        "",
        "## Workload",
        "",
        f"- Matchup: {report['workload']['matchup']}",
        f"- Master seed: {report['workload']['master_seed']}; games: {report['workload']['games']}; max steps: {report['workload']['max_steps']}",
        "- Workers: 1; mode: throughput; instrumentation: false; trace persistence: false",
        "",
        "## Observation timing — FULL sample",
        "",
        "| bucket | total us | calls | mean us/call | fraction of outer observation |",
        "|---|---:|---:|---:|---:|",
    ]
    outer = full["timing_us"]["outer_observation"]["total_us"]
    for key, timing in full["timing_us"]["observation"].items():
        fraction = timing["total_us"] * 100.0 / outer if outer else 0.0
        lines.append(
            f"| {key} | {timing['total_us']} | {timing['calls']} | "
            f"{timing['mean_us_per_call']} | {fraction:.6f}% |"
        )
    lines.extend(
        [
            "",
            f"Outer observation: {outer} us across "
            f"{full['timing_us']['outer_observation']['calls']} observations.",
            "",
            "## Query call-site classification",
            "",
            "| call site | query | calls/observation | measured calls | same-state duplicate? | safe reuse candidate |",
            "|---|---|---:|---:|---|---|",
        ]
    )
    for item in report["query_call_site_classification"]:
        lines.append(
            f"| {item['call_site']} | {item['query_type']} | "
            f"{item['calls_per_observation']} | {item['measured_total_calls']} | "
            f"{item['same_state_duplicate']} | {item['safe_candidate_for_reuse']} |"
        )
    lines.extend(
        [
            "",
            "## Entity/privacy audit",
            "",
            "| zone | projected | identity known | redacted |",
            "|---|---:|---:|---:|",
        ]
    )
    for zone, counters in full["counters"]["entities_by_zone"].items():
        lines.append(
            f"| {zone} | {counters['entities_projected']} | "
            f"{counters['identity_known']} | {counters['redacted']} |"
        )
    entity = report["entity_projection_audit"]
    lines.extend(
        [
            "",
            f"Static printed metadata lookups: {entity['static_printed_metadata_lookups']}; "
            f"current-property projections: {entity['current_property_projections']}; "
            f"relationship objects: {entity['relationship_objects']}; "
            f"explicit allocation/copy events: {entity['allocation_copy_events']}.",
            entity["evidence"],
            "",
            "## Setup, coordinator, and off cross-check",
            "",
            f"Script loads: {full['runtime']['script_loads']}; "
            f"script-reader requests: {full['runtime']['script_reader_requests']}.",
            f"Coordinator worker-compute wait: "
            f"{full['timing_us']['coordinator']['worker_compute_wait']['total_us']} us; "
            f"CPU domains total: {full['timing_us']['coordinator_cpu_domain_total_us']} us; "
            "wait is not counted as CPU.",
            f"Off sample: {off['classification_label']}; worker-local runtime FULL "
            f"{report['cross_check']['full_worker_local_runtime_us']} us, off "
            f"{report['cross_check']['off_worker_local_runtime_us']} us.",
            "",
            "## Candidate audit",
            "",
            f"Candidate maximum: {report['candidate_audit']['candidate_max']} "
            f"(baseline evidence: {report['candidate_audit']['baseline_evidence']}). "
            f"Measured protocol_candidate: "
            f"{report['candidate_audit']['measured_protocol_candidate_fraction_percent']:.6f}%; "
            "no candidate optimization is proposed.",
            "",
            "| rank | candidate | measured fraction | semantic risk | complexity | affected bucket |",
            "|---:|---|---:|---|---|---|",
        ]
    )
    for candidate in report["optimization_candidates"]:
        lines.append(
            f"| {candidate['rank']} | {candidate['candidate']} | "
            f"{candidate['measured_runtime_fraction_percent']:.6f}% | "
            f"{candidate['semantic_risk']} | {candidate['complexity']} | "
            f"{candidate['bucket']} |"
        )
    lines.extend(
        [
            "",
            "## First M4.3 experiment",
            "",
            f"{report['recommendation']['first_m4_3_experiment']} — "
            f"{report['recommendation']['basis']}",
            f"Required equivalence test: "
            f"{report['recommendation']['required_equivalence_test']}",
        ]
    )
    return "\n".join(lines) + "\n"


def write_performance_audit_outputs(
    report: Mapping[str, Any],
    *,
    markdown_path: str | Path,
    json_path: str | Path,
) -> None:
    """Write both outputs only after strict artifact validation."""

    validate_performance_audit_artifact(report)
    markdown = render_performance_audit_markdown(report)
    markdown_destination = Path(markdown_path)
    json_destination = Path(json_path)
    markdown_destination.parent.mkdir(parents=True, exist_ok=True)
    json_destination.parent.mkdir(parents=True, exist_ok=True)
    json_text = (
        json.dumps(
            report,
            indent=2,
            sort_keys=True,
            ensure_ascii=False,
            allow_nan=False,
        )
        + "\n"
    )
    markdown_destination.write_text(markdown, encoding="utf-8")
    json_destination.write_text(json_text, encoding="utf-8")


def run_and_write_performance_audit(
    worker_executable: str | Path | Sequence[str],
    *,
    master_seed: int,
    games: int,
    max_steps: int = 2200,
    markdown_path: str | Path = (
        PERFORMANCE_AUDIT_REPOSITORY_ROOT
        / "docs"
        / "m4"
        / "M4_PERFORMANCE_AUDIT.md"
    ),
    json_path: str | Path = (
        PERFORMANCE_AUDIT_REPOSITORY_ROOT
        / "docs"
        / "m4"
        / "m4_performance_audit.json"
    ),
    run_output_dir: str | Path | None = None,
    result_timeout_seconds: float = 120.0,
) -> dict[str, Any]:
    """Run FULL and off_diagnostic samples, then write outputs after both pass."""

    if run_output_dir is None:
        run_root = Path(tempfile.mkdtemp(prefix="ocgforge-m4-2-audit-"))
    else:
        run_root = Path(run_output_dir)
    full = run_audit_sample(
        worker_executable,
        master_seed=master_seed,
        games=games,
        max_steps=max_steps,
        observation_mode="full",
        output_dir=run_root / "full",
        result_timeout_seconds=result_timeout_seconds,
        require_primary_integrity=True,
    )
    off = run_audit_sample(
        worker_executable,
        master_seed=master_seed,
        games=games,
        max_steps=max_steps,
        observation_mode="off_diagnostic",
        output_dir=run_root / "off_diagnostic",
        result_timeout_seconds=result_timeout_seconds,
        require_primary_integrity=True,
    )
    report = build_performance_audit_report(
        full,
        off,
        master_seed=master_seed,
        games=games,
        max_steps=max_steps,
        worker_executable=worker_executable,
    )
    write_performance_audit_outputs(
        report,
        markdown_path=markdown_path,
        json_path=json_path,
    )
    return report


def _performance_audit_cli(arguments: Sequence[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Run the isolated OCGForge M4.2 performance audit"
    )
    parser.add_argument("--worker-executable", required=True)
    parser.add_argument("--master-seed", type=int, required=True)
    parser.add_argument("--games", type=int, default=16)
    parser.add_argument("--max-steps", type=int, default=2200)
    parser.add_argument(
        "--markdown-path",
        default=str(
            PERFORMANCE_AUDIT_REPOSITORY_ROOT
            / "docs"
            / "m4"
            / "M4_PERFORMANCE_AUDIT.md"
        ),
    )
    parser.add_argument(
        "--json-path",
        default=str(
            PERFORMANCE_AUDIT_REPOSITORY_ROOT
            / "docs"
            / "m4"
            / "m4_performance_audit.json"
        ),
    )
    parser.add_argument("--run-output-dir")
    parser.add_argument("--result-timeout-seconds", type=float, default=120.0)
    args = parser.parse_args(arguments)
    try:
        report = run_and_write_performance_audit(
            args.worker_executable,
            master_seed=args.master_seed,
            games=args.games,
            max_steps=args.max_steps,
            markdown_path=args.markdown_path,
            json_path=args.json_path,
            run_output_dir=args.run_output_dir,
            result_timeout_seconds=args.result_timeout_seconds,
        )
    except (PerformanceAuditRunnerError, PerformanceAuditReportError) as error:
        parser.error(str(error))
        return 2
    print(
        json.dumps(
            {"status": report["status"], "json_path": args.json_path},
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":  # pragma: no cover - exercised by the explicit CLI
    raise SystemExit(_performance_audit_cli())


__all__ = [
    "AUDIT_SIDECAR_PREFIX",
    "AuditWorkerPool",
    "CoordinatorTimingError",
    "CoordinatorTimingSnapshot",
    "OFF_DIAGNOSTIC_LABEL",
    "PERFORMANCE_AUDIT_MATCHUP",
    "PERFORMANCE_AUDIT_SCHEMA_VERSION",
    "PERFORMANCE_AUDIT_STATUS",
    "PerformanceAuditReportError",
    "PerformanceAuditRunnerError",
    "PerformanceAuditSidecarError",
    "aggregate_coordinator_timing",
    "build_audit_jobs",
    "build_performance_audit_report",
    "parse_audit_sidecar_line",
    "parse_audit_sidecars",
    "run_audit_sample",
    "run_and_write_performance_audit",
    "render_performance_audit_markdown",
    "validate_performance_audit_artifact",
    "write_performance_audit_outputs",
]
