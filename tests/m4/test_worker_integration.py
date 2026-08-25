from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest

from tools.m4.benchmark import PersistentWorkerPool
from tools.m4.job_generation import derive_job_with_options
from tools.m4.worker_protocol import (
    HandshakeExpectation,
    job_to_message,
    validate_ready,
    validate_result,
)
from tools.m4.worker_protocol_contract import CANONICAL_RULES_BUNDLE_ID


ROOT = Path(__file__).resolve().parents[2]
NATIVE_WORKER = Path(
    os.environ.get("YGO_M4_WORKER", str(ROOT / "build" / "windows-zig" / "ygo_m4_worker.exe"))
)


def make_jobs(
    count: int,
    *,
    mode: str = "throughput",
    persist_trace: bool = False,
    observation_mode: str = "full",
) -> list[dict[str, object]]:
    return [
        derive_job_with_options(
            20260815,
            index,
            mode=mode,
            observation_mode=observation_mode,
            instrumentation=True,
            persist_trace=persist_trace,
        )
        for index in range(count)
    ]


def semantic_projection(result: dict[str, object]) -> tuple[object, ...]:
    return tuple(
        result[key]
        for key in (
            "job_id",
            "status",
            "terminal",
            "winner",
            "win_reason",
            "engine_steps",
            "interactive_decisions",
            "semantic_action_count",
            "gameplay_hash",
            "errors",
        )
    )


def _stderr_tail(path: Path | None, *, limit: int = 4096) -> str:
    if path is None:
        return ""
    try:
        return path.read_text(encoding="utf-8", errors="replace")[-limit:]
    except OSError as error:
        return f"<stderr unavailable: {error}>"


