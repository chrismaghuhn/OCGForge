from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.m4.performance_audit import (
    AUDIT_SIDECAR_PREFIX,
    OFF_DIAGNOSTIC_LABEL,
    CoordinatorTimingError,
    CoordinatorTimingSnapshot,
    PerformanceAuditSidecarError,
    aggregate_coordinator_timing,
    build_audit_jobs,
    parse_audit_sidecar_line,
    parse_audit_sidecars,
)
from tools.m4.performance_audit_contract import OBSERVATION_COUNTER_KEYS, OBSERVATION_TIMING_KEYS


def _timing_group(total_us: int, calls: int) -> dict[str, int]:
    return {
        "total_us": total_us,
        "calls": calls,
        "mean_us_per_call": 0 if calls == 0 else total_us // calls,
    }


def _sidecar(job_id: str = "m4-000000") -> str:
    timing = {
        key: _timing_group(0, 0) for key in OBSERVATION_TIMING_KEYS
    }
    timing["observation_query_field"] = _timing_group(3, 1)
    timing["observation_other"] = _timing_group(9, 1)
    counters = {key: 0 for key in OBSERVATION_COUNTER_KEYS}
    counters["observations"] = 1
    counters["query_field_calls"] = 1
    return (
        AUDIT_SIDECAR_PREFIX
        + json.dumps(
            {
                "schema": "ocgforge.m4.performance_audit.v1",
                "type": "performance_audit",
                "job_id": job_id,
                "observation_total_us": 12,
                "observation_timing_us": timing,
                "observation_counters": counters,
                "observation_detail_counters": {"query_decode_calls": 1},
                "future_audit_field": {"kept": True},
            },
            separators=(",", ":"),
        )
    )


class PerformanceAuditRunnerTests(unittest.TestCase):
    def test_build_audit_jobs_preserves_deterministic_identity_for_modes(self) -> None:
        full = build_audit_jobs(0x1234, 3, max_steps=77, observation_mode="full")
        off = build_audit_jobs(0x1234, 3, max_steps=77, observation_mode="off_diagnostic")

        self.assertEqual([job["job_id"] for job in full], ["m4-000000", "m4-000001", "m4-000002"])
        self.assertEqual(
            [(job["seed"], job["seat_assignment"], job["starting_player"]) for job in full],
            [(job["seed"], job["seat_assignment"], job["starting_player"]) for job in off],
        )
        self.assertEqual([job["observation_mode"] for job in full], ["full"] * 3)
        self.assertEqual([job["observation_mode"] for job in off], ["off_diagnostic"] * 3)
        self.assertEqual(OFF_DIAGNOSTIC_LABEL, "DIAGNOSTIC ONLY — NOT TRAINING THROUGHPUT")

    def test_sidecar_requires_prefix_and_validates_existing_groups(self) -> None:
        record = parse_audit_sidecar_line(_sidecar() + "\n", expected_job_id="m4-000000")
        self.assertEqual(record["job_id"], "m4-000000")
        self.assertEqual(record["observation_timing_us"]["observation_query_field"]["calls"], 1)
        self.assertIn("future_audit_field", record)

        with self.assertRaises(PerformanceAuditSidecarError):
            parse_audit_sidecar_line(_sidecar()[len(AUDIT_SIDECAR_PREFIX) :])

    def test_sidecars_reject_missing_duplicate_and_malformed_records(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "worker-000.stderr.log"
            second = root / "worker-001.stderr.log"
            first.write_text(_sidecar(), encoding="utf-8")
            second.write_text("native diagnostic\n", encoding="utf-8")

            with self.assertRaisesRegex(PerformanceAuditSidecarError, "missing"):
                parse_audit_sidecars([first, second], ["m4-000000", "m4-000001"])

            second.write_text(_sidecar("m4-000000"), encoding="utf-8")
            with self.assertRaisesRegex(PerformanceAuditSidecarError, "duplicate"):
                parse_audit_sidecars([first, second], ["m4-000000"])

            second.write_text(AUDIT_SIDECAR_PREFIX + "not-json", encoding="utf-8")
            with self.assertRaisesRegex(PerformanceAuditSidecarError, "malformed"):
                parse_audit_sidecars([second], ["m4-000000"])

    def test_aggregate_coordinator_timing_keeps_wait_separate_from_cpu_domains(self) -> None:
        aggregate = aggregate_coordinator_timing(
            [
                CoordinatorTimingSnapshot(
                    wall_clock_ns=20_000,
                    coordinator_cpu_ns=5_000,
                    worker_compute_wait_ns=15_000,
                    pipe_read_write_cpu_ns=1_000,
                    json_encode_decode_cpu_ns=500,
                    dispatch_queue_overhead_ns=1_500,
                    other_cpu_ns=2_000,
                ),
                CoordinatorTimingSnapshot(
                    wall_clock_ns=10_000,
                    coordinator_cpu_ns=3_000,
                    worker_compute_wait_ns=7_000,
                    pipe_read_write_cpu_ns=500,
                    json_encode_decode_cpu_ns=500,
                    dispatch_queue_overhead_ns=500,
                    other_cpu_ns=1_500,
                ),
            ]
        )
        self.assertEqual(aggregate["samples"], 2)
        self.assertEqual(aggregate["coordinator_timing_us"], {
            "worker_compute_wait": 22,
            "pipe_read_write_cpu": 1,
            "json_encode_decode_cpu": 1,
            "dispatch_queue_overhead": 2,
            "other": 3,
        })
        self.assertEqual(aggregate["coordinator_timing_stats"]["worker_compute_wait"]["calls"], 2)

    def test_timing_snapshot_rejects_negative_and_boolean_values(self) -> None:
        with self.assertRaises(CoordinatorTimingError):
            CoordinatorTimingSnapshot(
                wall_clock_ns=-1,
                coordinator_cpu_ns=0,
                worker_compute_wait_ns=0,
                pipe_read_write_cpu_ns=0,
                json_encode_decode_cpu_ns=0,
                dispatch_queue_overhead_ns=0,
                other_cpu_ns=0,
            )
        with self.assertRaises(CoordinatorTimingError):
            CoordinatorTimingSnapshot(
                wall_clock_ns=True,
                coordinator_cpu_ns=0,
                worker_compute_wait_ns=0,
                pipe_read_write_cpu_ns=0,
                json_encode_decode_cpu_ns=0,
                dispatch_queue_overhead_ns=0,
                other_cpu_ns=0,
            )


if __name__ == "__main__":
    unittest.main()
