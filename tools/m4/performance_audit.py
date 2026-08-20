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
        ENTITY_ZONE_KEYS,
        OBSERVATION_COUNTER_KEYS,
        OBSERVATION_DETAIL_COUNTER_KEYS,
        OBSERVATION_TIMING_KEYS,
        PerformanceAuditContractError,
        SETUP_TIMING_KEYS,
        default_coordinator_timing_us,
        validate_audit_telemetry,
    )
except ImportError:  # pragma: no cover - exercised by direct CLI imports
    from tools.m4 import benchmark as _benchmark
    from tools.m4.benchmark import PersistentWorkerPool
    from tools.m4.job_generation import derive_job_with_options
    from tools.m4.performance_audit_contract import (
        AUXILIARY_TIMING_KEYS,
        ENTITY_ZONE_KEYS,
        OBSERVATION_COUNTER_KEYS,
        OBSERVATION_DETAIL_COUNTER_KEYS,
        OBSERVATION_TIMING_KEYS,
        PerformanceAuditContractError,
        SETUP_TIMING_KEYS,
        default_coordinator_timing_us,
        validate_audit_telemetry,
    )


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

    normalized = {
        "observation_detail_counters": _validate_counter_object(
            sidecar["observation_detail_counters"],
            "observation_detail_counters",
            required_keys=OBSERVATION_DETAIL_COUNTER_KEYS,
        ),
        "setup_timing_us": _validate_timing_object(
            sidecar["setup_timing_us"],
            "setup_timing_us",
            required_keys=SETUP_TIMING_KEYS,
        ),
        "auxiliary_timing_us": _validate_timing_object(
            sidecar["auxiliary_timing_us"],
            "auxiliary_timing_us",
            required_keys=AUXILIARY_TIMING_KEYS,
        ),
    }

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


__all__ = [
    "AUDIT_SIDECAR_PREFIX",
    "AuditWorkerPool",
    "CoordinatorTimingError",
    "CoordinatorTimingSnapshot",
    "OFF_DIAGNOSTIC_LABEL",
    "PerformanceAuditRunnerError",
    "PerformanceAuditSidecarError",
    "aggregate_coordinator_timing",
    "build_audit_jobs",
    "parse_audit_sidecar_line",
    "parse_audit_sidecars",
    "run_audit_sample",
]
