import unittest

from tools.m4.job_generation import configure_job, derive_job, derive_jobs, splitmix64


class JobGenerationTests(unittest.TestCase):
    def test_splitmix64_reference_vectors(self):
        vectors = {
            (0x0123456789ABCDEF, 0): 0x157A3807A48FAA9D,
            (0x0123456789ABCDEF, 1): 0xD573529B34A1D093,
            (0x0123456789ABCDEF, 2): 0x2F90B72E996DCCBE,
        }
        for (seed, index), expected in vectors.items():
            value = (seed + index * 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
            self.assertEqual(splitmix64(value), expected)

    def test_partition_cycle_and_job_ids(self):
        jobs = [derive_job(123, index, 2200) for index in range(8)]
        self.assertEqual(
            [(job["seat_assignment"], job["starting_player"]) for job in jobs],
            [
                ("normal", 0),
                ("mirror", 0),
                ("normal", 1),
                ("mirror", 1),
                ("normal", 0),
                ("mirror", 0),
                ("normal", 1),
                ("mirror", 1),
            ],
        )
        self.assertEqual(
            [job["job_id"] for job in jobs],
            [f"m4-{index:06d}" for index in range(8)],
        )

    def test_mapping_does_not_depend_on_worker_count(self):
        first = derive_jobs(987654321, 32)
        second = derive_jobs(987654321, 32)
        self.assertEqual(first, second)

    def test_zero_and_negative_job_counts(self):
        self.assertEqual(derive_jobs(123, 0), [])
        with self.assertRaises(ValueError):
            derive_jobs(123, -1)

    def test_inputs_are_masked_to_64_bits_and_ids_are_unique(self):
        jobs = derive_jobs(-1, 64, max_steps=17)
        self.assertEqual(len({job["job_id"] for job in jobs}), 64)
        self.assertTrue(all(0 <= job["seed"] <= 0xFFFFFFFFFFFFFFFF for job in jobs))
        self.assertTrue(all(job["max_steps"] == 17 for job in jobs))

    def test_execution_configuration_changes_only_execution_options(self):
        base = derive_job(123, 2, 2200)
        configured = configure_job(
            base,
            mode="conformance",
            observation_mode="off_diagnostic",
            instrumentation=True,
            persist_trace=True,
        )
        for field in (
            "job_id",
            "seed",
            "seat_assignment",
            "starting_player",
            "max_steps",
            "canonical_rules_id",
        ):
            self.assertEqual(configured[field], base[field])
        self.assertEqual(configured["mode"], "conformance")
        self.assertEqual(configured["observation_mode"], "off_diagnostic")
        self.assertTrue(configured["instrumentation"])
        self.assertTrue(configured["persist_trace"])
        self.assertEqual(base["mode"], "throughput")
        self.assertEqual(base["observation_mode"], "full")


if __name__ == "__main__":
    unittest.main()
