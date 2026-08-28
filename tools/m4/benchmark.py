"""Reusable persistent native-worker coordinator and M4 benchmark CLI."""

from __future__ import annotations

from dataclasses import dataclass
import ctypes
import math
import os
from pathlib import Path
import queue
import select
import subprocess
import sys
import threading
import time
from typing import Any, Mapping, Sequence

if __package__ in (None, ""):
    _REPO_ROOT = Path(__file__).resolve().parents[2]
    if str(_REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(_REPO_ROOT))

try:
    from .job_generation import derive_job_with_options
    from .process_metrics import ProcessMetricsSampler, stderr_size
    from .report import (
        BenchmarkIntegrityError,
        aggregate_results,
        build_argument_parser,
        build_report,
        default_hardware_metadata,
        validate_complete_results,
        write_report,
    )
    from .worker_protocol import (
        HandshakeExpectation,
        ProtocolValidationError,
        assert_primary_integrity,
        decode_line,
        encode_job,
        validate_ready,
        validate_result,
    )
    from .worker_protocol_contract import ProtocolContractError, normalize_job_id
except ImportError:  # pragma: no cover - exercised by direct CLI execution
    from tools.m4.job_generation import derive_job_with_options
    from tools.m4.process_metrics import ProcessMetricsSampler, stderr_size
    from tools.m4.report import (
        BenchmarkIntegrityError,
        aggregate_results,
        build_argument_parser,
        build_report,
        default_hardware_metadata,
        validate_complete_results,
        write_report,
    )
    from tools.m4.worker_protocol import (
        HandshakeExpectation,
        ProtocolValidationError,
        assert_primary_integrity,
        decode_line,
        encode_job,
        validate_ready,
        validate_result,
    )
    from tools.m4.worker_protocol_contract import ProtocolContractError, normalize_job_id


class CoordinatorError(RuntimeError):
    """Base error for coordinator lifecycle and dispatch failures."""


class WorkerStartupError(CoordinatorError):
    """A worker did not provide an acceptable ready handshake."""


class WorkerRuntimeError(CoordinatorError):
    """A worker failed while a job was in flight and could not be replaced."""


@dataclass
class _InFlight:
    job: dict[str, Any]
    dispatched_ns: int


@dataclass
class _WorkerState:
    index: int
    process: subprocess.Popen[str] | None = None
    events: queue.Queue[tuple[str, str | None]] | None = None
    reader: threading.Thread | None = None
    stderr_path: Path | None = None
    pid: int = 0
    restart_index: int = 0
    ready: dict[str, Any] | None = None
    alive: bool = False
    restarted_for_job: bool = False
    in_flight: _InFlight | None = None
    staged_result: dict[str, Any] | None = None
    # Retained only until the final run barrier settles the last result.
    published_job: _InFlight | None = None
    retired: bool = False
    reaped: bool = False
    cleanup_error: str | None = None


def _zero_coordinator_errors() -> dict[str, int]:
    return {
        "retries": 0,
        "handshake": 0,
        "malformed_protocol": 0,
        "failed_games": 0,
        "worker_crashes": 0,
        "worker_restarts": 0,
    }


def _zero_errors() -> dict[str, int]:
    return {
        "retries": 0,
        "unsupported": 0,
        "automatic": 0,
        "truncated": 0,
        "core_errors": 0,
        "worker_errors": 0,
    }


def _zero_timing() -> dict[str, int]:
    return {
        "core_process": 0,
        "protocol_candidate": 0,
        "continuation": 0,
        "observation": 0,
        "trace_hash": 0,
        "serialization": 0,
        "other": 0,
        "trace_persistence": 0,
    }


def _zero_counters() -> dict[str, int]:
    return {
        "ocg_duel_process": 0,
        "ocg_duel_query": 0,
        "ocg_duel_query_location": 0,
        "ocg_duel_query_field": 0,
        "ocg_duel_query_count": 0,
        "script_reader_requests": 0,
        "script_loads": 0,
        "observations": 0,
        "entities_projected": 0,
        "candidate_sets": 0,
        "candidate_total": 0,
        "candidate_max": 0,
        "semantic_hashes": 0,
        "trace_bytes_serialized": 0,
    }


def _job_sort_key(job_id: str) -> tuple[int, str]:
    try:
        prefix, suffix = job_id.rsplit("-", 1)
        if prefix == "m4" and suffix.isdigit():
            return int(suffix), job_id
    except (AttributeError, ValueError):
        pass
    return (2**63 - 1, job_id)


