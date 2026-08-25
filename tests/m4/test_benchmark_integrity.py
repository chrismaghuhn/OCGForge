from __future__ import annotations

import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

from jsonschema import Draft202012Validator

from tools.m4.benchmark import PersistentWorkerPool
from tools.m4.report import (
    REPORT_SCHEMA_VERSION,
    BenchmarkIntegrityError,
    aggregate_results,
    build_baseline,
    build_report,
    build_argument_parser,
    default_hardware_metadata,
    percentile_summary,
    validate_complete_results,
)


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "docs" / "m4" / "m4_benchmark_schema.json"


def valid_result(job_id: str, *, gameplay_hash: str = "a" * 64) -> dict[str, object]:
    return {
        "schema": "ocgforge.m4.worker.v1",
        "type": "result",
        "status": "passed",
        "job_id": job_id,
        "terminal": True,
        "winner": 0,
        "win_reason": 1,
        "engine_steps": 10,
        "interactive_decisions": 4,
        "semantic_action_count": 3,
        "gameplay_hash": gameplay_hash,
        "trace_hash": None,
        "simulation_elapsed_us": 10,
        "coordinator_elapsed_us": 20,
        "errors": {
            "retries": 0,
            "unsupported": 0,
            "automatic": 0,
            "truncated": 0,
            "core_errors": 0,
            "worker_errors": 0,
        },
        "timing_us": {
            "core_process": 1,
            "protocol_candidate": 2,
            "continuation": 1,
            "observation": 1,
            "trace_hash": 1,
            "serialization": 0,
            "other": 4,
            "trace_persistence": 0,
        },
        "counters": {
            "ocg_duel_process": 2,
            "ocg_duel_query": 3,
            "ocg_duel_query_location": 4,
            "ocg_duel_query_field": 5,
            "ocg_duel_query_count": 6,
            "script_reader_requests": 7,
            "script_loads": 8,
            "observations": 9,
            "entities_projected": 10,
            "candidate_sets": 11,
            "candidate_total": 12,
            "candidate_max": 13,
            "semantic_hashes": 14,
            "trace_bytes_serialized": 15,
        },
        "worker": {
            "pid": 100,
            "restart_index": 0,
            "crashed": False,
            "restarted": False,
        },
        "failure_code": None,
        "error_message": None,
        "coordinator": {
            "stderr_path": "test-worker.stderr.log",
            "worker_index": 0,
            "worker_pid": 100,
            "worker_crashed": False,
            "worker_restarted": False,
            "worker_restart_index": 0,
        },
        "coordinator_errors": {
            "retries": 0,
            "handshake": 0,
            "malformed_protocol": 0,
            "failed_games": 0,
            "worker_crashes": 0,
            "worker_restarts": 0,
        },
    }


def valid_metadata() -> dict[str, object]:
    return {
        "handshake_errors": 0,
        "malformed_protocol": 0,
        "worker_crashes": 0,
        "worker_restarts": 0,
        "retries": 0,
        "failed_games": 0,
        "worker_errors": 0,
        "memory": {
            "process_count": "NOT_MEASURED",
            "peak_total_working_set_bytes": "NOT_MEASURED",
            "peak_worker_working_set_bytes": "NOT_MEASURED",
            "memory_per_active_environment_bytes": "NOT_MEASURED",
        },
    }


def valid_matrix_report(stderr_path: str, *, workers: int = 1) -> dict[str, object]:
    results = [valid_result(f"m4-{index:06d}") for index in range(4, 68)]
    for result in results:
        result["coordinator"]["stderr_path"] = stderr_path  # type: ignore[index]
    metadata = valid_metadata()
    metadata["memory"] = {
        "process_count": workers,
        "peak_total_working_set_bytes": 1024,
        "peak_worker_working_set_bytes": 1024,
        "memory_per_active_environment_bytes": 1024,
    }
    return build_report(
        canonical_environment={
            "format_id": "TCG_ADVANCED_2026_05_18",
            "duel_mode_name": "DUEL_MODE_MR5",
            "duel_flags": 190464,
            "rules_bundle_id": "a" * 64,
            "core_patchset_sha256": "b" * 64,
            "deck_hashes": ["c" * 64, "d" * 64],
        },
        hardware={
            "platform": "test",
            "system": "test",
            "machine": "test",
            "processor": "test",
            "python_version": "test",
            "physical_memory_bytes": 1024,
        },
        build={
            "compiler_identity": "test",
            "build_type": "Test",
            "worker_identity": "ocgforge.m4.native_worker.v1",
            "protocol_schema": "ocgforge.m4.worker.v1",
            "protocol_version": "ocgforge.m4.worker.v1",
            "worker_executable": "worker.exe",
            "worker_count": workers,
            "result_timeout_seconds": 120.0,
            "platform": "test",
        },
        warmup_policy={
            "warmup_games": 4,
            "warmup_indices": {"start": 0, "stop": 4, "count": 4},
            "steady_state_indices": {"start": 4, "stop": 68, "count": 64},
            "master_seed": 20260815,
            "same_master_seed_for_warmup_and_steady": True,
            "starting_player_mode": "balanced",
            "seat_mode": "balanced",
            "steady_timer_start": "after_warmup_completion",
            "steady_timer_stop": "after_final_result_publication",
        },
        mode="throughput",
        instrumentation=True,
        trace_persistence=False,
        games_requested=64,
        workers_requested=workers,
        cold_start={
            "wall_clock_seconds": 0.1,
            "ready_workers": workers,
            "worker_count": workers,
            "process_ready_time_domain": "test",
        },
        steady_state=aggregate_results(
            results,
            metadata,
            wall_clock_seconds=1.0,
            games_requested=64,
            workers_requested=workers,
        ),
        jobs=results,
    )


