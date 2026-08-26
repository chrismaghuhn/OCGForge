from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "episodic" / "fixtures" / "phase1-pre-extraction-characterization.json"
PROVENANCE = ROOT / "tests" / "episodic" / "fixtures" / "phase1-pre-extraction-characterization.provenance.json"
COLLECTOR = ROOT / "tools" / "episodic" / "capture_phase1_characterization.py"
EXPECTED_BASE = "72c29009f107a2ebb172d85de1c70b38d2f007d8"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class Phase1EquivalenceTest(unittest.TestCase):
    def test_collector_is_a_pure_artifact_transform(self) -> None:
        tree = ast.parse(COLLECTOR.read_text(encoding="utf-8"))
        forbidden_imports = {"subprocess", "ctypes", "socket"}
        imported = {
            alias.name.split(".", 1)[0]
            for node in ast.walk(tree)
            if isinstance(node, ast.Import)
            for alias in node.names
        }
        imported.update(
            alias.name.split(".", 1)[0]
            for node in ast.walk(tree)
            if isinstance(node, ast.ImportFrom)
            for alias in node.names
        )
        self.assertTrue(forbidden_imports.isdisjoint(imported))
        source = COLLECTOR.read_text(encoding="utf-8")
        self.assertNotIn("Popen(", source)
        self.assertNotIn("run(", source)
        self.assertNotIn("OCG_DuelProcess", source)
        self.assertNotIn("ygo_core_probe", source)

    def test_checked_fixture_binds_the_immutable_baseline(self) -> None:
        self.assertTrue(FIXTURE.is_file(), f"missing generated fixture: {FIXTURE}")
        self.assertTrue(PROVENANCE.is_file(), f"missing provenance: {PROVENANCE}")
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        provenance = json.loads(PROVENANCE.read_text(encoding="utf-8"))
        self.assertEqual(fixture["schema_version"], "ocgforge.episodic.phase1.characterization.v1")
        self.assertEqual(fixture["source_base_commit"], EXPECTED_BASE)
        self.assertEqual(provenance["source_base_commit"], EXPECTED_BASE)
        self.assertEqual(provenance["fixture_sha256"], sha256_file(FIXTURE))
        self.assertEqual(provenance["collector_source_sha256"], sha256_file(COLLECTOR))
        self.assertEqual(provenance["raw_artifact_manifest"]["source_base_commit"], EXPECTED_BASE)

    def test_collector_output_matches_the_checked_fixture(self) -> None:
        raw_root_value = os.environ.get("OCGFORGE_PHASE1_RAW_ROOT")
        if not raw_root_value:
            self.fail("OCGFORGE_PHASE1_RAW_ROOT must point at immutable Worktree-A raw artifacts")
        from tools.episodic.capture_phase1_characterization import collect_characterization

        expected = json.loads(FIXTURE.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="ocgforge-phase1-collector-") as directory:
            actual = collect_characterization(Path(raw_root_value), Path(directory) / "out.json")
        self.assertEqual(actual, expected)

    def test_maximum_candidate_witness_is_first_in_execution_order(self) -> None:
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        records = [record for job in fixture["jobs"] for record in job["records"] if record["candidate_count"]]
        self.assertTrue(records)
        maximum = max(record["candidate_count"] for record in records)
        first = next(record for record in records if record["candidate_count"] == maximum)
        witness = fixture["maximum_candidate_witness"]
        self.assertEqual(witness["candidate_count"], maximum)
        self.assertEqual(witness["job_id"], first["job_id"])
        self.assertEqual(witness["ordered_record_index"], first["ordered_record_index"])
        self.assertEqual(witness["semantic_key_vector"], first["candidate_semantic_keys"])
        self.assertEqual(fixture["summary"]["candidate_count_max"], maximum)

    def test_failure_and_nonterminal_probes_remain_distinct(self) -> None:
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        jobs = {job["job_id"]: job for job in fixture["jobs"]}
        self.assertEqual(jobs["shared-full"]["summary"]["terminal"], True)
        self.assertEqual(jobs["shared-nonterminal"]["summary"]["terminal"], False)
        self.assertEqual(jobs["shared-nonterminal"]["summary"]["failure_code"], "nonterminal")
        forced = jobs["shared-forced-unsupported"]["summary"]
        self.assertFalse(forced["terminal"])
        self.assertEqual(forced["failure_code"], "forced_unsupported")
        self.assertEqual(forced["output_present"], False)


if __name__ == "__main__":
    unittest.main()
