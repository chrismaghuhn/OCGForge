from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

from tools.m4.benchmark import (
    CoordinatorError,
    PersistentWorkerPool,
    WorkerStartupError,
    WorkerRuntimeError,
)
from tools.m4.job_generation import derive_job
from tools.m4.worker_protocol import (
    HandshakeExpectation,
    ProtocolValidationError,
    assert_primary_integrity,
    validate_ready,
    validate_result,
)
from tools.m4.worker_protocol_contract import (
    CANONICAL_DECK_HASHES,
    CANONICAL_PATCHSET_SHA256,
    CANONICAL_RULES_BUNDLE_ID,
)


ROOT = Path(__file__).resolve().parents[2]
FAKE_WORKER = Path(__file__).with_name("fake_worker_crash.py")


def jobs(count: int = 3) -> list[dict[str, object]]:
    return [derive_job(20260815, index) for index in range(count)]


def fake_command(behavior: str, marker: Path | None = None) -> list[str]:
    command = [sys.executable, str(FAKE_WORKER), "--behavior", behavior]
    if marker is not None:
        command.extend(["--marker", str(marker)])
    return command


class WorkerProtocolValidationTests(unittest.TestCase):
    def test_ready_requires_exact_expected_identity(self) -> None:
        expectation = HandshakeExpectation.canonical()
        message = {
            "schema": expectation.protocol_schema,
            "type": "ready",
            "protocol_version": expectation.protocol_version,
            "pid": 123,
            "rules_bundle_id": expectation.rules_bundle_id,
            "core_patchset_sha256": expectation.patchset_sha256,
            "deck_hashes": list(expectation.deck_hashes),
            "format_id": expectation.format_id,
            "duel_mode_name": expectation.duel_mode,
            "duel_flags": expectation.duel_flags,
            "compiler_identity": "test",
            "build_type": "Test",
            "worker_identity": expectation.worker_identity,
        }
        validate_ready(message, expectation)
        message["duel_flags"] = 0
        with self.assertRaises(ProtocolValidationError):
            validate_ready(message, expectation)

    def test_result_validation_and_primary_integrity_fail_closed(self) -> None:
        message = {
            "schema": "ocgforge.m4.worker.v1",
            "type": "result",
            "status": "passed",
            "job_id": "m4-000000",
            "terminal": True,
            "winner": 0,
            "win_reason": 1,
            "engine_steps": 1,
            "interactive_decisions": 1,
            "semantic_action_count": 1,
            "gameplay_hash": "a" * 64,
            "trace_hash": None,
            "simulation_elapsed_us": 1,
            "coordinator_elapsed_us": None,
            "errors": {
                "retries": 0,
                "unsupported": 0,
                "automatic": 0,
                "truncated": 0,
                "core_errors": 0,
                "worker_errors": 0,
            },
            "timing_us": {
                "core_process": 0,
                "protocol_candidate": 0,
                "continuation": 0,
                "observation": 0,
                "trace_hash": 0,
                "serialization": 0,
                "other": 0,
                "trace_persistence": 0,
            },
            "counters": {
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
            },
            "worker": {
                "pid": 123,
                "restart_index": 0,
                "crashed": False,
                "restarted": False,
            },
            "failure_code": None,
            "error_message": None,
        }
        validate_result(message, "m4-000000")
        published = copy.deepcopy(message)
        published["coordinator"] = {
            "worker_crashed": False,
            "worker_restarted": False,
        }
        published["coordinator_errors"] = {
            "retries": 0,
            "handshake": 0,
            "malformed_protocol": 0,
            "failed_games": 0,
            "worker_crashes": 0,
            "worker_restarts": 0,
        }
        assert_primary_integrity(published)
        invalid = copy.deepcopy(message)
        invalid["coordinator"] = published["coordinator"]
        invalid["coordinator_errors"] = published["coordinator_errors"]
        invalid["errors"]["worker_errors"] = 1
        with self.assertRaises(ProtocolValidationError):
            assert_primary_integrity(invalid)