class BenchmarkIntegrityTests(unittest.TestCase):
    def test_baseline_refuses_missing_required_matrix_row(self) -> None:
        with self.assertRaisesRegex(BenchmarkIntegrityError, "missing required matrix row"):
            build_baseline({1: {}}, required_workers=(1, 2))

    def test_baseline_refuses_invalid_matrix_row(self) -> None:
        with self.assertRaisesRegex(BenchmarkIntegrityError, "invalid matrix row"):
            build_baseline({1: {}}, required_workers=(1,))

    def test_committed_baseline_does_not_claim_pass_without_durable_evidence(self) -> None:
        baseline_path = ROOT / "docs" / "m4" / "m4_baseline.json"
        manifest_path = ROOT / "docs" / "m4" / "m4_acceptance_manifest.json"
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
        status = baseline.get("status")
        if status == "M4 BASELINE ACCEPTANCE PENDING":
            self.assertIsNone(baseline.get("acceptance_evidence"))
            self.assertFalse(manifest_path.exists())
            return

        self.assertTrue(
            isinstance(status, str) and status.startswith("M4 BASELINE PASS"),
            f"unexpected committed M4 baseline status: {status!r}",
        )
        evidence = baseline.get("acceptance_evidence")
        self.assertIsInstance(evidence, dict)
        assert isinstance(evidence, dict)
        self.assertTrue(manifest_path.is_file())
        manifest_bytes = manifest_path.read_bytes()
        manifest = json.loads(manifest_bytes.decode("utf-8"))
        self.assertEqual(manifest.get("manifest_sha256"), evidence.get("manifest_sha256"))
        unsigned_manifest = dict(manifest)
        unsigned_manifest["manifest_sha256"] = ""
        self.assertEqual(
            hashlib.sha256(
                json.dumps(unsigned_manifest, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode("utf-8")
            ).hexdigest(),
            evidence.get("manifest_sha256"),
        )
        gates = evidence.get("gates")
        self.assertIsInstance(gates, dict)
        assert isinstance(gates, dict)
        for gate_name, gate in gates.items():
            with self.subTest(gate=gate_name):
                self.assertIsInstance(gate, dict)
                assert isinstance(gate, dict)
                artifacts = gate.get("artifacts")
                self.assertIsInstance(artifacts, list)
                assert isinstance(artifacts, list)
                for artifact in artifacts:
                    self.assertIsInstance(artifact, dict)
                    assert isinstance(artifact, dict)
                    artifact_path = ROOT / str(artifact.get("path", ""))
                    self.assertTrue(artifact_path.is_file())
                    self.assertEqual(
                        hashlib.sha256(artifact_path.read_bytes()).hexdigest(),
                        artifact.get("sha256"),
                    )

    def test_baseline_requires_schema_valid_rows_and_fresh_acceptance_evidence(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-baseline-fixture-") as directory:
            stderr_path = Path(directory) / "worker.stderr.log"
            stderr_path.touch()
            report = valid_matrix_report(str(stderr_path))
            baseline = build_baseline({1: report}, required_workers=(1,), optional_workers=())
            self.assertEqual(baseline["status"], "M4 BASELINE ACCEPTANCE PENDING")
            self.assertEqual(baseline["evidence_identity"], "UNVERIFIED")
            self.assertIn("dispatch_to_receipt", baseline["scaling"][0]["timing_percentages"])
            evidence = {
                gate: {"status": "PASS", "fresh": True, "evidence": "test evidence"}
                for gate in (
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
            }
            accepted = build_baseline(
                {1: report},
                required_workers=(1,),
                optional_workers=(),
                acceptance_evidence=evidence,
            )
            self.assertEqual(accepted["status"], "M4 BASELINE ACCEPTANCE PENDING")
            self.assertIn("acceptance evidence", accepted["status_reason"])

    def test_baseline_rejects_row_that_declared_schema_rejects(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-baseline-fixture-") as directory:
            stderr_path = Path(directory) / "worker.stderr.log"
            stderr_path.touch()
            invalid = valid_matrix_report(str(stderr_path))
            invalid["jobs"][0].pop("schema")  # type: ignore[index]
            with self.assertRaisesRegex(BenchmarkIntegrityError, "invalid matrix row"):
                build_baseline({1: invalid}, required_workers=(1,), optional_workers=())

    def test_hashed_acceptance_manifest_rejects_in_memory_proof_mutation(self) -> None:
        manifest_path = ROOT / "docs" / "m4" / "m4_acceptance_manifest.json"
        if not manifest_path.exists():
            baseline = json.loads((ROOT / "docs" / "m4" / "m4_baseline.json").read_text(encoding="utf-8"))
            self.assertEqual(baseline["status"], "M4 BASELINE ACCEPTANCE PENDING")
            self.assertIsNone(baseline.get("acceptance_evidence"))
            return
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        rows = {
            workers: ROOT / "artifacts" / "m4" / "matrix" / f"throughput-w{workers}.json"
            for workers in (1, 2, 4, 8, 16, 32, 64)
        }
        if not all(path.exists() for path in rows.values()):
            self.fail("committed acceptance manifest requires all hashed matrix artifacts")
        skipped = {
            128: "NOT_RUN - measured 64-worker throughput did not improve over 32 workers; no measured usefulness for another oversubscribed row"
        }
        accepted = build_baseline(rows, skipped_rows=skipped, acceptance_evidence=manifest)
        self.assertTrue(accepted["status"].startswith("M4 BASELINE PASS"))
        mutated = copy.deepcopy(manifest)
        mutated["gates"]["mode_equivalence"]["verification"]["semantic_match"] = False
        rejected = build_baseline(rows, skipped_rows=skipped, acceptance_evidence=mutated)
        self.assertEqual(rejected["status"], "M4 BASELINE ACCEPTANCE PENDING")

    def test_hardware_metadata_reports_physical_memory_on_windows(self) -> None:
        value = default_hardware_metadata()["physical_memory_bytes"]
        if os.name == "nt":
            self.assertIsInstance(value, int)
            self.assertGreater(value, 0)
        else:
            self.assertIn(value, {"NOT_MEASURED"})

    def test_schema_declares_version_and_required_top_level_contract(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(schema["$id"], REPORT_SCHEMA_VERSION)
        self.assertEqual(
            set(schema["required"]),
            {
                "schema_version",
                "canonical_environment",
                "hardware",
                "build",
                "warmup_policy",
                "mode",
                "observation_mode",
                "instrumentation",
                "trace_persistence",
                "games_requested",
                "workers_requested",
                "cold_start",
                "steady_state",
                "jobs",
            },
        )
        self.assertIn("simulation_elapsed_us", schema["$defs"]["steady_state"]["required"])
        self.assertIn("operation_counters", schema["$defs"]["steady_state"]["required"])
        self.assertIn("dispatch_to_receipt", schema["$defs"]["timing_buckets"]["required"])
        self.assertNotIn("coordinator_ipc", schema["$defs"]["timing_buckets"]["required"])

    def test_generated_report_is_accepted_by_the_declared_json_schema(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        result = valid_result("m4-000000")
        steady_state = aggregate_results(
            [result],
            valid_metadata(),
            wall_clock_seconds=1.0,
            games_requested=1,
            workers_requested=1,
        )
        report = build_report(
            canonical_environment={
                "format_id": "TCG_ADVANCED_2026_05_18",
                "duel_mode_name": "DUEL_MODE_MR5",
                "duel_flags": 190464,
                "rules_bundle_id": "a" * 64,
                "core_patchset_sha256": "b" * 64,
                "deck_hashes": ["c" * 64, "d" * 64],
            },
            hardware=default_hardware_metadata(),
            build={
                "compiler_identity": "test",
                "build_type": "Test",
                "worker_identity": "ocgforge.m4.native_worker.v1",
                "protocol_schema": "ocgforge.m4.worker.v1",
                "protocol_version": "ocgforge.m4.worker.v1",
                "worker_executable": "worker.exe",
                "worker_count": 1,
                "result_timeout_seconds": 120.0,
                "platform": "test",
            },
            warmup_policy={
                "warmup_games": 0,
                "warmup_indices": {"start": 0, "stop": 0, "count": 0},
                "steady_state_indices": {"start": 0, "stop": 1, "count": 1},
                "master_seed": 20260815,
                "same_master_seed_for_warmup_and_steady": True,
                "starting_player_mode": "balanced",
                "seat_mode": "balanced",
                "steady_timer_start": "after_warmup_completion",
                "steady_timer_stop": "after_final_result_publication",
            },
            mode="throughput",
            games_requested=1,
            workers_requested=1,
            cold_start={
                "wall_clock_seconds": 0.1,
                "ready_workers": 1,
                "worker_count": 1,
                "process_ready_time_domain": "test",
            },
            steady_state=steady_state,
            jobs=[result],
        )
        errors = sorted(Draft202012Validator(schema).iter_errors(report), key=str)
        self.assertEqual(errors, [])

    def test_schema_rejects_job_missing_required_gameplay_hash(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        result = valid_result("m4-000000")
        result.pop("gameplay_hash")
        steady_state = aggregate_results(
            [valid_result("m4-000000")],
            valid_metadata(),
            wall_clock_seconds=1.0,
            games_requested=1,
            workers_requested=1,
        )
        report = build_report(
            canonical_environment={},
            hardware=default_hardware_metadata(),
            build={},
            warmup_policy={},
            mode="throughput",
            games_requested=1,
            workers_requested=1,
            cold_start={},
            steady_state=steady_state,
            jobs=[result],
        )
        errors = list(Draft202012Validator(schema).iter_errors(report))
        self.assertTrue(any("gameplay_hash" in error.message for error in errors))

    def test_cli_wall_clock_stops_at_final_publication_before_integrity_barrier(self) -> None:
        fake_worker = ROOT / "tests" / "m4" / "fake_worker_crash.py"
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task6-timer-") as directory:
            pool = PersistentWorkerPool(
                [sys.executable, str(fake_worker), "--behavior", "normal"],
                worker_count=1,
                output_dir=Path(directory),
            )
            started = time.perf_counter_ns()
            with pool:
                results = pool.run(
                    [valid_result_job()],
                    require_primary_integrity=True,
                )
                finished = time.perf_counter_ns()
                publication = pool.last_publication_ns
            self.assertEqual(len(results), 1)
            self.assertIsNotNone(publication)
            assert publication is not None
            self.assertLess(publication, finished)
            self.assertGreaterEqual(finished - publication, 200_000_000)
            self.assertGreater(publication, started)

    def test_percentiles_use_the_specified_ceiling_index(self) -> None:
        summary = percentile_summary([10, 20, 30, 40])
        self.assertEqual(summary["sample_count"], 4)
        self.assertEqual(summary["p50"], 20)
        self.assertEqual(summary["p95"], 40)
        self.assertEqual(summary["p99"], 40)
        self.assertEqual(summary["mean"], 25)

    def test_integrity_rejects_each_native_and_coordinator_error(self) -> None:
        native_keys = (
            "retries",
            "unsupported",
            "automatic",
            "truncated",
            "core_errors",
            "worker_errors",
        )
        coordinator_keys = (
            "retries",
            "handshake",
            "malformed_protocol",
            "failed_games",
            "worker_crashes",
            "worker_restarts",
        )
        for key in native_keys:
            with self.subTest(counter=f"native.{key}"):
                result = valid_result("m4-000000")
                result["errors"][key] = 1  # type: ignore[index]
                with self.assertRaises(BenchmarkIntegrityError):
                    validate_complete_results([result], ["m4-000000"], valid_metadata())
        for key in coordinator_keys:
            with self.subTest(counter=f"coordinator.{key}"):
                result = valid_result("m4-000000")
                result["coordinator_errors"][key] = 1  # type: ignore[index]
                with self.assertRaises(BenchmarkIntegrityError):
                    validate_complete_results([result], ["m4-000000"], valid_metadata())

    def test_integrity_rejects_failed_nonterminal_and_metadata_failures(self) -> None:
        for mutation in (
            lambda result: result.update({"status": "failed", "terminal": False}),
            lambda result: result.update({"terminal": False}),
        ):
            result = valid_result("m4-000000")
            mutation(result)
            with self.assertRaises(BenchmarkIntegrityError):
                validate_complete_results([result], ["m4-000000"], valid_metadata())
        metadata = valid_metadata()
        metadata["handshake_errors"] = 1
        with self.assertRaises(BenchmarkIntegrityError):
            validate_complete_results([valid_result("m4-000000")], ["m4-000000"], metadata)

    def test_integrity_rejects_duplicate_missing_unexpected_and_mismatched_hashes(self) -> None:
        with self.assertRaises(BenchmarkIntegrityError):
            validate_complete_results(
                [valid_result("m4-000000"), valid_result("m4-000000")],
                ["m4-000000", "m4-000001"],
                valid_metadata(),
            )
        with self.assertRaises(BenchmarkIntegrityError):
            validate_complete_results([valid_result("m4-000000")], ["m4-000000", "m4-000001"], valid_metadata())
        with self.assertRaises(BenchmarkIntegrityError):
            validate_complete_results([valid_result("m4-000002")], ["m4-000000"], valid_metadata())
        with self.assertRaises(BenchmarkIntegrityError):
            validate_complete_results(
                [valid_result("m4-000000", gameplay_hash="b" * 64)],
                ["m4-000000"],
                valid_metadata(),
                expected_gameplay_hashes={"m4-000000": "a" * 64},
            )

    def test_aggregation_sums_native_fields_and_keeps_coordinator_domain_separate(self) -> None:
        results = [valid_result("m4-000000"), valid_result("m4-000001")]
        aggregate = aggregate_results(
            results,
            valid_metadata(),
            wall_clock_seconds=2.0,
            games_requested=2,
            workers_requested=2,
        )
        self.assertEqual(aggregate["games_completed"], 2)
        self.assertEqual(aggregate["engine_steps_total"], 20)
        self.assertEqual(aggregate["games_per_second"], 1.0)
        self.assertEqual(aggregate["simulation_elapsed_us"]["p50"], 10)
        self.assertEqual(aggregate["coordinator_elapsed_us"]["p50"], 20)
        self.assertEqual(aggregate["timing_buckets_us"]["core_process"], 2)
        self.assertEqual(aggregate["timing_buckets_us"]["dispatch_to_receipt"], 40)
        self.assertEqual(aggregate["operation_counters"]["ocg_duel_process"], 4)
        self.assertNotIn("wall_clock_subtraction", aggregate["timing_buckets_us"])

    def test_argument_parser_supports_the_exact_task6_flags(self) -> None:
        parser = build_argument_parser()
        args = parser.parse_args(
            [
                "--worker-executable",
                "worker.exe",
                "--games",
                "8",
                "--workers",
                "2",
                "--master-seed",
                "20260815",
                "--mode",
                "throughput",
                "--warmup-games",
                "2",
                "--output",
                "out.json",
                "--starting-player-mode",
                "balanced",
                "--seat-mode",
                "balanced",
                "--instrument",
                "--observation-mode",
                "off-diagnostic",
                "--trace-persistence",
                "off",
            ]
        )
        self.assertEqual(args.games, 8)
        self.assertEqual(args.observation_mode, "off_diagnostic")
        self.assertFalse(args.trace_persistence)

    def test_argument_parser_accepts_result_timeout_seconds(self) -> None:
        parser = build_argument_parser()
        args = parser.parse_args(
            [
                "--worker-executable",
                "worker.exe",
                "--games",
                "64",
                "--workers",
                "32",
                "--master-seed",
                "20260815",
                "--mode",
                "throughput",
                "--output",
                "out.json",
                "--result-timeout-seconds",
                "300",
            ]
        )
        self.assertEqual(args.result_timeout_seconds, 300.0)

    def test_off_diagnostic_report_is_labeled_as_non_training(self) -> None:
        result = valid_result("m4-000000")
        report = build_report(
            canonical_environment={},
            hardware={},
            build={},
            warmup_policy={},
            mode="throughput",
            observation_mode="off_diagnostic",
            games_requested=1,
            workers_requested=1,
            cold_start={},
            steady_state={},
            jobs=[result],
        )
        self.assertEqual(report["observation_mode"], "off_diagnostic")
        self.assertEqual(
            report["classification_label"],
            "DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT",
        )


def valid_result_job() -> dict[str, object]:
    return {
        "job_id": "m4-000000",
        "seed": 1,
        "seat_assignment": "normal",
        "starting_player": 0,
        "max_steps": 2200,
        "canonical_rules_id": "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
        "mode": "throughput",
        "observation_mode": "full",
        "instrumentation": False,
        "persist_trace": False,
    }


if __name__ == "__main__":
    unittest.main()
