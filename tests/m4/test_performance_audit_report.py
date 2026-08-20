from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from jsonschema import Draft202012Validator

from tools.m4.performance_audit import (
    AUDIT_SIDECAR_PREFIX,
    OFF_DIAGNOSTIC_LABEL,
    PerformanceAuditReportError,
    build_performance_audit_report,
    render_performance_audit_markdown,
    validate_performance_audit_artifact,
)
from tools.m4.performance_audit_contract import (
    AUXILIARY_TIMING_KEYS,
    ENTITY_ZONE_KEYS,
    OBSERVATION_COUNTER_KEYS,
    OBSERVATION_DETAIL_COUNTER_KEYS,
    OBSERVATION_TIMING_KEYS,
    SETUP_TIMING_KEYS,
)
from tools.m4.worker_protocol_contract import (
    CANONICAL_DECK_HASHES,
    CANONICAL_PATCHSET_SHA256,
    CANONICAL_RULES_BUNDLE_ID,
    PROTOCOL_SCHEMA,
    WORKER_IDENTITY,
)


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "docs" / "m4" / "m4_performance_audit_schema.json"


def _timing(total_us: int, calls: int) -> dict[str, int]:
    return {
        "total_us": total_us,
        "calls": calls,
        "mean_us_per_call": 0 if calls == 0 else total_us // calls,
    }


def _ready(pid: int) -> dict[str, object]:
    return {
        "schema": "ocgforge.m4.worker.v1",
        "type": "ready",
        "protocol_version": PROTOCOL_SCHEMA,
        "pid": pid,
        "rules_bundle_id": CANONICAL_RULES_BUNDLE_ID,
        "core_patchset_sha256": CANONICAL_PATCHSET_SHA256,
        "deck_hashes": list(CANONICAL_DECK_HASHES),
        "format_id": "TCG_ADVANCED_2026_05_18",
        "duel_mode_name": "DUEL_MODE_MR5",
        "duel_flags": 190464,
        "compiler_identity": "test-compiler",
        "build_type": "Debug",
        "worker_identity": WORKER_IDENTITY,
    }


