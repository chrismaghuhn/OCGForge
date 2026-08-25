"""Regression tests for the M4.3.6 bytewise fixture-equivalence gate."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tools.m4.run_m4_3_6_direct_writer_ab import _compare_fixture_dump_dirs


_DUMP_FILES = (
    "rich.canonical_without_hash.bin",
    "rich.canonical.bin",
    "terminal.canonical_without_hash.bin",
    "terminal.canonical.bin",
)


class M436HarnessTests(unittest.TestCase):
    def test_fixture_dump_gate_requires_exact_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            control = root / "control"
            experiment = root / "experiment"
            control.mkdir()
            experiment.mkdir()
            for name in _DUMP_FILES:
                (control / name).write_bytes(b"canonical-bytes-" + name.encode("ascii"))
                (experiment / name).write_bytes(b"canonical-bytes-" + name.encode("ascii"))

            equivalent = _compare_fixture_dump_dirs(control, experiment)

            self.assertTrue(equivalent["pass"])
            self.assertTrue(all(row["comparison"]["byte_exact"] for row in equivalent["files"]))
            self.assertEqual(
                equivalent["files"][0]["control"]["bytes"],
                equivalent["files"][0]["experiment"]["bytes"],
            )
            self.assertEqual(
                equivalent["files"][0]["control"]["sha256"],
                equivalent["files"][0]["experiment"]["sha256"],
            )

            (experiment / _DUMP_FILES[0]).write_bytes(b"different")
            divergent = _compare_fixture_dump_dirs(control, experiment)

            self.assertFalse(divergent["pass"])
            self.assertFalse(divergent["files"][0]["comparison"]["byte_exact"])
            self.assertFalse(divergent["files"][0]["comparison"]["sha256_equal"])


if __name__ == "__main__":
    unittest.main()
