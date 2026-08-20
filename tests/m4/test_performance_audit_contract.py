from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.m4.benchmark import PersistentWorkerPool, _aggregate_audit_telemetry
from tools.m4.worker_protocol_contract import (
    COORDINATOR_TIMING_KEYS,
    OBSERVATION_COUNTER_KEYS,
    OBSERVATION_TIMING_KEYS,
    ProtocolContractError,
    default_audit_telemetry,
    validate_audit_telemetry,
    validate_result,
)


ROOT = Path(__file__).resolve().parents[2]


def result_fixture() -> dict[str, object]:
    return {
        "schema": "ocgforge.m4.worker.v1",
        "type": "result",
        "status": "passed",
        "job_id": "m4-000001",
        "terminal": True,
        "winner": 0,
        "win_reason": 1,
        "engine_steps": 10,
        "interactive_decisions": 2,
        "semantic_action_count": 2,
        "gameplay_hash": "a" * 64,
        "trace_hash": None,
        "simulation_elapsed_us": 100,
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
            "core_process": 10,
            "protocol_candidate": 20,
            "continuation": 0,
            "observation": 30,
            "trace_hash": 5,
            "serialization": 5,
            "other": 30,
            "trace_persistence": 0,
        },
        "counters": {
            "ocg_duel_process": 10,
            "ocg_duel_query": 0,
            "ocg_duel_query_location": 0,
            "ocg_duel_query_field": 0,
            "ocg_duel_query_count": 0,
            "script_reader_requests": 1,
            "script_loads": 1,
            "observations": 2,
            "entities_projected": 4,
            "candidate_sets": 2,
            "candidate_total": 4,
            "candidate_max": 2,
            "semantic_hashes": 1,
            "trace_bytes_serialized": 100,
        },
        "worker": {
            "pid": 1234,
            "restart_index": 0,
            "crashed": False,
            "restarted": False,
        },
        "failure_code": None,
        "error_message": None,
    }


class PerformanceAuditContractTests(unittest.TestCase):
    def test_audit_groups_have_exact_declared_key_sets(self) -> None:
        self.assertEqual(
            set(OBSERVATION_TIMING_KEYS),
            {
                "observation_query_field",
                "observation_query_location",
                "observation_query_individual",
                "observation_query_decode",
                "observation_zone_projection",
                "observation_entity_projection",
                "observation_relationship_projection",
                "observation_visibility_privacy",
                "observation_candidate_consistency",
                "observation_canonical_serialization",
                "observation_hash",
                "observation_other",
            },
        )
        self.assertEqual(
            set(OBSERVATION_COUNTER_KEYS),
            {
                "observations",
                "query_field_calls",
                "query_location_calls",
                "query_individual_calls",
                "entities_projected",
                "identity_known_entities",
                "redacted_entities",
                "static_card_data_lookups",
                "current_property_projections",
                "relationship_objects",
                "allocation_copy_events",
                "script_loads",
            },
        )
        self.assertEqual(
            set(COORDINATOR_TIMING_KEYS),
            {
                "worker_compute_wait",
                "pipe_read_write_cpu",
                "json_encode_decode_cpu",
                "dispatch_queue_overhead",
                "other",
            },
        )

    def test_audit_values_are_nonnegative_integers(self) -> None:
        telemetry = default_audit_telemetry()
        for group_name, keys in (
            ("observation_timing_us", OBSERVATION_TIMING_KEYS),
            ("observation_counters", OBSERVATION_COUNTER_KEYS),
            ("coordinator_timing_us", COORDINATOR_TIMING_KEYS),
        ):
            for key in keys:
                for invalid in (-1, True, 1.5):
                    candidate = default_audit_telemetry()
                    candidate[group_name][key] = invalid
                    with self.subTest(group=group_name, key=key, value=invalid):
                        with self.assertRaises(ProtocolContractError):
                            validate_audit_telemetry(candidate)

        malformed = default_audit_telemetry()
        malformed["observation_counters"] = None  # type: ignore[assignment]
        with self.assertRaises(ProtocolContractError):
            validate_audit_telemetry(malformed)

    def test_nested_observation_timing_cannot_exceed_outer_observation_bucket(self) -> None:
        message = result_fixture()
        message.update(default_audit_telemetry())
        message["observation_timing_us"]["observation_query_field"] = 31  # type: ignore[index]
        with self.assertRaisesRegex(ProtocolContractError, "observation timing"):
            validate_result(
                message,
                expected_job_id="m4-000001",
                require_audit_telemetry=True,
            )

    def test_old_result_without_audit_telemetry_remains_default_compatible(self) -> None:
        message = result_fixture()
        validate_result(message, expected_job_id="m4-000001")

        message.update(default_audit_telemetry())
        validate_result(
            message,
            expected_job_id="m4-000001",
            require_audit_telemetry=True,
        )

        fallback = PersistentWorkerPool._invalid_job_result(
            {"job_id": "m4-000002"}, "invalid test job"
        )
        self.assertEqual(fallback["observation_timing_us"], {key: 0 for key in OBSERVATION_TIMING_KEYS})
        self.assertEqual(fallback["observation_counters"], {key: 0 for key in OBSERVATION_COUNTER_KEYS})
        self.assertEqual(fallback["coordinator_timing_us"], {key: 0 for key in COORDINATOR_TIMING_KEYS})

    def test_missing_audit_telemetry_is_rejected_only_in_explicit_audit_mode(self) -> None:
        message = result_fixture()
        validate_result(message, expected_job_id="m4-000001")
        with self.assertRaisesRegex(ProtocolContractError, "audit telemetry"):
            validate_result(
                message,
                expected_job_id="m4-000001",
                require_audit_telemetry=True,
            )

    def test_declared_schema_keeps_audit_fields_optional(self) -> None:
        schema = json.loads(
            (ROOT / "docs" / "m4" / "m4_benchmark_schema.json").read_text(
                encoding="utf-8"
            )
        )
        job = schema["$defs"]["job"]
        self.assertNotIn("observation_timing_us", job["required"])
        self.assertNotIn("observation_counters", job["required"])
        self.assertNotIn("coordinator_timing_us", job["required"])
        for key in (
            "observation_timing_us",
            "observation_counters",
            "coordinator_timing_us",
        ):
            self.assertIn(key, job["properties"])

    def test_benchmark_aggregates_present_audit_telemetry_additively(self) -> None:
        first = result_fixture()
        first.update(default_audit_telemetry())
        first["observation_timing_us"]["observation_query_field"] = 4  # type: ignore[index]
        first["observation_counters"]["observations"] = 2  # type: ignore[index]
        first["coordinator_timing_us"]["worker_compute_wait"] = 7  # type: ignore[index]
        second = result_fixture()
        second.update(default_audit_telemetry())
        second["observation_timing_us"]["observation_query_field"] = 6  # type: ignore[index]
        second["observation_counters"]["observations"] = 3  # type: ignore[index]
        second["coordinator_timing_us"]["worker_compute_wait"] = 8  # type: ignore[index]

        aggregate = _aggregate_audit_telemetry([first, second])
        self.assertEqual(aggregate["observation_timing_us"]["observation_query_field"], 10)
        self.assertEqual(aggregate["observation_counters"]["observations"], 5)
        self.assertEqual(aggregate["coordinator_timing_us"]["worker_compute_wait"], 15)
        self.assertEqual(_aggregate_audit_telemetry([result_fixture()]), {})


if __name__ == "__main__":
    unittest.main()