def _result(
    job_id: str,
    *,
    observation_mode: str,
    gameplay_hash: str = "d" * 64,
) -> dict[str, object]:
    is_full = observation_mode == "full"
    observations = 2 if is_full else 0
    query_field = 4 if is_full else 2
    query_location = 24 if is_full else 0
    entities = 4 if is_full else 0
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
        "simulation_elapsed_us": 1000,
        "coordinator_elapsed_us": 10,
        "errors": {
            "retries": 0,
            "unsupported": 0,
            "automatic": 0,
            "truncated": 0,
            "core_errors": 0,
            "worker_errors": 0,
        },
        "timing_us": {
            "core_process": 100,
            "protocol_candidate": 2,
            "continuation": 0,
            "observation": 800 if is_full else 0,
            "trace_hash": 10,
            "serialization": 0,
            "other": 88 if is_full else 888,
            "trace_persistence": 0,
        },
        "counters": {
            "ocg_duel_process": 2,
            "ocg_duel_query": 0,
            "ocg_duel_query_location": query_location,
            "ocg_duel_query_field": query_field,
            "ocg_duel_query_count": 0,
            "script_reader_requests": 2,
            "script_loads": 2,
            "observations": observations,
            "entities_projected": entities,
            "candidate_sets": 2,
            "candidate_total": 4,
            "candidate_max": 2,
            "semantic_hashes": 1,
            "trace_bytes_serialized": 0,
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
            "stderr_path": "audit-worker.stderr.log",
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


def _sidecar(job_id: str, *, observation_mode: str) -> str:
    is_full = observation_mode == "full"
    observations = 2 if is_full else 0
    observation_total = 100 if is_full else 0
    timing_totals = {key: 0 for key in OBSERVATION_TIMING_KEYS}
    if is_full:
        timing_totals.update(
            {
                "observation_query_field": 10,
                "observation_query_location": 20,
                "observation_query_individual": 0,
                "observation_query_decode": 5,
                "observation_zone_projection": 5,
                "observation_entity_projection": 10,
                "observation_relationship_projection": 5,
                "observation_visibility_privacy": 5,
                "observation_candidate_consistency": 5,
                "observation_canonical_serialization": 10,
                "observation_hash": 25,
                "observation_other": 0,
            }
        )
    timing = {key: _timing(value, 0 if value == 0 else 1) for key, value in timing_totals.items()}
    if is_full:
        timing["observation_query_field"]["calls"] = 2
        timing["observation_query_field"]["mean_us_per_call"] = 5
        timing["observation_query_location"]["calls"] = 24
        timing["observation_query_location"]["mean_us_per_call"] = 20 // 24
    counters = {key: 0 for key in OBSERVATION_COUNTER_KEYS}
    counters.update(
        {
            "observations": observations,
            "query_field_calls": 4 if is_full else 2,
            "query_location_calls": 24 if is_full else 0,
            "query_individual_calls": 0,
            "entities_projected": 4 if is_full else 0,
            "identity_known_entities": 2 if is_full else 0,
            "redacted_entities": 2 if is_full else 0,
            "static_card_data_lookups": 2 if is_full else 0,
            "current_property_projections": 4 if is_full else 0,
            "relationship_objects": 1 if is_full else 0,
            "allocation_copy_events": 3 if is_full else 0,
            "script_loads": 2,
        }
    )
    detail = {key: 0 for key in OBSERVATION_DETAIL_COUNTER_KEYS}
    detail.update(
        {
            "query_decode_calls": 5 if is_full else 0,
            "zone_projection_calls": 5 if is_full else 0,
            "relationship_resolution_events": 1 if is_full else 0,
            "observation_query_field_calls": 2 if is_full else 0,
            "public_state_hash_query_field_calls": 2,
            "script_reader_requests": 2,
        }
    )
    setup_keys = tuple(SETUP_TIMING_KEYS) + ("script_load",)
    setup = {key: _timing(0, 0) for key in setup_keys}
    setup["core_host_setup"] = _timing(20, 1)
    setup["script_load"] = _timing(4, 2)
    auxiliary = {key: _timing(0, 0) for key in AUXILIARY_TIMING_KEYS}
    auxiliary["public_state_hash"] = _timing(10, 2)
    auxiliary["public_state_hash_query_field"] = _timing(2, 2)
    zones = {
        zone: {"entities_projected": 0, "identity_known": 0, "redacted": 0}
        for zone in ENTITY_ZONE_KEYS
    }
    if is_full:
        zones["HAND"] = {"entities_projected": 2, "identity_known": 1, "redacted": 1}
        zones["MONSTER_ZONE"] = {"entities_projected": 2, "identity_known": 1, "redacted": 1}
    return AUDIT_SIDECAR_PREFIX + json.dumps(
        {
            "schema": "ocgforge.m4.performance_audit.v1",
            "type": "performance_audit",
            "job_id": job_id,
            "observation_total_us": observation_total,
            "observation_timing_us": timing,
            "observation_counters": counters,
            "observation_detail_counters": detail,
            "setup_timing_us": setup,
            "auxiliary_timing_us": auxiliary,
            "entities_by_zone": zones,
        },
        separators=(",", ":"),
    )


def _sample(*, observation_mode: str, seed: int = 1234) -> dict[str, object]:
    from tools.m4.performance_audit import parse_audit_sidecar_line
    from tools.m4.performance_audit import build_audit_jobs

    jobs = build_audit_jobs(seed, 2, max_steps=10, observation_mode=observation_mode)
    results = [
        _result(str(job["job_id"]), observation_mode=observation_mode)
        for job in jobs
    ]
    sidecars = {
        str(job["job_id"]): parse_audit_sidecar_line(
            _sidecar(str(job["job_id"]), observation_mode=observation_mode),
            expected_job_id=str(job["job_id"]),
        )
        for job in jobs
    }
    return {
        "jobs": jobs,
        "results": results,
        "ready_messages": [_ready(100)],
        "metadata": {
            "worker_count": 1,
            "worker_crashes": 0,
            "worker_restarts": 0,
            "retries": 0,
            "handshake_errors": 0,
            "malformed_protocol": 0,
            "failed_games": 0,
            "worker_errors": 0,
            "workers": [{"stderr_path": "audit-worker.stderr.log"}],
        },
        "sidecars": sidecars,
        "coordinator_timing": {
            "wall_clock_us": 2000,
            "coordinator_cpu_us": 100,
            "coordinator_timing_us": {
                "worker_compute_wait": 1500,
                "pipe_read_write_cpu": 10,
                "json_encode_decode_cpu": 20,
                "dispatch_queue_overhead": 30,
                "other": 40,
            },
            "coordinator_timing_stats": {
                key: _timing(value, 1)
                for key, value in {
                    "worker_compute_wait": 1500,
                    "pipe_read_write_cpu": 10,
                    "json_encode_decode_cpu": 20,
                    "dispatch_queue_overhead": 30,
                    "other": 40,
                }.items()
            },
        },
        "observation_mode": observation_mode,
        "observation_mode_label": (
            OFF_DIAGNOSTIC_LABEL if observation_mode == "off_diagnostic" else "FULL"
        ),
        "training_throughput_eligible": observation_mode == "full",
    }


class PerformanceAuditReportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.full = _sample(observation_mode="full")
        self.off = _sample(observation_mode="off_diagnostic")

    def _build(self) -> dict[str, object]:
        return build_performance_audit_report(
            self.full,
            self.off,
            master_seed=1234,
            games=2,
            max_steps=10,
            worker_executable="audit-worker.exe",
        )

    def test_aggregate_arithmetic_and_coordinator_domains(self) -> None:
        report = self._build()
        sample = report["samples"]["full"]  # type: ignore[index]
        self.assertEqual(sample["timing_us"]["outer_observation"]["total_us"], 200)  # type: ignore[index]
        self.assertEqual(
            sample["timing_us"]["observation"]["observation_query_field"]["mean_us_per_call"], 5,  # type: ignore[index]
        )
        self.assertEqual(sample["counters"]["observation"]["query_location_calls"], 48)  # type: ignore[index]
        self.assertEqual(sample["timing_us"]["coordinator"]["worker_compute_wait"]["total_us"], 1500)  # type: ignore[index]
        self.assertEqual(sample["timing_us"]["coordinator_cpu_us"], 100)  # type: ignore[index]
        self.assertEqual(sample["timing_us"]["coordinator_cpu_domain_total_us"], 100)  # type: ignore[index]

    def test_report_and_schema_validate(self) -> None:
        report = self._build()
        validate_performance_audit_artifact(report)
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(list(Draft202012Validator(schema).iter_errors(report)), [])
        self.assertEqual(report["status"], "M4.2 PERFORMANCE AUDIT PASS")
        self.assertFalse(report["optimization_implemented"])
        self.assertFalse(report["begin_m5"])

    def test_candidate_ranking_includes_largest_hash_bucket(self) -> None:
        report = self._build()

        candidates = report["optimization_candidates"]  # type: ignore[index]
        self.assertEqual(candidates[0]["bucket"], "observation_hash")  # type: ignore[index]
        self.assertEqual(
            report["recommendation"]["first_m4_3_experiment"],  # type: ignore[index]
            candidates[0]["candidate"],  # type: ignore[index]
        )
        candidate_audit = report["candidate_audit"]  # type: ignore[index]
        self.assertEqual(candidate_audit["sample_candidate_max"], 4)  # type: ignore[index]
        self.assertNotIn("sample_candidate_max_sum", candidate_audit)

    def test_markdown_round_trip_is_deterministic_and_explains_baselines(self) -> None:
        report = self._build()
        reordered = json.loads(json.dumps(report, sort_keys=True))

        self.assertEqual(
            render_performance_audit_markdown(report),
            render_performance_audit_markdown(reordered),
        )
        markdown = render_performance_audit_markdown(report)
        self.assertIn("Baseline protocol_candidate fraction: 0.189337%", markdown)
        self.assertIn("script_load 8 us", markdown)

    def test_schema_rejects_extra_top_level_field(self) -> None:
        report = self._build()
        report["unexpected"] = True
        with self.assertRaises(PerformanceAuditReportError):
            validate_performance_audit_artifact(report)

    def test_timing_mutation_is_rejected(self) -> None:
        sample = copy.deepcopy(self.full)
        sample["sidecars"]["m4-000000"]["observation_timing_us"]["observation_other"]["total_us"] += 1  # type: ignore[index]
        with self.assertRaises(PerformanceAuditReportError):
            build_performance_audit_report(
                sample,
                self.off,
                master_seed=1234,
                games=2,
                max_steps=10,
                worker_executable="audit-worker.exe",
            )

    def test_counter_and_query_mutations_are_rejected(self) -> None:
        for mutation in ("counter", "query"):
            sample = copy.deepcopy(self.full)
            if mutation == "counter":
                sample["sidecars"]["m4-000000"]["observation_counters"]["query_location_calls"] += 1  # type: ignore[index]
            else:
                sample["sidecars"]["m4-000000"]["observation_detail_counters"]["public_state_hash_query_field_calls"] += 1  # type: ignore[index]
            with self.subTest(mutation=mutation):
                with self.assertRaises(PerformanceAuditReportError):
                    build_performance_audit_report(
                        sample,
                        self.off,
                        master_seed=1234,
                        games=2,
                        max_steps=10,
                        worker_executable="audit-worker.exe",
                    )

    def test_identity_privacy_and_off_label_mutations_are_rejected(self) -> None:
        identity = copy.deepcopy(self.off)
        identity["ready_messages"][0]["worker_identity"] = "different"  # type: ignore[index]
        privacy = copy.deepcopy(self.full)
        privacy["sidecars"]["m4-000000"]["entities_by_zone"]["HAND"]["redacted"] += 1  # type: ignore[index]
        label = copy.deepcopy(self.off)
        label["observation_mode_label"] = "TRAINING THROUGHPUT"  # type: ignore[index]
        for name, full, off in (
            ("identity", self.full, identity),
            ("privacy", privacy, self.off),
            ("off label", self.full, label),
        ):
            with self.subTest(mutation=name):
                with self.assertRaises(PerformanceAuditReportError):
                    build_performance_audit_report(
                        full,
                        off,
                        master_seed=1234,
                        games=2,
                        max_steps=10,
                        worker_executable="audit-worker.exe",
                    )

    def test_missing_native_sample_does_not_write_report(self) -> None:
        from tools.m4.performance_audit import run_and_write_performance_audit

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(Exception):
                run_and_write_performance_audit(
                    "missing-audit-worker.exe",
                    master_seed=1234,
                    games=1,
                    max_steps=10,
                    markdown_path=root / "M4_PERFORMANCE_AUDIT.md",
                    json_path=root / "m4_performance_audit.json",
                )
            self.assertFalse((root / "M4_PERFORMANCE_AUDIT.md").exists())
            self.assertFalse((root / "m4_performance_audit.json").exists())


if __name__ == "__main__":
    unittest.main()
