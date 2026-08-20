from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
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
                with PersistentWorkerPool(
                    NATIVE_WORKER,
                    worker_count=worker_count,
                    output_dir=Path(directory),
                ) as pool:
                    results = pool.run(baseline_jobs, require_primary_integrity=True)
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
                with PersistentWorkerPool(
                    NATIVE_WORKER,
                    worker_count=worker_count,
                    output_dir=Path(directory),
                ) as pool:
                    results = pool.run(trace_jobs, require_primary_integrity=True)
                self.assertTrue(
                    all(isinstance(result["trace_hash"], str) and result["trace_hash"] for result in results)
                )
                traces = [result["trace_hash"] for result in results]
                if trace_baseline is None:
                    trace_baseline = traces
                self.assertEqual(traces, trace_baseline)


if __name__ == "__main__":
    unittest.main()
