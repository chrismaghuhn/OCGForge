"""Regression tests for the fail-closed M4.3.5 audit harness."""

from __future__ import annotations

import unittest

from tools.m4.compare_build_modes import _compare
from tools.m4.run_m4_3_5_reserve_ab import _timing_decision


def _job(trace_hash: str) -> dict[str, object]:
    trace = {
        "observation_hashes": ["observation"],
        "steps": [],
        "normalized_sha256": "normalized",
        "step_bytes_sha256": "step-bytes",
        "sha256": "trace-bytes",
    }
    return {
        "job_id": "m4-000000",
        "result_semantics": {},
        "trace": trace,
        "trace_hash": trace_hash,
    }


class M435HarnessTests(unittest.TestCase):
    def test_raw_trace_hash_mismatch_is_not_accepted(self) -> None:
        comparison = _compare({"jobs": [_job("control")]}, {"jobs": [_job("experiment")]})
        self.assertFalse(comparison["raw_trace_hash_equal"])
        self.assertFalse(comparison["pass"])
        self.assertFalse(comparison["rows"][0]["pass"])

    def test_nonmaterial_timing_decision_is_rejected(self) -> None:
        repetitions = []
        for index, (control_worker, experiment_worker, control_serializer, experiment_serializer) in enumerate(
            ((100, 105, 100, 105), (101, 104, 101, 104), (102, 103, 102, 103)),
            start=1,
        ):
            repetitions.append({
                "label": f"A{index}",
                "variant": "control",
                "summary": {
                    "worker_local_simulation_us": control_worker,
                    "serializer_us": control_serializer,
                },
            })
            repetitions.append({
                "label": f"B{index}",
                "variant": "experiment",
                "summary": {
                    "worker_local_simulation_us": experiment_worker,
                    "serializer_us": experiment_serializer,
                },
            })
        decision = _timing_decision(repetitions)
        self.assertFalse(decision["pass"])
        self.assertLess(decision["worker_speedup_percent"], 0)
        self.assertLess(decision["serializer_speedup_percent"], 0)


if __name__ == "__main__":
    unittest.main()