class PersistentWorkerPool:
    """One coordinator over persistent, single-threaded native workers.

    A worker result is staged until a bounded lifecycle-settle interval has
    elapsed or the worker's process state settles.  The interval is a
    coordinator lifecycle barrier, not part of result-receipt latency or the
    worker-reported ``simulation_elapsed_us``.  This prevents a result line
    racing an EOF/exit from becoming primary evidence while keeping healthy
    persistent workers bounded rather than waiting forever.
    """

    def __init__(
        self,
        worker_executable: str | os.PathLike[str] | Sequence[str],
        *,
        worker_count: int = 1,
        output_dir: str | os.PathLike[str] | None = None,
        expected: HandshakeExpectation | Mapping[str, Any] | None = None,
        startup_timeout_seconds: float = 30.0,
        result_timeout_seconds: float = 120.0,
        lifecycle_settle_timeout_seconds: float = 0.05,
        restart_workers: bool = True,
    ) -> None:
        if isinstance(worker_count, bool) or not isinstance(worker_count, int) or worker_count <= 0:
            raise ValueError("worker_count must be a positive integer")
        self.command = self._normalize_command(worker_executable)
        self.worker_count = worker_count
        self.output_dir = Path(output_dir) if output_dir is not None else Path(
            tempfile_directory()
        )
        self.expected = HandshakeExpectation.canonical() if expected is None else expected
        self.startup_timeout_seconds = startup_timeout_seconds
        self.result_timeout_seconds = result_timeout_seconds
        try:
            settle_timeout = float(lifecycle_settle_timeout_seconds)
        except (TypeError, ValueError, OverflowError) as error:
            raise ValueError("lifecycle_settle_timeout_seconds must be positive") from error
        if (
            isinstance(lifecycle_settle_timeout_seconds, bool)
            or not isinstance(lifecycle_settle_timeout_seconds, (int, float))
            or not math.isfinite(settle_timeout)
            or settle_timeout <= 0
        ):
            raise ValueError("lifecycle_settle_timeout_seconds must be positive")
        self.lifecycle_settle_timeout_seconds = settle_timeout
        self.restart_workers = restart_workers
        self._states: list[_WorkerState] = []
        self._started = False
        self._closed = False
        self._unusable = False
        self._unusable_reason: str | None = None
        self._run_integrity_failure: str | None = None
        self._last_publication_ns: int | None = None
        self._metrics: ProcessMetricsSampler | None = None
        self._results: dict[str, dict[str, Any]] = {}
        self.last_run_metadata: dict[str, Any] = self._new_metadata()

    @staticmethod
    def _normalize_command(
        executable: str | os.PathLike[str] | Sequence[str],
    ) -> list[str]:
        if isinstance(executable, (str, os.PathLike)):
            path = Path(executable)
            if path.suffix.lower() == ".py":
                return [sys.executable, str(path)]
            return [str(path)]
        command = [str(value) for value in executable]
        if not command:
            raise ValueError("worker command must not be empty")
        return command

    @staticmethod
    def _positive_finite_timeout(value: Any, name: str) -> float:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"{name} must be a positive finite number")
        try:
            timeout = float(value)
        except (TypeError, ValueError, OverflowError) as error:
            raise ValueError(f"{name} must be a positive finite number") from error
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError(f"{name} must be a positive finite number")
        return timeout

    def _validate_timeouts(self) -> None:
        self.startup_timeout_seconds = self._positive_finite_timeout(
            self.startup_timeout_seconds, "startup_timeout_seconds"
        )
        self.result_timeout_seconds = self._positive_finite_timeout(
            self.result_timeout_seconds, "result_timeout_seconds"
        )
        self.lifecycle_settle_timeout_seconds = self._positive_finite_timeout(
            self.lifecycle_settle_timeout_seconds, "lifecycle_settle_timeout_seconds"
        )

    def _new_metadata(self) -> dict[str, Any]:
        return {
            "worker_count": self.worker_count,
            "worker_crashes": 0,
            "worker_restarts": 0,
            "retries": 0,
            "handshake_errors": 0,
            "malformed_protocol": 0,
            "failed_games": 0,
            "worker_errors": 0,
            "integrity_failure": None,
            "workers": [],
            "memory": {
                "process_count": "NOT_MEASURED",
                "peak_total_working_set_bytes": "NOT_MEASURED",
                "peak_worker_working_set_bytes": "NOT_MEASURED",
                "memory_per_active_environment_bytes": "NOT_MEASURED",
            },
        }

    def __enter__(self) -> "PersistentWorkerPool":
        self.start()
        return self

    def __exit__(self, exc_type: object, exc_value: object, traceback: object) -> None:
        self.close()

    @property
    def workers(self) -> tuple[dict[str, Any], ...]:
        return tuple(self._worker_metadata(state) for state in self._states)

    @property
    def ready_messages(self) -> tuple[dict[str, Any], ...]:
        """Return the validated startup identities observed for each worker."""

        return tuple(dict(state.ready) for state in self._states if state.ready is not None)

    @property
    def last_publication_ns(self) -> int | None:
        """Coordinator clock at which the final result row was published."""

        return self._last_publication_ns

    def start(self) -> None:
        if self._closed:
            raise CoordinatorError("worker pool is already closed")
        self._validate_timeouts()
        if self._started:
            return
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.last_run_metadata = self._new_metadata()
        self._states = [_WorkerState(index=index) for index in range(self.worker_count)]
        try:
            for state in self._states:
                self._launch(state)
                self._read_ready(state)
                # A process that exits immediately after a ready line is an
                # abnormal startup, not a worker that accepted jobs.
                self._verify_ready_worker_alive(state)
            self._started = True
            self._refresh_worker_metadata()
        except Exception:
            self._shutdown_all()
            raise

    def close(self) -> None:
        if self._metrics is not None:
            self.last_run_metadata["memory"] = self._metrics.stop().__dict__
            self._metrics = None
        self._shutdown_all()
        self._refresh_worker_metadata()
        self._started = False
        self._closed = True

    def run(
        self,
        jobs: Sequence[Mapping[str, Any]],
        *,
        require_primary_integrity: bool = False,
    ) -> list[dict[str, Any]]:
        """Run each value job once and publish results in canonical order."""

        if self._unusable:
            raise WorkerRuntimeError(
                "worker pool is not reusable after fail-closed shutdown: "
                f"{self._unusable_reason or 'unknown coordinator failure'}"
            )
        try:
            self._run_integrity_failure = None
            self._last_publication_ns = None
            self._validate_timeouts()
            if not self._started:
                self.start()
            # A pool may be reused after a recoverable worker replacement.
            # Metadata describes this run, not the lifetime of the pool.
            self.last_run_metadata = self._new_metadata()
            self._refresh_worker_metadata()
            dead_workers = [
                state.index
                for state in self._states
                if not state.alive or state.retired
            ]
            if dead_workers:
                raise WorkerRuntimeError(
                    "worker pool is not reusable after worker failure; "
                    f"dead worker(s): {dead_workers}"
                )
            copied_jobs = [dict(job) for job in jobs]
            job_ids: list[str] = []
            for job in copied_jobs:
                raw_job_id = job.get("job_id")
                if not isinstance(raw_job_id, str) or not raw_job_id:
                    raise ValueError("every job must have a nonempty string job_id")
                try:
                    canonical_job_id = normalize_job_id(raw_job_id)
                except ProtocolContractError:
                    canonical_job_id = raw_job_id
                else:
                    job["job_id"] = canonical_job_id
                job_ids.append(canonical_job_id)
            if any(not isinstance(job_id, str) or not job_id for job_id in job_ids):
                raise ValueError("every job must have a nonempty string job_id")
            if len(set(job_ids)) != len(job_ids):
                raise ValueError("job IDs must be unique")

            self._results = {}
            self.last_run_metadata.update(
                {
                    "games_requested": len(copied_jobs),
                    "games_completed": 0,
                    "result_order": [str(job_id) for job_id in job_ids],
                }
            )
            if not copied_jobs:
                return []

            dispatchable_jobs: list[tuple[dict[str, Any], str]] = []
            for job in copied_jobs:
                try:
                    encoded_job = encode_job(job)
                except (AttributeError, KeyError, TypeError, ValueError) as error:
                    job_id = str(job["job_id"])
                    self._results[job_id] = self._invalid_job_result(job, str(error))
                    continue
                dispatchable_jobs.append((job, encoded_job))

            if not dispatchable_jobs:
                ordered = [
                    self._results[key] for key in sorted(self._results, key=_job_sort_key)
                ]
                self.last_run_metadata["games_completed"] = len(ordered)
                self.last_run_metadata["failed_games"] = len(ordered)
                if require_primary_integrity:
                    for result in ordered:
                        assert_primary_integrity(result)
                return ordered

            self._metrics = ProcessMetricsSampler(self._live_pids)
            self._metrics.start()
            next_index = 0
            for state in self._states:
                if state.alive and next_index < len(dispatchable_jobs):
                    job, encoded_job = dispatchable_jobs[next_index]
                    self._dispatch(state, job, encoded_job)
                    next_index += 1

            while next_index < len(dispatchable_jobs) or any(
                state.in_flight is not None for state in self._states
            ):
                state, event = self._next_event()
                kind, payload = event
                if kind == "line":
                    if payload is None:
                        self._worker_failed(
                            state,
                            code="malformed_protocol",
                            message="worker reader returned an empty protocol line",
                            malformed=True,
                        )
                    else:
                        self._handle_line(state, payload)
                elif kind == "eof":
                    self._worker_failed(
                        state,
                        code="worker_crash",
                        message=self._exit_message(state),
                        crashed=True,
                    )
                else:
                    self._worker_failed(
                        state,
                        code="malformed_protocol",
                        message=payload or "worker stdout reader failure",
                        malformed=True,
                    )

                if state.alive and state.in_flight is None:
                    self._reject_pending_events(state)
                if state.alive and state.in_flight is None and next_index < len(dispatchable_jobs):
                    job, encoded_job = dispatchable_jobs[next_index]
                    self._dispatch(state, job, encoded_job)
                    next_index += 1
                elif not state.alive and next_index < len(dispatchable_jobs):
                    if not self.restart_workers:
                        raise WorkerRuntimeError(
                            f"worker {state.index} failed with unassigned jobs remaining"
                        )
                    self._restart(state)
                    job, encoded_job = dispatchable_jobs[next_index]
                    self._dispatch(state, job, encoded_job)
                    next_index += 1
            self._last_publication_ns = time.perf_counter_ns()
            # Keep a short post-publication lifecycle barrier so a worker
            # cannot emit a delayed duplicate after the final result has
            # already been accepted, even for diagnostic/non-primary runs.
            try:
                self._settle_completed_run()
            finally:
                for state in self._states:
                    state.published_job = None
            ordered = [
                self._results[key] for key in sorted(self._results, key=_job_sort_key)
            ]
            self.last_run_metadata["games_completed"] = len(ordered)
            self.last_run_metadata["failed_games"] = sum(
                result.get("status") == "failed" for result in ordered
            )
            if require_primary_integrity:
                if self._run_integrity_failure is not None:
                    raise ProtocolValidationError(
                        "primary run encountered coordinator integrity failure: "
                        f"{self._run_integrity_failure}"
                    )
                for result in ordered:
                    assert_primary_integrity(result)
            return ordered
        except Exception as error:
            self._fail_closed(
                f"coordinator run failed: {error}", code="coordinator_exception"
            )
            raise
        finally:
            if self._metrics is not None:
                self.last_run_metadata["memory"] = self._metrics.stop().__dict__
                self._metrics = None
            self._refresh_worker_metadata()

    def _launch(self, state: _WorkerState) -> None:
        suffix = "" if state.restart_index == 0 else f"-restart-{state.restart_index:03d}"
        stderr_path = self.output_dir / f"worker-{state.index:03d}{suffix}.stderr.log"
        stderr_file = stderr_path.open("w", encoding="utf-8", newline="")
        try:
            process = subprocess.Popen(
                self.command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=stderr_file,
                text=False,
                bufsize=0,
            )
        except Exception:
            stderr_file.close()
            raise
        finally:
            # Popen owns the duplicated child handle; the coordinator must not
            # retain a descriptor that could keep the file open indefinitely.
            stderr_file.close()
        state.process = process
        state.events = queue.Queue()
        state.stderr_path = stderr_path
        state.pid = int(process.pid or 0)
        state.alive = True
        state.ready = None
        state.in_flight = None
        state.staged_result = None
        state.published_job = None
        state.retired = False
        state.reaped = False
        state.cleanup_error = None
        state.reader = threading.Thread(
            target=self._read_stdout,
            args=(state,),
            name=f"ocgforge-m4-worker-reader-{state.index}",
            daemon=True,
        )
        state.reader.start()
        self._refresh_worker_metadata()

    @staticmethod
    def _read_stdout(state: _WorkerState) -> None:
        assert state.process is not None
        assert state.process.stdout is not None
        assert state.events is not None
        try:
            eof_observed = False
            while True:
                raw_line = state.process.stdout.readline()
                if raw_line == b"":
                    eof_observed = True
                    break
                try:
                    line = raw_line.decode("utf-8")
                except UnicodeDecodeError as error:
                    state.events.put(("reader_error", f"invalid UTF-8 on worker stdout: {error}"))
                    return
                pending_lines = [line.rstrip("\r\n")]
                while PersistentWorkerPool._stdout_pending_status(state) == "data":
                    raw_line = state.process.stdout.readline()
                    if raw_line == b"":
                        eof_observed = True
                        break
                    try:
                        pending_lines.append(raw_line.decode("utf-8").rstrip("\r\n"))
                    except UnicodeDecodeError as error:
                        state.events.put(("reader_error", f"invalid UTF-8 on worker stdout: {error}"))
                        return
                if eof_observed:
                    # An observed EOF invalidates every result in the batch;
                    # publish the failure signal before the result lines.
                    state.events.put(("eof", None))
                    for pending_line in pending_lines:
                        state.events.put(("line", pending_line))
                    return
                for pending_line in pending_lines:
                    state.events.put(("line", pending_line))
        except Exception as error:  # pragma: no cover - OS-specific reader failure
            state.events.put(("reader_error", str(error)))
        finally:
            if not eof_observed:
                state.events.put(("eof", None))

    def _read_ready(self, state: _WorkerState) -> None:
        try:
            kind, payload = self._wait_state_event(state, self.startup_timeout_seconds)
            if kind == "eof":
                self.last_run_metadata["worker_crashes"] += 1
                raise WorkerStartupError(self._exit_message(state))
            if kind != "line" or payload is None:
                raise WorkerStartupError(payload or "worker did not emit a ready line")
            message = decode_line(payload)
            if message.get("type") != "ready":
                raise ProtocolValidationError("first worker message was not ready")
            validate_ready(message, self.expected)
            actual_pid = int(state.process.pid) if state.process is not None else 0
            if message["pid"] != actual_pid:
                raise ProtocolValidationError(
                    "worker ready PID does not match the Popen.pid"
                )
        except WorkerStartupError:
            self.last_run_metadata["handshake_errors"] += 1
            raise
        except Exception as error:
            self.last_run_metadata["handshake_errors"] += 1
            raise WorkerStartupError(str(error)) from error
        state.ready = message

    def _verify_ready_worker_alive(self, state: _WorkerState) -> None:
        """Reject a process that closes immediately after its ready line."""

        deadline = time.perf_counter() + 0.05
        while time.perf_counter() < deadline:
            if state.process is not None and state.process.poll() is not None:
                self.last_run_metadata["worker_crashes"] += 1
                raise WorkerStartupError(
                    f"worker {state.index} exited after ready with code {state.process.returncode}"
                )
            if state.events is not None:
                try:
                    kind, payload = state.events.get_nowait()
                except queue.Empty:
                    pass
                else:
                    if kind == "eof":
                        self.last_run_metadata["worker_crashes"] += 1
                        raise WorkerStartupError(self._exit_message(state))
                    raise WorkerStartupError(
                        payload or "worker emitted unexpected data during startup"
                    )
            time.sleep(0.001)

    def _wait_state_event(
        self,
        state: _WorkerState,
        timeout_seconds: float,
    ) -> tuple[str, str | None]:
        if state.events is None:
            raise CoordinatorError(f"worker {state.index} has no event queue")
        try:
            return state.events.get(timeout=timeout_seconds)
        except queue.Empty as error:
            if state.process is not None and state.process.poll() is not None:
                self.last_run_metadata["worker_crashes"] += 1
                raise WorkerStartupError(self._exit_message(state)) from error
            raise WorkerStartupError(f"worker {state.index} ready timeout") from error

    def _next_event(self) -> tuple[_WorkerState, tuple[str, str | None]]:
        timeout_ns = max(0, int(self.result_timeout_seconds * 1_000_000_000))
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
                raise WorkerRuntimeError(message)
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
                raise CoordinatorError("no in-flight worker job while waiting for an event")
            next_deadline_ns = min(
                state.in_flight.dispatched_ns + timeout_ns for state in outstanding
            )
            remaining_seconds = max(
                0.0,
                (next_deadline_ns - time.perf_counter_ns()) / 1_000_000_000,
            )
            time.sleep(min(0.001, remaining_seconds))

    def _dispatch(self, state: _WorkerState, job: dict[str, Any], encoded_job: str) -> None:
        if not state.alive or state.process is None or state.process.stdin is None:
            raise WorkerRuntimeError(f"worker {state.index} is not available for dispatch")
        state.published_job = None
        state.in_flight = _InFlight(job=job, dispatched_ns=time.perf_counter_ns())
        try:
            state.process.stdin.write((encoded_job + "\n").encode("utf-8"))
            state.process.stdin.flush()
        except (BrokenPipeError, OSError, ValueError) as error:
            self._worker_failed(
                state,
                code="worker_crash",
                message=f"failed to send job: {error}",
                crashed=True,
            )

    def _handle_line(self, state: _WorkerState, line: str) -> None:
        receipt_ns = time.perf_counter_ns()
        failure = self._queued_failure_event(state)
        if failure is not None:
            self._handle_failure_event(state, failure)
            return
        if state.in_flight is None:
            self._worker_failed(
                state,
                code="malformed_protocol",
                message="worker emitted a result without an in-flight job",
                malformed=True,
            )
            return
        if state.staged_result is not None:
            self._worker_failed(
                state,
                code="malformed_protocol",
                message="worker emitted more than one result before finalization",
                malformed=True,
            )
            return
        job = state.in_flight.job
        try:
            message = decode_line(line)
            if message.get("type") != "result":
                raise ProtocolValidationError("worker emitted a non-result message for a job")
            validate_result(message, str(job["job_id"]))
            actual_pid = self._validate_worker_result_identity(state, message)
        except Exception as error:
            self._worker_failed(
                state,
                code="malformed_protocol",
                message=str(error),
                malformed=True,
            )
            return

        result = dict(message)
        result["coordinator"] = {
            "worker_index": state.index,
            "worker_pid": actual_pid,
            "worker_crashed": False,
            "worker_restarted": state.restarted_for_job,
            "worker_restart_index": state.restart_index,
            "stderr_path": str(state.stderr_path) if state.stderr_path else "NOT_MEASURED",
        }
        coordinator_errors = _zero_coordinator_errors()
        coordinator_errors["worker_restarts"] = state.restart_index
        if result["status"] == "failed":
            coordinator_errors["failed_games"] = 1
        result["coordinator_errors"] = coordinator_errors
        result["coordinator_elapsed_us"] = max(
            0,
            (receipt_ns - state.in_flight.dispatched_ns) // 1000,
        )
        state.staged_result = result
        self._finalize_staged_result(state)

    @staticmethod
    def _validate_worker_result_identity(
        state: _WorkerState, message: Mapping[str, Any]
    ) -> int:
        process = state.process
        actual_pid = int(process.pid) if process is not None and process.pid else 0
        if actual_pid <= 0 or state.pid != actual_pid:
            raise ProtocolValidationError(
                "coordinator worker slot PID is not bound to the Popen.pid"
            )
        if state.ready is None or state.ready.get("pid") != actual_pid:
            raise ProtocolValidationError(
                "worker ready PID is not bound to the Popen.pid"
            )
        worker = message["worker"]
        if worker["pid"] != actual_pid:
            raise ProtocolValidationError(
                "worker result PID does not match the Popen.pid"
            )
        if (
            worker["restart_index"] != 0
            or worker["crashed"] is not False
            or worker["restarted"] is not False
        ):
            raise ProtocolValidationError(
                "worker result lifecycle metadata is inconsistent"
            )
        return actual_pid

    def _finalize_staged_result(self, state: _WorkerState) -> None:
        if state.staged_result is None:
            return
        failure = self._settle_staged_result(state)
        if failure is not None:
            self._handle_failure_event(state, failure)
            return
        if state.in_flight is None:
            self._worker_failed(
                state,
                code="malformed_protocol",
                message="staged result lost its in-flight job",
                malformed=True,
            )
            return
        self._results[str(state.in_flight.job["job_id"])] = state.staged_result
        if state.staged_result["status"] == "failed":
            self.last_run_metadata["failed_games"] += 1
        state.published_job = state.in_flight
        state.staged_result = None
        state.in_flight = None

    def _settle_staged_result(
        self, state: _WorkerState
    ) -> tuple[str, str | None] | None:
        """Wait for a bounded lifecycle barrier before publishing a result.

        ``Popen.wait`` waits on the actual process handle rather than polling
        once.  A healthy persistent worker reaches the explicit deadline and
        remains reusable; EOF or a reader failure observed before that
        deadline wins over the staged result.  The bounded interval is a
        coordinator lifecycle barrier, not worker simulation timing or
        result-receipt latency.
        """

        process = state.process
        if process is None:
            return ("eof", None)
        deadline = time.perf_counter() + self.lifecycle_settle_timeout_seconds
        while True:
            failure = self._queued_failure_event(state)
            if failure is not None:
                return failure
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                return self._queued_failure_event(state)
            try:
                process.wait(timeout=remaining)
            except subprocess.TimeoutExpired:
                return self._queued_failure_event(state)
            except (OSError, ValueError) as error:
                return ("reader_error", f"worker lifecycle wait failed: {error}")
            else:
                return ("eof", None)

    def _settle_completed_run(self) -> None:
        """Reject output that arrives after the last result was published."""

        # This is deliberately longer than the per-result lifecycle barrier:
        # it covers a worker that flushes a duplicate after a scheduling
        # delay, while remaining bounded for a healthy persistent worker.
        deadline = time.perf_counter() + max(
            self.lifecycle_settle_timeout_seconds,
            0.25,
        )
        while time.perf_counter() < deadline:
            for state in self._states:
                if not state.alive:
                    continue
                if state.process is not None and state.process.poll() is not None:
                    self._worker_failed(
                        state,
                        code="worker_crash",
                        message=self._exit_message(state),
                        crashed=True,
                        include_published_job=True,
                    )
                    continue
                if state.events is None:
                    continue
                try:
                    kind, payload = state.events.get_nowait()
                except queue.Empty:
                    continue
                if kind == "line":
                    self._worker_failed(
                        state,
                        code="malformed_protocol",
                        message="worker emitted a result after the final job was published",
                        malformed=True,
                        include_published_job=True,
                    )
                else:
                    self._worker_failed(
                        state,
                        code="malformed_protocol" if kind == "reader_error" else "worker_crash",
                        message=payload or self._exit_message(state),
                        crashed=kind == "eof",
                        malformed=kind == "reader_error",
                        include_published_job=True,
                    )
            time.sleep(min(0.001, max(0.0, deadline - time.perf_counter())))

    @staticmethod
    def _stdout_pending_status(state: _WorkerState) -> str:
        process = state.process
        if process is None or process.stdout is None:
            return "eof"
        if os.name == "nt":
            try:
                import msvcrt
                from ctypes import wintypes

                handle = msvcrt.get_osfhandle(process.stdout.fileno())
                available = wintypes.DWORD()
                peek_named_pipe = ctypes.windll.kernel32.PeekNamedPipe
                peek_named_pipe.argtypes = [
                    wintypes.HANDLE,
                    ctypes.c_void_p,
                    wintypes.DWORD,
                    ctypes.POINTER(wintypes.DWORD),
                    ctypes.POINTER(wintypes.DWORD),
                    ctypes.POINTER(wintypes.DWORD),
                ]
                peek_named_pipe.restype = wintypes.BOOL
                if not peek_named_pipe(
                    wintypes.HANDLE(handle),
                    None,
                    0,
                    None,
                    ctypes.byref(available),
                    None,
                ):
                    return "eof"
                return "data" if available.value else "none"
            except Exception:
                return "unknown"
        try:
            readable, _writable, _exceptional = select.select([process.stdout], [], [], 0)
        except (OSError, ValueError):
            return "unknown"
        return "data" if readable else "none"

    def _queued_failure_event(
        self, state: _WorkerState
    ) -> tuple[str, str | None] | None:
        if state.process is not None and state.process.poll() is not None:
            return ("eof", None)
        if state.events is None:
            return None
        buffered: list[tuple[str, str | None]] = []
        while True:
            try:
                buffered.append(state.events.get_nowait())
            except queue.Empty:
                break
        failure = next(
            (event for event in buffered if event[0] in {"eof", "reader_error"}),
            None,
        )
        if failure is None:
            for event in buffered:
                state.events.put(event)
        return failure

    def _handle_failure_event(
        self, state: _WorkerState, event: tuple[str, str | None]
    ) -> None:
        kind, payload = event
        if kind == "eof":
            self._worker_failed(
                state,
                code="worker_crash",
                message=self._exit_message(state),
                crashed=True,
            )
            return
        self._worker_failed(
            state,
            code="malformed_protocol",
            message=payload or "worker stdout reader failure",
            malformed=True,
        )

    def _reject_pending_events(self, state: _WorkerState) -> None:
        if state.events is None:
            raise CoordinatorError(f"worker {state.index} has no event queue")
        failure = self._queued_failure_event(state)
        if failure is not None:
            self._handle_failure_event(state, failure)
            return
        try:
            kind, _payload = state.events.get_nowait()
        except queue.Empty:
            return
        self._worker_failed(
            state,
            code="malformed_protocol",
            message=f"worker emitted an unexpected {kind} event without an in-flight job",
            malformed=True,
        )

    def _worker_failed(
        self,
        state: _WorkerState,
        *,
        code: str,
        message: str,
        crashed: bool = False,
        malformed: bool = False,
        include_published_job: bool = False,
    ) -> None:
        in_flight = state.in_flight or (
            state.published_job if include_published_job else None
        )
        self._run_integrity_failure = message or code
        self.last_run_metadata["integrity_failure"] = self._run_integrity_failure
        state.staged_result = None
        if in_flight is not None:
            result = self._failed_result(
                state,
                in_flight,
                code=code,
                message=message,
                crashed=crashed,
                malformed=malformed,
            )
            self._results[str(in_flight.job["job_id"])] = result
            state.in_flight = None
            state.published_job = None
            self.last_run_metadata["failed_games"] += 1
        if crashed:
            self.last_run_metadata["worker_crashes"] += 1
        if malformed:
            self.last_run_metadata["malformed_protocol"] += 1
        self.last_run_metadata["worker_errors"] += 1
        reaped = self._shutdown_state(state)
        if not reaped:
            self._mark_unusable(
                f"worker {state.index} could not be reaped after {code}"
            )
            raise WorkerRuntimeError(self._unusable_reason or "worker cleanup failed")

    def _fail_closed(self, message: str, *, code: str) -> None:
        self.last_run_metadata["integrity_failure"] = message or code
        self._mark_unusable(message)
        for state in self._states:
            in_flight = state.in_flight
            if in_flight is not None:
                self._results[str(in_flight.job["job_id"])] = self._failed_result(
                    state,
                    in_flight,
                    code=code,
                    message=message,
                    crashed=False,
                    malformed=False,
                )
                state.in_flight = None
                self.last_run_metadata["failed_games"] += 1
            state.staged_result = None
            reaped = self._shutdown_state(state)
            if not reaped:
                self._mark_unusable(
                    f"{message}; worker {state.index} could not be reaped"
                )
        self._refresh_worker_metadata()

    def _mark_unusable(self, reason: str) -> None:
        self._unusable = True
        if self._unusable_reason is None:
            self._unusable_reason = reason

    @staticmethod
    def _invalid_job_result(job: Mapping[str, Any], message: str) -> dict[str, Any]:
        return {
            "schema": "ocgforge.m4.worker.v1",
            "type": "result",
            "status": "failed",
            "job_id": str(job["job_id"]),
            "terminal": False,
            "winner": None,
            "win_reason": None,
            "engine_steps": 0,
            "interactive_decisions": 0,
            "semantic_action_count": 0,
            "gameplay_hash": None,
            "trace_hash": None,
            "simulation_elapsed_us": None,
            "coordinator_elapsed_us": None,
            "errors": {**_zero_errors(), "worker_errors": 1},
            "timing_us": _zero_timing(),
            "counters": _zero_counters(),
            "worker": {
                "pid": 0,
                "restart_index": 0,
                "crashed": False,
                "restarted": False,
            },
            "failure_code": "invalid_job",
            "error_message": message or "job validation failed",
            "coordinator": {
                "worker_index": None,
                "worker_pid": None,
                "worker_crashed": False,
                "worker_restarted": False,
                "worker_restart_index": 0,
                "stderr_path": "NOT_MEASURED",
                "job_validation_error": True,
            },
            "coordinator_errors": {
                **_zero_coordinator_errors(),
                "failed_games": 1,
            },
        }

    def _failed_result(
        self,
        state: _WorkerState,
        in_flight: _InFlight,
        *,
        code: str,
        message: str,
        crashed: bool,
        malformed: bool,
    ) -> dict[str, Any]:
        elapsed_us = max(0, (time.perf_counter_ns() - in_flight.dispatched_ns) // 1000)
        errors = _zero_errors()
        errors["worker_errors"] = 1
        coordinator_errors = _zero_coordinator_errors()
        coordinator_errors["failed_games"] = 1
        coordinator_errors["worker_crashes"] = 1 if crashed else 0
        coordinator_errors["worker_restarts"] = state.restart_index
        if malformed:
            coordinator_errors["malformed_protocol"] = 1
        return {
            "schema": "ocgforge.m4.worker.v1",
            "type": "result",
            "status": "failed",
            "job_id": str(in_flight.job["job_id"]),
            "terminal": False,
            "winner": None,
            "win_reason": None,
            "engine_steps": 0,
            "interactive_decisions": 0,
            "semantic_action_count": 0,
            "gameplay_hash": None,
            "trace_hash": None,
            # No worker-local simulation interval exists when the worker
            # crashed or violated the protocol before returning a result.
            "simulation_elapsed_us": None,
            "coordinator_elapsed_us": elapsed_us,
            "errors": errors,
            "timing_us": _zero_timing(),
            "counters": _zero_counters(),
            "worker": {
                "pid": state.pid or (state.process.pid if state.process else 1),
                "restart_index": state.restart_index,
                "crashed": crashed,
                "restarted": state.restarted_for_job,
            },
            "failure_code": code,
            "error_message": message or code,
            "coordinator": {
                "worker_index": state.index,
                "worker_pid": state.pid,
                "worker_crashed": crashed,
                "worker_restarted": state.restarted_for_job,
                "worker_restart_index": state.restart_index,
                "stderr_path": str(state.stderr_path) if state.stderr_path else "NOT_MEASURED",
                "exit_code": state.process.returncode if state.process else None,
            },
            "coordinator_errors": coordinator_errors,
        }

    def _restart(self, state: _WorkerState) -> None:
        if not self.restart_workers:
            raise WorkerRuntimeError(f"worker {state.index} restart disabled")
        state.restart_index += 1
        state.restarted_for_job = True
        self.last_run_metadata["worker_restarts"] += 1
        if not state.reaped or state.cleanup_error:
            raise WorkerRuntimeError(
                f"worker {state.index} cannot be restarted safely: "
                f"{state.cleanup_error or 'termination was not confirmed'}"
            )
        try:
            self._launch(state)
        except Exception as error:
            message = f"replacement worker {state.index} failed launch: {error}"
            self._run_integrity_failure = message
            self.last_run_metadata["integrity_failure"] = message
            self._fail_closed(message, code="replacement_launch_failure")
            raise WorkerRuntimeError(message) from error
        try:
            self._read_ready(state)
            self._verify_ready_worker_alive(state)
        except Exception as error:
            message = f"replacement worker {state.index} failed handshake: {error}"
            if state.ready is not None:
                # _read_ready accounts for malformed identity messages.  A
                # process that exits after a valid ready line fails the same
                # startup/liveness gate and must be visible as a handshake
                # failure too.
                self.last_run_metadata["handshake_errors"] += 1
            self._run_integrity_failure = message
            self.last_run_metadata["integrity_failure"] = message
            self._fail_closed(message, code="replacement_handshake_failure")
            raise WorkerRuntimeError(message) from error

    def _exit_message(self, state: _WorkerState) -> str:
        returncode = state.process.returncode if state.process is not None else None
        return f"worker {state.index} exited abnormally with code {returncode}"

    def _live_pids(self) -> list[int]:
        pids: list[int] = []
        for state in self._states:
            if not state.alive or state.process is None:
                continue
            pid = int(state.process.pid or 0)
            if pid > 0:
                pids.append(pid)
        return pids

    def _worker_metadata(self, state: _WorkerState) -> dict[str, Any]:
        return {
            "worker_index": state.index,
            "pid": state.pid,
            "restart_index": state.restart_index,
            "stderr_path": str(state.stderr_path) if state.stderr_path else "NOT_MEASURED",
            "stderr_bytes": stderr_size(state.stderr_path) if state.stderr_path else "NOT_MEASURED",
            "ready": state.ready is not None,
            "alive": state.alive,
            "retired": state.retired,
            "reaped": state.reaped,
            "cleanup_error": state.cleanup_error,
        }

    def _refresh_worker_metadata(self) -> None:
        self.last_run_metadata["workers"] = [self._worker_metadata(state) for state in self._states]

    def _shutdown_state(self, state: _WorkerState) -> bool:
        process = state.process
        state.retired = True
        state.in_flight = None
        state.staged_result = None
        state.published_job = None
        if process is None:
            state.alive = False
            state.reaped = True
            state.cleanup_error = None
            return True
        try:
            if process.stdin is not None:
                process.stdin.close()
        except (OSError, ValueError):
            pass
        try:
            if process.poll() is None:
                process.terminate()
        except (OSError, ValueError) as error:
            state.cleanup_error = f"terminate failed: {error}"
        try:
            if process.poll() is None:
                process.wait(timeout=2.0)
        except (OSError, subprocess.TimeoutExpired) as error:
            state.cleanup_error = f"wait after terminate failed: {error}"
            try:
                if process.poll() is None:
                    process.kill()
            except (OSError, ValueError) as kill_error:
                state.cleanup_error = f"kill failed: {kill_error}"
            try:
                if process.poll() is None:
                    process.wait(timeout=2.0)
            except (OSError, subprocess.TimeoutExpired) as kill_wait_error:
                state.cleanup_error = f"wait after kill failed: {kill_wait_error}"
        if process.poll() is None:
            state.alive = True
            state.reaped = False
            if state.cleanup_error is None:
                state.cleanup_error = "process termination was not confirmed"
            self._refresh_worker_metadata()
            return False
        state.alive = False
        state.reaped = True
        if state.reader is not None and state.reader is not threading.current_thread():
            state.reader.join(timeout=2.0)
            if state.reader.is_alive():
                state.cleanup_error = state.cleanup_error or "stdout reader did not terminate"
        try:
            if process.stdout is not None:
                process.stdout.close()
        except (OSError, ValueError) as error:
            state.cleanup_error = state.cleanup_error or f"stdout close failed: {error}"
        self._refresh_worker_metadata()
        return state.cleanup_error is None

    def _shutdown_all(self) -> None:
        for state in self._states:
            if not self._shutdown_state(state):
                self._mark_unusable(
                    f"worker {state.index} cleanup was not confirmed"
                )


def tempfile_directory() -> str:
    import tempfile

    return tempfile.mkdtemp(prefix="ocgforge-m4-workers-")


WorkerPool = PersistentWorkerPool
SimulationCoordinator = PersistentWorkerPool


def _run_benchmark_cli(arguments: Sequence[str] | None = None) -> int:
    parser = build_argument_parser()
    args = parser.parse_args(arguments)
    if args.starting_player_mode != "balanced" or args.seat_mode != "balanced":
        raise BenchmarkIntegrityError("only balanced partition modes are supported")

    total_jobs = args.warmup_games + args.games
    jobs = [
        derive_job_with_options(
            args.master_seed,
            index,
            mode=args.mode,
            observation_mode=args.observation_mode,
            instrumentation=args.instrument,
            persist_trace=args.trace_persistence,
        )
        for index in range(total_jobs)
    ]
    warmup_jobs = jobs[: args.warmup_games]
    steady_jobs = jobs[args.warmup_games :]
    output_path = Path(args.output)
    worker_output_dir = output_path.parent / f"{output_path.stem}.workers"
    pool = PersistentWorkerPool(
        args.worker_executable,
        worker_count=args.workers,
        output_dir=worker_output_dir,
        result_timeout_seconds=args.result_timeout_seconds,
    )
    cold_start_begin = time.perf_counter_ns()
    pool.start()
    cold_start_seconds = (time.perf_counter_ns() - cold_start_begin) / 1_000_000_000
    ready_messages = pool.ready_messages
    try:
        if warmup_jobs:
            warmup_results = pool.run(warmup_jobs, require_primary_integrity=True)
            validate_complete_results(
                warmup_results,
                [job["job_id"] for job in warmup_jobs],
                pool.last_run_metadata,
                require_trace_hash=args.mode == "conformance" and args.trace_persistence,
            )
        steady_begin = time.perf_counter_ns()
        steady_results = pool.run(steady_jobs, require_primary_integrity=True)
        steady_end = pool.last_publication_ns or time.perf_counter_ns()
        steady_seconds = (steady_end - steady_begin) / 1_000_000_000
    finally:
        pool.close()

    metadata = pool.last_run_metadata
    expected_ids = [job["job_id"] for job in steady_jobs]
    validate_complete_results(
        steady_results,
        expected_ids,
        metadata,
        require_trace_hash=args.mode == "conformance" and args.trace_persistence,
    )
    steady_state = aggregate_results(
        steady_results,
        metadata,
        wall_clock_seconds=steady_seconds,
        games_requested=args.games,
        workers_requested=args.workers,
    )
    ready = ready_messages[0] if ready_messages else {}
    from tools.m3.rules_mode import load_canonical_environment

    canonical_environment = load_canonical_environment(
        Path(__file__).resolve().parents[2] / "third_party" / "rules_bundle.lock.json"
    )
    canonical_environment.update(
        {
            "deck_hashes": list(HandshakeExpectation.canonical().deck_hashes),
            "worker_identity": ready.get("worker_identity", "NOT_MEASURED"),
        }
    )
    build = {
        "protocol_schema": ready.get("schema", "NOT_MEASURED"),
        "protocol_version": ready.get("protocol_version", "NOT_MEASURED"),
        "worker_identity": ready.get("worker_identity", "NOT_MEASURED"),
        "compiler_identity": ready.get("compiler_identity", "NOT_MEASURED"),
        "build_type": ready.get("build_type", "NOT_MEASURED"),
        "worker_executable": str(args.worker_executable),
        "worker_count": args.workers,
        "result_timeout_seconds": args.result_timeout_seconds,
        "platform": default_hardware_metadata()["platform"],
    }
    warmup_policy = {
        "warmup_games": args.warmup_games,
        "warmup_indices": {
            "start": 0,
            "stop": args.warmup_games,
            "count": args.warmup_games,
        },
        "steady_state_indices": {
            "start": args.warmup_games,
            "stop": args.warmup_games + args.games,
            "count": args.games,
        },
        "master_seed": args.master_seed,
        "same_master_seed_for_warmup_and_steady": True,
        "starting_player_mode": args.starting_player_mode,
        "seat_mode": args.seat_mode,
        "steady_timer_start": "after_warmup_completion",
        "steady_timer_stop": "after_final_result_publication",
    }
    report = build_report(
        canonical_environment=canonical_environment,
        hardware=default_hardware_metadata(),
        build=build,
        warmup_policy=warmup_policy,
        mode=args.mode,
        observation_mode=args.observation_mode,
        instrumentation=args.instrument,
        trace_persistence=args.trace_persistence,
        games_requested=args.games,
        workers_requested=args.workers,
        cold_start={
            "wall_clock_seconds": cold_start_seconds,
            "ready_workers": len(ready_messages),
            "worker_count": args.workers,
            "process_ready_time_domain": "coordinator_process_and_ready_handshake",
        },
        steady_state=steady_state,
        jobs=steady_results,
    )
    write_report(output_path, report)
    print(
        f"M4 benchmark written: {output_path} "
        f"({steady_state['games_completed']} games, {steady_state['games_per_second']:.6f} games/s)"
    )
    return 0


def main(arguments: Sequence[str] | None = None) -> int:
    try:
        return _run_benchmark_cli(arguments)
    except (BenchmarkIntegrityError, CoordinatorError, ProtocolValidationError, ValueError) as error:
        print(f"M4 benchmark failed: {error}", file=sys.stderr)
        return 2


__all__ = [
    "CoordinatorError",
    "PersistentWorkerPool",
    "SimulationCoordinator",
    "WorkerPool",
    "WorkerRuntimeError",
    "WorkerStartupError",
    "main",
]


if __name__ == "__main__":  # pragma: no cover - exercised by CLI smoke tests
    raise SystemExit(main())
