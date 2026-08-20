from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.m4.performance_audit import (
    AUDIT_SIDECAR_PREFIX,
    AUXILIARY_TIMING_KEYS,
    ENTITY_ZONE_KEYS,
    OFF_DIAGNOSTIC_LABEL,
    OBSERVATION_DETAIL_COUNTER_KEYS,
    SETUP_TIMING_KEYS,
    AuditWorkerPool,
    CoordinatorTimingError,
    CoordinatorTimingSnapshot,
    PerformanceAuditSidecarError,
    _CumulativeNonblockingTimer,
    _CoordinatorTimingAccumulator,
    aggregate_coordinator_timing,
    build_audit_jobs,
    parse_audit_sidecar_line,
    parse_audit_sidecars,
)
from tools.m4.benchmark import PersistentWorkerPool
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
    detail_counters = {key: 0 for key in OBSERVATION_DETAIL_COUNTER_KEYS}
    detail_counters["query_decode_calls"] = 1
    setup_timing = {key: _timing_group(0, 0) for key in SETUP_TIMING_KEYS}
    auxiliary_timing = {key: _timing_group(0, 0) for key in AUXILIARY_TIMING_KEYS}
    entities_by_zone = {
        zone: {"entities_projected": 0, "identity_known": 0, "redacted": 0}
        for zone in ENTITY_ZONE_KEYS
    }
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
                "observation_detail_counters": detail_counters,
                "setup_timing_us": setup_timing,
                "auxiliary_timing_us": auxiliary_timing,
                "entities_by_zone": entities_by_zone,
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

    def test_sidecar_requires_all_native_detail_groups_and_rejects_shape_drift(self) -> None:
        payload = json.loads(_sidecar()[len(AUDIT_SIDECAR_PREFIX) :])
        required_groups = (
            "observation_detail_counters",
            "setup_timing_us",
            "auxiliary_timing_us",
            "entities_by_zone",
        )
        for group in required_groups:
            candidate = json.loads(json.dumps(payload))
            del candidate[group]
            with self.subTest(group=group, shape="missing"):
                with self.assertRaisesRegex(PerformanceAuditSidecarError, group):
                    parse_audit_sidecar_line(
                        AUDIT_SIDECAR_PREFIX + json.dumps(candidate, separators=(",", ":"))
                    )

        for group, key in (
            ("observation_detail_counters", OBSERVATION_DETAIL_COUNTER_KEYS[0]),
            ("setup_timing_us", SETUP_TIMING_KEYS[0]),
            ("auxiliary_timing_us", AUXILIARY_TIMING_KEYS[0]),
            ("entities_by_zone", ENTITY_ZONE_KEYS[0]),
        ):
            candidate = json.loads(json.dumps(payload))
            if group == "entities_by_zone":
                del candidate[group][key]
            else:
                del candidate[group][key]
            with self.subTest(group=group, shape="missing nested key"):
                with self.assertRaisesRegex(PerformanceAuditSidecarError, group):
                    parse_audit_sidecar_line(
                        AUDIT_SIDECAR_PREFIX + json.dumps(candidate, separators=(",", ":"))
                    )

        candidate = json.loads(json.dumps(payload))
        candidate["observation_detail_counters"]["unexpected"] = 0
        with self.assertRaisesRegex(PerformanceAuditSidecarError, "observation_detail_counters"):
            parse_audit_sidecar_line(
                AUDIT_SIDECAR_PREFIX + json.dumps(candidate, separators=(",", ":"))
            )

        candidate = json.loads(json.dumps(payload))
        candidate["unexpected_top_level"] = 0
        with self.assertRaisesRegex(PerformanceAuditSidecarError, "top-level"):
            parse_audit_sidecar_line(
                AUDIT_SIDECAR_PREFIX + json.dumps(candidate, separators=(",", ":"))
            )

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

    def test_lifecycle_barriers_are_excluded_from_wait_and_sample_clock(self) -> None:
        accumulator = _CoordinatorTimingAccumulator()
        accumulator.add("worker_wait_ns", 7_000)
        accumulator.add_lifecycle_time(wall_clock_ns=90_000, cpu_ns=4_000)
        accumulator.finish(wall_clock_ns=100_000, main_cpu_ns=12_000)
        snapshot = accumulator.snapshot()

        self.assertEqual(snapshot.worker_compute_wait_ns, 7_000)
        self.assertEqual(snapshot.wall_clock_ns, 10_000)
        self.assertEqual(snapshot.coordinator_cpu_ns, 8_000)
        self.assertIs(
            AuditWorkerPool._settle_staged_result,
            PersistentWorkerPool._settle_staged_result,
        )
        self.assertIs(
            AuditWorkerPool._settle_completed_run,
            PersistentWorkerPool._settle_completed_run,
        )

    def test_cpu_domain_accounting_excludes_worker_wait(self) -> None:
        accumulator = _CoordinatorTimingAccumulator()
        accumulator.add("reader_cpu_ns", 1_000)
        accumulator.add("pipe_write_cpu_ns", 500)
        accumulator.add("json_cpu_ns", 700)
        accumulator.add("dispatch_queue_cpu_ns", 800)
        accumulator.add("worker_wait_ns", 50_000)
        accumulator.finish(wall_clock_ns=60_000, main_cpu_ns=4_000)
        snapshot = accumulator.snapshot()

        self.assertEqual(snapshot.coordinator_cpu_ns, 5_000)
        self.assertEqual(snapshot.pipe_read_write_cpu_ns, 1_500)
        self.assertEqual(snapshot.json_encode_decode_cpu_ns, 700)
        self.assertEqual(snapshot.dispatch_queue_overhead_ns, 800)
        self.assertEqual(snapshot.other_cpu_ns, 2_000)
        self.assertEqual(
            snapshot.pipe_read_write_cpu_ns
            + snapshot.json_encode_decode_cpu_ns
            + snapshot.dispatch_queue_overhead_ns
            + snapshot.other_cpu_ns,
            snapshot.coordinator_cpu_ns,
        )
        self.assertNotIn(
            snapshot.worker_compute_wait_ns,
            {
                snapshot.pipe_read_write_cpu_ns,
                snapshot.json_encode_decode_cpu_ns,
                snapshot.dispatch_queue_overhead_ns,
                snapshot.other_cpu_ns,
            },
        )

    def test_short_json_proxy_is_capped_at_available_cpu_residual(self) -> None:
        accumulator = _CoordinatorTimingAccumulator()
        accumulator.add("json_cpu_ns", 700)
        accumulator.add("dispatch_queue_cpu_ns", 5_000)
        accumulator.finish(wall_clock_ns=60_000, main_cpu_ns=5_000)

        snapshot = accumulator.snapshot()

        self.assertEqual(snapshot.coordinator_cpu_ns, 5_000)
        self.assertEqual(snapshot.dispatch_queue_overhead_ns, 5_000)
        self.assertEqual(snapshot.json_encode_decode_cpu_ns, 0)
        self.assertEqual(snapshot.other_cpu_ns, 0)
        self.assertLessEqual(
            snapshot.pipe_read_write_cpu_ns
            + snapshot.json_encode_decode_cpu_ns
            + snapshot.dispatch_queue_overhead_ns
            + snapshot.other_cpu_ns,
            snapshot.coordinator_cpu_ns,
        )

    def test_nonblocking_short_operations_accumulate_with_perf_counter_window(self) -> None:
        class FakeClock:
            def __init__(self) -> None:
                self.values = iter((100, 101, 101, 104))

            def __call__(self) -> int:
                return next(self.values)

        timer = _CumulativeNonblockingTimer(clock=FakeClock())
        timer.measure(lambda: None)
        timer.measure(lambda: None)

        self.assertEqual(timer.calls, 2)
        self.assertEqual(timer.total_ns, 4)

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
