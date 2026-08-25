from __future__ import annotations

import unittest

from tools.m4.performance_audit_contract import (
    COORDINATOR_TIMING_KEYS,
    OBSERVATION_COUNTER_KEYS,
    OBSERVATION_TIMING_KEYS,
    PerformanceAuditContractError,
    default_audit_telemetry,
    validate_audit_telemetry,
)


def report_fixture() -> dict[str, object]:
    return {
        "timing_us": {
            "observation": 30,
        },
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
                        with self.assertRaises(PerformanceAuditContractError):
                            validate_audit_telemetry(candidate, require=True)

        malformed = default_audit_telemetry()
        malformed["observation_counters"] = None  # type: ignore[assignment]
        with self.assertRaises(PerformanceAuditContractError):
            validate_audit_telemetry(malformed, require=True)

    def test_audit_groups_reject_missing_or_extra_keys(self) -> None:
        for group_name, keys in (
            ("observation_timing_us", OBSERVATION_TIMING_KEYS),
            ("observation_counters", OBSERVATION_COUNTER_KEYS),
            ("coordinator_timing_us", COORDINATOR_TIMING_KEYS),
        ):
            missing = default_audit_telemetry()
            del missing[group_name][next(iter(keys))]
            with self.subTest(group=group_name, shape="missing"):
                with self.assertRaisesRegex(
                    PerformanceAuditContractError, "wrong keys"
                ):
                    validate_audit_telemetry(missing, require=True)

            extra = default_audit_telemetry()
            extra[group_name]["unexpected"] = 0
            with self.subTest(group=group_name, shape="extra"):
                with self.assertRaisesRegex(
                    PerformanceAuditContractError, "wrong keys"
                ):
                    validate_audit_telemetry(extra, require=True)

    def test_nested_observation_timing_cannot_exceed_outer_observation_timing(self) -> None:
        report = report_fixture()
        report.update(default_audit_telemetry())
        report["observation_timing_us"]["observation_query_field"] = 31  # type: ignore[index]
        with self.assertRaisesRegex(
            PerformanceAuditContractError, "nested observation timing"
        ):
            validate_audit_telemetry(report, require=True)

    def test_existing_report_without_audit_telemetry_remains_compatible(self) -> None:
        report = report_fixture()
        validate_audit_telemetry(report)

        report.update(default_audit_telemetry())
        validate_audit_telemetry(report, require=True)

    def test_missing_audit_telemetry_is_rejected_only_when_required(self) -> None:
        report = report_fixture()
        validate_audit_telemetry(report)
        with self.assertRaisesRegex(PerformanceAuditContractError, "audit telemetry"):
            validate_audit_telemetry(report, require=True)


if __name__ == "__main__":
    unittest.main()
