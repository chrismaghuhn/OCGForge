from __future__ import annotations

import copy
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

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
        published["coordinator_elapsed_us"] = 17
        published["coordinator_errors"] = {
            "retries": 0,
            "handshake": 0,
            "malformed_protocol": 0,
            "failed_games": 0,
            "worker_crashes": 0,
            "worker_restarts": 0,
        }
        assert_primary_integrity(published)
        missing_timing = copy.deepcopy(published)
        missing_timing.pop("coordinator_elapsed_us")
        with self.assertRaises(ProtocolValidationError):
            assert_primary_integrity(missing_timing)
        for invalid_value in (None, True):
            invalid_timing = copy.deepcopy(published)
            invalid_timing["coordinator_elapsed_us"] = invalid_value
            with self.subTest(invalid_value=invalid_value), self.assertRaises(
                ProtocolValidationError
            ):
                assert_primary_integrity(invalid_timing)
        max_uint64 = copy.deepcopy(published)
        max_uint64["coordinator_elapsed_us"] = (1 << 64) - 1
        assert_primary_integrity(max_uint64)
        for invalid_value in ((1 << 64), -1):
            invalid_timing = copy.deepcopy(published)
            invalid_timing["coordinator_elapsed_us"] = invalid_value
            with self.assertRaises(ProtocolValidationError):
                assert_primary_integrity(invalid_timing)
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

    def test_outgoing_surrogates_are_invalid_jobs_without_worker_crash(self) -> None:
        for field, value in (
            ("setup_script", "\ud800"),
            ("replay_actions", ["\ud800"]),
        ):
            with self.subTest(field=field), tempfile.TemporaryDirectory(
                prefix="ocgforge-m4-task5-surrogate-"
            ) as directory:
                workload = jobs(2)
                workload[0][field] = value
                with PersistentWorkerPool(
                    fake_command("normal"),
                    worker_count=1,
                    output_dir=Path(directory),
                ) as pool:
                    results = pool.run(workload)

                    self.assertEqual([result["status"] for result in results], ["failed", "passed"])
                    self.assertEqual(results[0]["failure_code"], "invalid_job")
                    self.assertEqual(results[0]["coordinator"]["job_validation_error"], True)
                    self.assertFalse(results[0]["worker"]["crashed"])
                    self.assertEqual(pool.last_run_metadata["worker_crashes"], 0)
                    self.assertIsNone(pool._states[0].in_flight)
                    self.assertTrue(pool._states[0].alive)

    def test_unpaired_job_id_is_invalid_without_worker_crash(self) -> None:
        workload = jobs(2)
        invalid_job_id = "m4-invalid-\ud800"
        workload[0]["job_id"] = invalid_job_id
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-job-id-invalid-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)

                invalid_result = next(
                    result for result in results if result["job_id"] == invalid_job_id
                )
                self.assertEqual(invalid_result["status"], "failed")
                self.assertEqual(invalid_result["failure_code"], "invalid_job")
                self.assertEqual(invalid_result["coordinator"]["job_validation_error"], True)
                self.assertFalse(invalid_result["worker"]["crashed"])
                self.assertEqual(pool.last_run_metadata["worker_crashes"], 0)
                self.assertIsNone(pool._states[0].in_flight)
                self.assertTrue(pool._states[0].alive)

    def test_surrogate_pair_job_id_matches_and_publishes_normalized_identity(self) -> None:
        workload = jobs(2)
        workload[0]["job_id"] = "m4-\ud83d\ude00"
        canonical_job_id = "m4-\U0001f600"
        expected_input_ids = [canonical_job_id, workload[1]["job_id"]]
        expected_published_ids = [workload[1]["job_id"], canonical_job_id]
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-job-id-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)

                self.assertEqual([result["status"] for result in results], ["passed", "passed"])
                self.assertEqual([result["job_id"] for result in results], expected_published_ids)
                self.assertEqual(pool.last_run_metadata["result_order"], expected_input_ids)
                self.assertEqual(pool.last_run_metadata["malformed_protocol"], 0)
                self.assertEqual(pool.last_run_metadata["worker_crashes"], 0)
                self.assertEqual(pool.last_run_metadata["worker_restarts"], 0)
                self.assertTrue(pool._states[0].alive)
                self.assertIsNone(pool._states[0].in_flight)

    def test_valid_surrogate_pairs_keep_worker_usable(self) -> None:
        workload = jobs(2)
        workload[0]["setup_script"] = "\ud83d\ude00"
        workload[0]["replay_actions"] = ["\ud83d\ude00"]
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-surrogate-pair-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)

                self.assertEqual([result["status"] for result in results], ["passed", "passed"])
                self.assertEqual(pool.last_run_metadata["worker_crashes"], 0)
                self.assertEqual(pool.last_run_metadata["worker_restarts"], 0)
                self.assertTrue(pool._states[0].alive)
                self.assertIsNone(pool._states[0].in_flight)

    def test_primary_integrity_rejects_local_invalid_job_results(self) -> None:
        workload = jobs(1)
        workload[0]["setup_script"] = "\ud800"
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-primary-invalid-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)
                self.assertEqual(results[0]["failure_code"], "invalid_job")
                with self.assertRaises(ProtocolValidationError):
                    pool.run(workload, require_primary_integrity=True)
                self.assertTrue(pool._unusable)
                self.assertTrue(all(state.retired for state in pool._states))
                self.assertTrue(all(state.reaped for state in pool._states))
                self.assertTrue(all(not state.alive for state in pool._states))
                self.assertTrue(all(state.in_flight is None for state in pool._states))

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

    def test_extra_worker_result_is_not_accepted_as_the_next_job(self) -> None:
        workload = jobs(2)
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-extra-result-") as directory:
            marker = Path(directory) / "extra-result.marker"
            with PersistentWorkerPool(
                fake_command("extra-first-job", marker),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)
                metadata = pool.last_run_metadata
                self.assertTrue(pool._states[0].alive)
                self.assertIsNone(pool._states[0].in_flight)

            self.assertEqual([result["status"] for result in results], ["passed", "passed"])
            self.assertEqual([result["job_id"] for result in results], [job["job_id"] for job in workload])
            self.assertEqual(metadata["malformed_protocol"], 1)
            self.assertEqual(metadata["worker_errors"], 1)
            self.assertEqual(metadata["worker_restarts"], 1)

    def test_result_worker_pid_mismatch_is_rejected_before_publication(self) -> None:
        workload = jobs(2)
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-result-pid-") as directory:
            marker = Path(directory) / "pid-mismatch.marker"
            with PersistentWorkerPool(
                fake_command("result-pid-mismatch", marker),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(workload)
                metadata = pool.last_run_metadata

                self.assertEqual(results[0]["status"], "failed")
                self.assertEqual(results[0]["failure_code"], "malformed_protocol")
                self.assertIsNone(results[0]["gameplay_hash"])
                self.assertEqual(
                    results[0]["worker"]["pid"],
                    results[0]["coordinator"]["worker_pid"],
                )
                self.assertEqual(results[1]["status"], "passed")
                self.assertEqual(
                    results[1]["worker"]["pid"],
                    results[1]["coordinator"]["worker_pid"],
                )
                self.assertIsNotNone(pool._states[0].process)
                replacement_pid = pool._states[0].process.pid
                self.assertEqual(results[1]["worker"]["pid"], replacement_pid)
                self.assertEqual(pool._live_pids(), [replacement_pid])
                self.assertEqual(metadata["malformed_protocol"], 1)
                self.assertEqual(metadata["worker_restarts"], 1)

    def test_coordinator_receipt_timing_excludes_lifecycle_settle_barrier(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-receipt-timing-") as directory:
            with PersistentWorkerPool(
                fake_command("normal"),
                worker_count=1,
                output_dir=Path(directory),
                lifecycle_settle_timeout_seconds=0.20,
            ) as pool:
                results = pool.run(jobs(1))

            self.assertEqual(results[0]["status"], "passed")
            self.assertLess(results[0]["coordinator_elapsed_us"], 100_000)

    def test_result_then_exit_is_not_published_as_primary_evidence(self) -> None:
        class ExitObservedPool(PersistentWorkerPool):
            def __init__(self, *args, **kwargs) -> None:
                super().__init__(*args, **kwargs)
                self._exit_observed = False

            def _next_event(self):
                state, event = super()._next_event()
                if not self._exit_observed and event[0] == "line":
                    self._exit_observed = True
                    assert state.process is not None
                    state.process.wait(timeout=2.0)
                    if state.reader is not None:
                        state.reader.join(timeout=2.0)
                return state, event

        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-result-exit-") as directory:
            with ExitObservedPool(
                fake_command("result-then-exit"),
                worker_count=1,
                output_dir=Path(directory),
            ) as pool:
                results = pool.run(jobs(1))
                metadata = pool.last_run_metadata

            self.assertEqual(results[0]["status"], "failed")
            self.assertEqual(results[0]["failure_code"], "worker_crash")
            self.assertIsNone(results[0]["gameplay_hash"])
            self.assertTrue(results[0]["coordinator"]["worker_crashed"])
            self.assertEqual(metadata["worker_crashes"], 1)
            self.assertTrue(pool._states[0].retired)
            self.assertTrue(pool._states[0].reaped)
            self.assertIsNone(pool._states[0].in_flight)

    def test_result_then_exit_never_publishes_passed_under_repeated_scheduling(self) -> None:
        workload = jobs(1)
        observed: list[dict[str, object]] = []
        repetitions = 100
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-result-exit-stress-") as directory:
            root = Path(directory)
            for repetition in range(repetitions):
                iteration_dir = root / f"run-{repetition:03d}"
                iteration_dir.mkdir()
                with PersistentWorkerPool(
                    fake_command("result-then-exit"),
                    worker_count=1,
                    output_dir=iteration_dir,
                ) as pool:
                    results = pool.run(workload)
                    self.assertEqual(len(results), 1)
                    self.assertEqual(pool.last_run_metadata["games_completed"], 1)
                    self.assertEqual(pool.last_run_metadata["retries"], 0)
                    observed.append(results[0])

        self.assertEqual(len(observed), repetitions)
        self.assertTrue(all(result["status"] == "failed" for result in observed))
        self.assertTrue(
            all(result["failure_code"] == "worker_crash" for result in observed)
        )
        self.assertTrue(
            all(result["coordinator"]["worker_crashed"] for result in observed)
        )
        self.assertTrue(all(result["gameplay_hash"] is None for result in observed))

    def test_deep_malformed_json_is_explicit_and_retires_worker(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-deep-json-") as directory:
            pool = PersistentWorkerPool(
                fake_command("deep-malformed"),
                worker_count=1,
                output_dir=Path(directory),
            )
            try:
                results = pool.run(jobs(1))

                self.assertEqual(results[0]["status"], "failed")
                self.assertEqual(results[0]["failure_code"], "malformed_protocol")
                self.assertEqual(pool.last_run_metadata["malformed_protocol"], 1)
                self.assertEqual(pool.last_run_metadata["worker_errors"], 1)
                self.assertTrue(pool._states[0].retired)
                self.assertTrue(pool._states[0].reaped)
                self.assertIsNone(pool._states[0].in_flight)
                self.assertIsNotNone(pool._states[0].process)
                self.assertIsNotNone(pool._states[0].process.poll())
            finally:
                pool.close()

    def test_timeout_uses_hung_job_deadline_despite_periodic_peer_results(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-deadline-") as directory:
            pool = PersistentWorkerPool(
                fake_command("one-hung-one-periodic", Path(directory) / "roles.marker"),
                worker_count=2,
                output_dir=Path(directory),
                result_timeout_seconds=0.05,
            )
            pool.start()
            started = time.monotonic()
            try:
                with self.assertRaisesRegex(
                    WorkerRuntimeError, "timed out waiting for a worker result"
                ):
                    pool.run(jobs(8))
                self.assertLess(time.monotonic() - started, 0.20)
                self.assertTrue(pool._unusable)
                self.assertTrue(all(state.in_flight is None for state in pool._states))
                self.assertTrue(all(state.reaped for state in pool._states))
                self.assertTrue(
                    all(state.process is not None and state.process.poll() is not None for state in pool._states)
                )
            finally:
                pool.close()

    def test_replacement_handshake_failure_cleans_up_every_worker(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-replacement-") as directory:
            pool = PersistentWorkerPool(
                fake_command(
                    "replacement-handshake-failure",
                    Path(directory) / "replacement-starts.marker",
                ),
                worker_count=2,
                output_dir=Path(directory),
            )
            pool.start()
            try:
                with self.assertRaisesRegex(
                    WorkerRuntimeError, "replacement worker .* failed handshake"
                ):
                    pool.run(jobs(3))
                self.assertTrue(pool._unusable)
                self.assertTrue(all(state.in_flight is None for state in pool._states))
                self.assertTrue(all(state.retired for state in pool._states))
                self.assertTrue(all(state.reaped for state in pool._states))
                self.assertTrue(
                    all(state.process is not None and state.process.poll() is not None for state in pool._states)
                )
                self.assertEqual(pool.last_run_metadata["handshake_errors"], 1)
            finally:
                pool.close()

    def test_replacement_launch_failure_retires_and_reaps_entire_pool(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-replacement-launch-") as directory:
            pool = PersistentWorkerPool(
                fake_command(
                    "replacement-handshake-failure",
                    Path(directory) / "replacement-starts.marker",
                ),
                worker_count=2,
                output_dir=Path(directory),
            )
            pool.start()
            try:
                with mock.patch.object(
                    pool, "_launch", side_effect=OSError("replacement launch failed")
                ):
                    with self.assertRaises(WorkerRuntimeError):
                        pool.run(jobs(3))
                self.assertTrue(pool._unusable)
                self.assertTrue(all(state.in_flight is None for state in pool._states))
                self.assertTrue(all(state.retired for state in pool._states))
                self.assertTrue(all(state.reaped for state in pool._states))
                self.assertTrue(all(not state.alive for state in pool._states))
                self.assertTrue(
                    all(state.process is not None and state.process.poll() is not None for state in pool._states)
                )
            finally:
                pool.close()

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

    def test_restart_disabled_failure_retires_and_reaps_every_worker(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-no-restart-cleanup-") as directory:
            pool = PersistentWorkerPool(
                fake_command("crash-first-job", Path(directory) / "crashed.marker"),
                worker_count=1,
                output_dir=Path(directory),
                restart_workers=False,
            )
            try:
                with self.assertRaisesRegex(
                    WorkerRuntimeError, "restart disabled|unassigned jobs remaining"
                ):
                    pool.run(jobs(2))
                self.assertTrue(pool._unusable)
                self.assertTrue(all(state.retired for state in pool._states))
                self.assertTrue(all(state.reaped for state in pool._states))
                self.assertTrue(all(not state.alive for state in pool._states))
                self.assertTrue(all(state.in_flight is None for state in pool._states))
                self.assertTrue(
                    all(state.process is not None and state.process.poll() is not None for state in pool._states)
                )
            finally:
                pool.close()

    def test_nan_result_timeout_fails_before_worker_launch(self) -> None:
        for invalid_timeout in (math.nan, math.inf, -math.inf, 0, -1):
            with self.subTest(invalid_timeout=invalid_timeout), tempfile.TemporaryDirectory(
                prefix="ocgforge-m4-task5-invalid-timeout-"
            ) as directory:
                pool = PersistentWorkerPool(
                    fake_command("normal"),
                    worker_count=1,
                    output_dir=Path(directory),
                    result_timeout_seconds=invalid_timeout,
                )
                try:
                    with self.assertRaisesRegex(ValueError, "result_timeout_seconds"):
                        pool.run(jobs(1))
                    self.assertTrue(pool._unusable)
                    self.assertEqual(pool._states, [])
                finally:
                    pool.close()

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

    def test_ready_pid_mismatch_is_rejected_without_overwriting_actual_pid(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task5-pid-") as directory:
            pool = PersistentWorkerPool(
                fake_command("pid-mismatch"),
                worker_count=1,
                output_dir=Path(directory),
            )
            try:
                with self.assertRaises(WorkerStartupError):
                    pool.start()
                state = pool._states[0]
                self.assertIsNotNone(state.process)
                self.assertEqual(state.pid, state.process.pid)
                self.assertNotEqual(state.ready["pid"] if state.ready else None, state.pid)
                self.assertEqual(pool.last_run_metadata["handshake_errors"], 1)
            finally:
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
