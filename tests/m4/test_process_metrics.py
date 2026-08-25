import unittest
from unittest.mock import patch

from tools.m4.process_metrics import NOT_MEASURED, ProcessMetricsSampler


class ProcessMetricsSamplerTests(unittest.TestCase):
    def test_partial_pid_sample_is_not_reported_as_complete(self) -> None:
        with patch(
            "tools.m4.process_metrics.read_working_set_bytes",
            side_effect=lambda pid: 100 if pid == 101 else None,
        ):
            sampler = ProcessMetricsSampler(lambda: [101, 102])
            sampler._sample_once()
            snapshot = sampler.stop()

        self.assertEqual(snapshot.process_count, NOT_MEASURED)
        self.assertEqual(snapshot.peak_total_working_set_bytes, NOT_MEASURED)
        self.assertEqual(snapshot.peak_worker_working_set_bytes, NOT_MEASURED)
        self.assertEqual(snapshot.memory_per_active_environment_bytes, NOT_MEASURED)


if __name__ == "__main__":
    unittest.main()