def write_pool_failure_diagnostics(
    pool: PersistentWorkerPool,
    *,
    test_name: str,
    phase: str,
    worker_count: int,
    jobs: list[dict[str, object]],
    started_ns: int,
    error: Exception,
) -> None:
    """Persist bounded lifecycle diagnostics when CI exposes an artifact dir."""

    artifact_dir = os.environ.get("YGO_M4_FAILURE_ARTIFACT_DIR")
    if not artifact_dir:
        return
    try:
        root = Path(artifact_dir)
        root.mkdir(parents=True, exist_ok=True)
        workers = []
        for state in pool._states:
            process = state.process
            in_flight = state.in_flight
            dispatched_ns = getattr(in_flight, "dispatched_ns", None)
            in_flight_job = getattr(in_flight, "job", None)
            stderr_path = state.stderr_path
            stderr_bytes = 0
            if stderr_path is not None:
                try:
                    stderr_bytes = stderr_path.stat().st_size
                except OSError:
                    pass
            workers.append(
                {
                    "worker_index": state.index,
                    "pid": state.pid,
                    "returncode": process.poll() if process is not None else None,
                    "alive": state.alive,
                    "ready": state.ready is not None,
                    "retired": state.retired,
                    "reaped": state.reaped,
                    "in_flight_job_id": (
                        str(in_flight_job["job_id"])
                        if isinstance(in_flight_job, dict) and "job_id" in in_flight_job
                        else None
                    ),
                    "in_flight_elapsed_seconds": (
                        round((time.perf_counter_ns() - dispatched_ns) / 1_000_000_000, 3)
                        if dispatched_ns is not None
                        else None
                    ),
                    "stderr_bytes": stderr_bytes,
                    "stderr_tail": _stderr_tail(stderr_path),
                }
            )
        metadata_keys = (
            "games_completed",
            "failed_games",
            "worker_crashes",
            "worker_errors",
            "malformed_protocol",
            "handshake_errors",
            "retries",
        )
        payload = {
            "schema": "ocgforge.m4.integration-diagnostic.v1",
            "test": test_name,
            "phase": phase,
            "worker_count": worker_count,
            "job_ids": [str(job["job_id"]) for job in jobs],
            "exception_type": type(error).__name__,
            "exception": str(error),
            "timeout_category": (
                "worker_result_timeout"
                if "timed out waiting for a worker result" in str(error)
                else "integration_failure"
            ),
            "elapsed_seconds": round((time.perf_counter_ns() - started_ns) / 1_000_000_000, 3),
            "result_timeout_seconds": pool.result_timeout_seconds,
            "metadata": {
                key: pool.last_run_metadata.get(key)
                for key in metadata_keys
                if key in pool.last_run_metadata
            },
            "workers": workers,
        }
        safe_name = "".join(
            character if character.isalnum() or character in {"-", "_"} else "_"
            for character in f"{test_name}-{phase}-w{worker_count}"
        )
        (root / f"{safe_name}.json").write_text(
            json.dumps(payload, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (OSError, TypeError, ValueError):
        # Diagnostics must never replace the original integration failure.
        return


def run_pool_with_diagnostics(
    jobs: list[dict[str, object]],
    *,
    worker_count: int,
    output_dir: Path,
    test_name: str,
    phase: str,
) -> list[dict[str, object]]:
    pool = PersistentWorkerPool(
        NATIVE_WORKER,
        worker_count=worker_count,
        output_dir=output_dir,
    )
    started_ns = time.perf_counter_ns()
    with pool:
        try:
            return pool.run(jobs, require_primary_integrity=True)
        except Exception as error:
            write_pool_failure_diagnostics(
                pool,
                test_name=test_name,
                phase=phase,
                worker_count=worker_count,
                jobs=jobs,
                started_ns=started_ns,
                error=error,
            )
            raise


def run_native(jobs: list[dict[str, object]]) -> tuple[dict[str, object], list[dict[str, object]]]:
    if not NATIVE_WORKER.is_file():
        raise unittest.SkipTest(f"native worker is unavailable: {NATIVE_WORKER}")
    process = subprocess.Popen(
        [str(NATIVE_WORKER)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        errors="strict",
    )
    assert process.stdin is not None
    assert process.stdout is not None
    payload = "\n".join(
        json.dumps(job_to_message(job), separators=(",", ":"), ensure_ascii=False)
        for job in jobs
    ) + "\n"
    try:
        stdout, _ = process.communicate(input=payload, timeout=240)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate()
        raise
    lines = stdout.splitlines()
    if not lines or any(not line.strip() for line in lines):
        raise AssertionError("native worker emitted blank or missing JSONL output")
    ready = json.loads(lines[0])
    validate_ready(ready, HandshakeExpectation.canonical())
    results: list[dict[str, object]] = []
    if len(lines) != len(jobs) + 1:
        raise AssertionError(f"expected {len(jobs) + 1} worker lines, got {len(lines)}")
    for line, job in zip(lines[1:], jobs):
        result = json.loads(line)
        validate_result(result, str(job["job_id"]))
        results.append(result)
    if process.returncode != 0:
        raise AssertionError(f"native worker exited with {process.returncode}")
    return ready, results


class NativeWorkerIntegrationTests(unittest.TestCase):
    def test_valid_invalid_valid_sequence_is_explicit_and_recoverable(self) -> None:
        jobs = make_jobs(3)
        jobs[1]["max_steps"] = 0
        ready, results = run_native(jobs)
        self.assertEqual(ready["pid"], results[0]["worker"]["pid"])
        self.assertEqual([result["job_id"] for result in results], [job["job_id"] for job in jobs])
        self.assertEqual(results[0]["status"], "passed")
        self.assertTrue(results[0]["terminal"])
        self.assertGreater(results[0]["simulation_elapsed_us"], 0)
        self.assertEqual(results[0]["gameplay_hash"].__class__, str)
        self.assertEqual(results[0]["errors"], {
            "retries": 0,
            "unsupported": 0,
            "automatic": 0,
            "truncated": 0,
            "core_errors": 0,
            "worker_errors": 0,
        })
        self.assertEqual(results[1]["status"], "failed")
        self.assertFalse(results[1]["terminal"])
        self.assertEqual(results[1]["failure_code"], "nonterminal")
        self.assertGreater(results[1]["errors"]["worker_errors"], 0)
        self.assertEqual(results[2]["status"], "passed")
        self.assertTrue(results[2]["terminal"])

    def test_conformance_and_throughput_have_equal_semantic_results(self) -> None:
        throughput_jobs = make_jobs(4, mode="throughput")
        conformance_jobs = make_jobs(4, mode="conformance")
        _, throughput = run_native(throughput_jobs)
        _, conformance = run_native(conformance_jobs)
        self.assertEqual(
            [semantic_projection(result) for result in throughput],
            [semantic_projection(result) for result in conformance],
        )
        self.assertTrue(all(result["trace_hash"] is None for result in throughput))
        self.assertTrue(all(isinstance(result["trace_hash"], str) for result in conformance))

    def test_worker_counts_preserve_semantics_and_trace_hashes(self) -> None:
        baseline_jobs = make_jobs(8)
        baseline_projection: list[tuple[object, ...]] | None = None
        trace_baseline: list[str] | None = None
        for worker_count in (1, 2, 4, 8):
            with self.subTest(worker_count=worker_count), tempfile.TemporaryDirectory(
                prefix=f"ocgforge-m4-integration-w{worker_count}-"
            ) as directory:
                results = run_pool_with_diagnostics(
                    baseline_jobs,
                    worker_count=worker_count,
                    output_dir=Path(directory),
                    test_name="worker-counts",
                    phase="throughput",
                )
                projection = [semantic_projection(result) for result in results]
                if baseline_projection is None:
                    baseline_projection = projection
                self.assertEqual(projection, baseline_projection)

            with self.subTest(worker_count=f"{worker_count}-conformance"), tempfile.TemporaryDirectory(
                prefix=f"ocgforge-m4-trace-w{worker_count}-"
            ) as directory:
                trace_jobs = make_jobs(8, mode="conformance", persist_trace=True)
                for job in trace_jobs:
                    job["trace_output"] = str(Path(directory) / f"{job['job_id']}.jsonl")
                results = run_pool_with_diagnostics(
                    trace_jobs,
                    worker_count=worker_count,
                    output_dir=Path(directory),
                    test_name="worker-counts",
                    phase="conformance",
                )
                self.assertTrue(
                    all(isinstance(result["trace_hash"], str) and result["trace_hash"] for result in results)
                )
                traces = [result["trace_hash"] for result in results]
                if trace_baseline is None:
                    trace_baseline = traces
                self.assertEqual(traces, trace_baseline)


if __name__ == "__main__":
    unittest.main()
