import unittest

from tools.episodic.acceptance import (
    ROOT,
    canonicalize_evidence_line,
    display_command,
    gate_from_ctest_evidence,
    parse_ctest_test_results,
)


class EpisodicAcceptanceEvidenceTests(unittest.TestCase):
    def test_display_command_hides_run_specific_evidence_directory(self):
        command = [
            "ygo_episodic_reset_probe.exe",
            "--output",
            "artifacts/episodic/v2/g32-run-a/g05_reset_soak.json",
        ]

        self.assertEqual(
            [
                "ygo_episodic_reset_probe.exe",
                "--output",
                "<OUTPUT>/g05_reset_soak.json",
            ],
            display_command(command),
        )
        self.assertEqual(
            display_command(
                [
                    "ygo_episodic_reset_probe.exe",
                    "--output",
                    "artifacts/episodic/v2/g32-run-b/g05_reset_soak.json",
                ]
            ),
            display_command(command),
        )

    def test_canonicalize_evidence_line_removes_paths_and_runtime_only_timings(self):
        encoded_root = str(ROOT).replace("\\", "\\\\")
        line = (
            r'17/19 Test #18: episodic_fault_injection_test ....................   Passed    0.24 sec '
            f'probe="{encoded_root}\\\\build\\g32-clean\\ygo_episodic_probe.exe"'
        )

        self.assertEqual(
            r'17/19 Test #18: episodic_fault_injection_test ....................   Passed    <elapsed> sec probe="<REPO>/build/g32-clean/ygo_episodic_probe.exe"',
            canonicalize_evidence_line(line),
        )
        self.assertEqual(
            canonicalize_evidence_line(line),
            canonicalize_evidence_line(line.replace("0.24 sec", "0.27 sec")),
        )

    def test_canonicalize_evidence_line_preserves_semantic_counts(self):
        line = "{\"gate\":\"G05\",\"episode_count\":500,\"continuation_count\":2}"

        self.assertEqual(line, canonicalize_evidence_line(line))

    def test_canonicalize_evidence_line_canonicalizes_all_runner_timings(self):
        self.assertEqual(
            "Total Test time (real) = <elapsed> sec",
            canonicalize_evidence_line("Total Test time (real) = 1566.33 sec"),
        )
        self.assertEqual(
            "M4_ACCEPTANCE_SCALE    = <elapsed> sec*proc (1 test)",
            canonicalize_evidence_line("M4_ACCEPTANCE_SCALE    = 1060.92 sec*proc (1 test)"),
        )
        self.assertEqual(
            "Ran 8 tests in <elapsed>s",
            canonicalize_evidence_line("Ran 8 tests in 0.023s"),
        )

    def test_parse_ctest_test_results_requires_explicit_passed_test_names(self):
        output = """
1/2 Test #9: episodic_environment_test ...................   Passed    0.01 sec
2/2 Test #12: episodic_paired_world_test .................***Failed    0.02 sec
"""
        self.assertEqual(
            {
                "episodic_environment_test": "PASS",
                "episodic_paired_world_test": "FAIL",
            },
            parse_ctest_test_results(output),
        )

    def test_gate_from_ctest_evidence_fails_when_expected_test_is_missing(self):
        run = {
            "returncode": 0,
            "ctest_tests": {"episodic_environment_test": "PASS"},
        }
        result = gate_from_ctest_evidence(
            "G22",
            run,
            {"episodic_environment_test", "episodic_paired_world_test"},
            "paired-world evidence",
        )
        self.assertEqual("FAIL", result["result"])
        self.assertIn("episodic_paired_world_test", result["reason"])

    def test_gate_from_ctest_evidence_does_not_treat_unrelated_green_run_as_pass(self):
        run = {
            "returncode": 0,
            "ctest_tests": {"m4_simulation_contract_test": "PASS"},
        }
        result = gate_from_ctest_evidence(
            "G22",
            run,
            {"episodic_paired_world_test"},
            "paired-world evidence",
        )
        self.assertEqual("FAIL", result["result"])


if __name__ == "__main__":
    unittest.main()