class FailureIsolationTests(unittest.TestCase):
    def test_invalid_jobs_fail_without_poisoning_healthy_pool(self) -> None:
        workload = jobs(4)
        workload[0]["seed"] = -1
        workload[1]["replay_actions"] = None
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-invalid-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)

                self.assertEqual(
                    [result["job_id"] for result in results],
                    [job["job_id"] for job in workload],
                )
                self.assertEqual([result["status"] for result in results], [
                    "failed", "failed", "passed", "passed"
                ])
                self.assertEqual(results[0]["failure_code"], "invalid_job")
                self.assertIn("nonnegative", results[0]["error_message"])
                self.assertEqual(results[1]["failure_code"], "invalid_job")
                self.assertEqual(results[1]["coordinator"]["job_validation_error"], True)
                self.assertFalse(results[1]["worker"]["crashed"])
                self.assertEqual(pool.last_run_metadata["worker_crashes"], 0)
                self.assertEqual(pool.last_run_metadata["retries"], 0)
                self.assertTrue(pool._states[0].alive)
                self.assertIsNone(pool._states[0].in_flight)

    def test_valid_invalid_valid_jobs_are_explicit_and_sorted(self) -> None:
        workload = jobs()
        workload[1]["force_unsupported"] = True
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)
                metadata = pool.last_run_metadata

            self.assertEqual([result["job_id"] for result in results], [job["job_id"] for job in workload])
            self.assertEqual([result["status"] for result in results], ["passed", "failed", "passed"])
            self.assertEqual(results[1]["failure_code"], "unsupported")
            self.assertEqual(results[1]["errors"]["unsupported"], 1)
            assert_primary_integrity(results[0])
            self.assertEqual(metadata["worker_crashes"], 0)
            self.assertEqual(metadata["retries"], 0)
            stderr_path = Path(metadata["workers"][0]["stderr_path"])
            self.assertTrue(stderr_path.is_file())
            self.assertGreaterEqual(metadata["workers"][0]["stderr_bytes"], 0)

    def test_worker_crash_is_failed_without_hidden_retry(self) -> None:
        workload = jobs(2)
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-crash-") as directory:
            with PersistentWorkerPool(
                fake_command("crash-first-job", Path(directory) / "crashed.marker"),
                worker_count=1,
                output_dir=Path(directory),
                restart_workers=True,
            ) as pool:
                results = pool.run(workload)
                metadata = pool.last_run_metadata

            self.assertEqual([result["job_id"] for result in results], [job["job_id"] for job in workload])
            self.assertEqual(results[0]["status"], "failed")
            self.assertEqual(results[0]["failure_code"], "worker_crash")
            self.assertTrue(results[0]["coordinator"]["worker_crashed"])
            self.assertEqual(results[0]["errors"]["worker_errors"], 1)
            self.assertIsNone(results[0]["simulation_elapsed_us"])
            self.assertIsInstance(results[0]["coordinator_elapsed_us"], int)
            self.assertEqual(results[1]["status"], "passed")
            self.assertEqual(metadata["worker_crashes"], 1)
            self.assertEqual(metadata["retries"], 0)
            self.assertGreaterEqual(metadata["worker_restarts"], 1)

    def test_pool_rejects_later_run_after_final_worker_crash_without_hanging(self) -> None:
        workload = jobs(1)
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-lifecycle-") as directory:
            pool = PersistentWorkerPool(
                fake_command("crash-first-job", Path(directory) / "crashed.marker"),
                worker_count=1,
                output_dir=Path(directory),
                restart_workers=True,
                result_timeout_seconds=0.05,
            )
            try:
                first_results = pool.run(workload)
                self.assertEqual(first_results[0]["job_id"], workload[0]["job_id"])
                self.assertEqual(first_results[0]["failure_code"], "worker_crash")
                with self.assertRaisesRegex(
                    WorkerRuntimeError, "not reusable after worker failure"
                ):
                    pool.run(jobs(2)[1:])
            finally:
                pool.close()

    def test_timeout_retires_pool_and_clears_stale_mapping(self) -> None:
        workload = jobs(1)
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-timeout-") as directory:
            pool = PersistentWorkerPool(
                fake_command("hang-first-job"),
                worker_count=1,
                output_dir=Path(directory),
                result_timeout_seconds=0.05,
            )
            started = time.monotonic()
            try:
                with self.assertRaisesRegex(
                    WorkerRuntimeError, "timed out waiting for a worker result"
                ):
                    pool.run(workload)
                self.assertLess(time.monotonic() - started, 5.0)
                self.assertTrue(pool._unusable)
                self.assertEqual(set(pool._results), {workload[0]["job_id"]})
                self.assertTrue(all(state.in_flight is None for state in pool._states))
                self.assertTrue(all(state.reaped for state in pool._states))
                self.assertTrue(all(state.process is not None for state in pool._states))
                self.assertTrue(all(state.process.poll() is not None for state in pool._states))
                with self.assertRaisesRegex(
                    WorkerRuntimeError, "not reusable after fail-closed shutdown"
                ):
                    pool.run(jobs(2))
            finally:
                pool.close()

    def test_invalid_utf8_is_explicit_malformed_protocol_failure(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-utf8-") as directory:
            with PersistentWorkerPool(
                fake_command("invalid-utf8"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(jobs(1))

                self.assertEqual(results[0]["status"], "failed")
                self.assertEqual(results[0]["failure_code"], "malformed_protocol")
                self.assertIn("invalid UTF-8", results[0]["error_message"])
                self.assertEqual(results[0]["coordinator_errors"]["malformed_protocol"], 1)
                self.assertEqual(results[0]["coordinator_errors"]["worker_crashes"], 0)
                self.assertIsNone(results[0]["simulation_elapsed_us"])

    def test_crash_after_ready_is_distinguished_from_job_failure(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-ready-crash-") as directory:
            pool = PersistentWorkerPool(
                fake_command("crash-after-ready"),
                worker_count=1,
                output_dir=Path(directory),
            )
            with self.assertRaises(WorkerStartupError):
                pool.start()
            self.assertEqual(pool.last_run_metadata["worker_crashes"], 1)
            pool.close()

    def test_handshake_mismatch_fails_before_dispatch(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-handshake-") as directory:
            pool = PersistentWorkerPool(
                [
                    sys.executable,
                    str(FAKE_WORKER),
                    "--behavior",
                    "normal",
                ],
                worker_count=1,
                output_dir=Path(directory),
                expected=HandshakeExpectation(
                    rules_bundle_id="wrong-rules-bundle",
                    patchset_sha256=CANONICAL_PATCHSET_SHA256,
                    deck_hashes=CANONICAL_DECK_HASHES,
                ),
            )
            with self.assertRaises(WorkerStartupError):
                pool.start()
            pool.close()


if __name__ == "__main__":
    unittest.main()
