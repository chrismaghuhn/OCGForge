from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tests.m4.test_worker_integration import (
    make_jobs,
    run_pool_with_diagnostics,
    semantic_projection,
)


class HostedFastWorkerIntegrationTests(unittest.TestCase):
    """Bounded hosted-CI proof; the larger scaling test remains separate."""

    @staticmethod
    def _assert_clean_results(test_case: unittest.TestCase, results: list[dict[str, object]]) -> None:
        test_case.assertTrue(all(result["status"] == "passed" for result in results))
        test_case.assertTrue(all(result["terminal"] is True for result in results))
        test_case.assertTrue(
            all(not any(result["errors"].values()) for result in results)
        )

    def test_bounded_worker_semantic_and_trace_equivalence(self) -> None:
        throughput_jobs = make_jobs(2, mode="throughput")
        throughput_projection: list[tuple[object, ...]] | None = None
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-hosted-fast-throughput-") as directory:
            for worker_count in (1, 2):
                results = run_pool_with_diagnostics(
                    throughput_jobs,
                    worker_count=worker_count,
                    output_dir=Path(directory) / f"w{worker_count}",
                    test_name="hosted-fast",
                    phase="throughput",
                )
                self._assert_clean_results(self, results)
                projection = [semantic_projection(result) for result in results]
                if throughput_projection is None:
                    throughput_projection = projection
                self.assertEqual(projection, throughput_projection)
                self.assertTrue(all(result["trace_hash"] is None for result in results))

        assert throughput_projection is not None
        trace_jobs = make_jobs(2, mode="conformance", persist_trace=True)
        trace_hashes: list[str] | None = None
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-hosted-fast-trace-") as directory:
            for job in trace_jobs:
                job["trace_output"] = str(Path(directory) / f"{job['job_id']}.jsonl")
            for worker_count in (1, 2):
                results = run_pool_with_diagnostics(
                    trace_jobs,
                    worker_count=worker_count,
                    output_dir=Path(directory) / f"w{worker_count}",
                    test_name="hosted-fast",
                    phase="conformance",
                )
                self._assert_clean_results(self, results)
                self.assertEqual(
                    [semantic_projection(result) for result in results],
                    throughput_projection,
                )
                current_hashes = [str(result["trace_hash"]) for result in results]
                self.assertTrue(all(current_hashes))
                if trace_hashes is None:
                    trace_hashes = current_hashes
                self.assertEqual(current_hashes, trace_hashes)


if __name__ == "__main__":
    unittest.main()
