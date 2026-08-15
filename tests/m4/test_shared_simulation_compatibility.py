from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PROBE = Path(os.environ.get("YGO_CORE_PROBE", ROOT / "build" / "windows-zig" / "ygo_core_probe.exe"))

# Characterization evidence captured from the pre-extraction M3 probe with the
# required Task-3 workload. These values are the compatibility contract for
# the shared simulation extraction.
EXPECTED_SEMANTIC_GAMEPLAY_HASH = "e19349b22796b18eaf1fb35cf34b0b2c95cbb0ad36c161376c1d431dd9798320"
EXPECTED_TRACE_HASH = "afb6ee362c5cabf850c5ec4c7098a12981ce0d550be7658a28309090343457a8"
ERROR_FIELDS = (
    "unsupported_count",
    "retry_count",
    "automatic_decision_count",
    "candidate_truncation_count",
    "core_error_count",
)


def _read_summary(output: Path) -> dict[str, object]:
    for line in output.read_text(encoding="utf-8").splitlines():
        if line.startswith("# m3_summary="):
            return json.loads(line[len("# m3_summary="):])
    raise AssertionError(f"probe output is missing # m3_summary=: {output}")


class SharedSimulationCompatibilityTest(unittest.TestCase):
    def test_canonical_full_game_characterization(self) -> None:
        if not PROBE.is_file():
            self.fail(f"ygo_core_probe executable not found: {PROBE}")

        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task3-") as directory:
            output = Path(directory) / "characterization.jsonl"
            completed = subprocess.run(
                [
                    str(PROBE),
                    "--m3-full-game",
                    "--seed",
                    "2",
                    "--starting-player",
                    "0",
                    "--max-steps",
                    "1800",
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=300,
                check=False,
            )
            summary = _read_summary(output) if completed.returncode == 0 else {}

        self.assertEqual(
            completed.returncode,
            0,
            msg=f"probe failed with exit {completed.returncode}:\n{completed.stderr[-4000:]}",
        )
        self.assertTrue(summary.get("terminal"))
        self.assertEqual(summary.get("semantic_gameplay_hash"), EXPECTED_SEMANTIC_GAMEPLAY_HASH)
        self.assertEqual(summary.get("trace_hash"), EXPECTED_TRACE_HASH)
        for field in ERROR_FIELDS:
            self.assertEqual(summary.get(field), 0, msg=f"{field} was non-zero")

        self.assertRegex(str(summary.get("semantic_gameplay_hash")), r"^[0-9a-f]{64}$")
        self.assertRegex(str(summary.get("trace_hash")), r"^[0-9a-f]{64}$")

    def test_nonterminal_max_steps_preserves_legacy_success_exit(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task3-nonterminal-") as directory:
            output = Path(directory) / "nonterminal.jsonl"
            completed = subprocess.run(
                [
                    str(PROBE),
                    "--m3-full-game",
                    "--seed",
                    "2",
                    "--starting-player",
                    "0",
                    "--max-steps",
                    "1",
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=300,
                check=False,
            )
            self.assertEqual(completed.returncode, 0, msg=completed.stderr[-4000:])
            summary = _read_summary(output)

        self.assertFalse(summary.get("terminal"))
        for field in ERROR_FIELDS:
            self.assertEqual(summary.get(field), 0, msg=f"{field} was non-zero")
        self.assertRegex(str(summary.get("semantic_gameplay_hash")), r"^[0-9a-f]{64}$")
        self.assertRegex(str(summary.get("trace_hash")), r"^[0-9a-f]{64}$")

    def test_force_unsupported_preserves_diagnostic_exit(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ocgforge-m4-task3-force-") as directory:
            output = Path(directory) / "must-not-be-written.jsonl"
            completed = subprocess.run(
                [
                    str(PROBE),
                    "--m3-full-game",
                    "--seed",
                    "2",
                    "--starting-player",
                    "0",
                    "--max-steps",
                    "1800",
                    "--force-unsupported",
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                capture_output=True,
                text=True,
                timeout=300,
                check=False,
            )
            output_was_written = output.exists()

        self.assertEqual(completed.returncode, 3, msg=completed.stderr[-4000:])
        self.assertIn("UNSUPPORTED_OR_MALFORMED_DECISION", completed.stderr)
        self.assertFalse(output_was_written)


if __name__ == "__main__":
    unittest.main()
