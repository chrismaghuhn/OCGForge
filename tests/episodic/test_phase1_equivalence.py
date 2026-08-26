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


def semantic_projection(value: dict) -> dict:
    projected = json.loads(json.dumps(value))
    projected.pop("build", None)
    projected.pop("source_base_commit", None)
    projected["collector"].pop("source_sha256", None)
    for job in projected["jobs"]:
        for record in job["records"]:
            record.pop("source_base_commit", None)
    return projected


def normalized_raw_manifest(value: dict) -> dict:
    projected = json.loads(json.dumps(value))
    projected["capture_worktree"] = "<immutable-worktree-a>"
    for artifact in projected.get("artifacts", []):
        artifact["path"] = artifact["path"].replace("\\", "/")
    for probe in projected.get("negative_probes", []):
        for name in ("output_path", "stderr_path"):
            if name in probe:
                probe[name] = probe[name].replace("\\", "/")
    return projected


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

    def test_collector_json_writer_is_lf_stable(self) -> None:
        from tools.episodic.capture_phase1_characterization import _write_json

        with tempfile.TemporaryDirectory(prefix="ocgforge-phase1-json-writer-") as directory:
            output = Path(directory) / "output.json"
            _write_json(output, {"value": "ok"})
            data = output.read_bytes()
        self.assertNotIn(b"\r", data)
        self.assertTrue(data.endswith(b"\n"))
        self.assertFalse(data.endswith(b"\n\n"))
        self.assertEqual(data, b'{\n  "value": "ok"\n}\n')

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
        raw_root_value = os.environ.get("OCGFORGE_PHASE1_RAW_ROOT")
        if not raw_root_value:
            self.fail("OCGFORGE_PHASE1_RAW_ROOT must point at immutable Worktree-A raw artifacts")
        raw_manifest_path = Path(raw_root_value) / "raw-artifact-manifest.json"
        self.assertTrue(raw_manifest_path.is_file(), f"missing raw artifact manifest: {raw_manifest_path}")
        raw_manifest = json.loads(raw_manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(provenance["raw_artifact_manifest"], normalized_raw_manifest(raw_manifest))

    def test_acceptance_identity_rejections_fail_closed(self) -> None:
        raw_root_value = os.environ.get("OCGFORGE_PHASE1_POST_RAW_ROOT")
        if not raw_root_value:
            self.fail("OCGFORGE_PHASE1_POST_RAW_ROOT must point at post-refactor raw artifacts")
        from tests.episodic.phase1_acceptance import _assert_build_binding, _assert_manifest_binding

        raw_root = Path(raw_root_value)
        manifest_path = raw_root / "raw-artifact-manifest.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        with self.assertRaises(AssertionError):
            _assert_manifest_binding(raw_root, ROOT, "0" * 40, label="negative-head")

        for field, value in (
            ("probe_sha256", "0" * 64),
            ("cmake_cache_sha256", "0" * 64),
        ):
            candidate = json.loads(json.dumps(manifest))
            candidate["build"][field] = value
            with self.assertRaises(AssertionError):
                _assert_build_binding(ROOT, candidate, label=f"negative-{field}")

        for field, value in (
            ("probe_path", "../outside.exe"),
            ("build_identity", "Release"),
        ):
            candidate = json.loads(json.dumps(manifest))
            candidate["build"][field] = value
            with self.assertRaises(AssertionError):
                _assert_build_binding(ROOT, candidate, label=f"negative-{field}")

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

    def test_post_refactor_collector_matches_locked_semantics(self) -> None:
        raw_root_value = os.environ.get("OCGFORGE_PHASE1_POST_RAW_ROOT")
        if not raw_root_value:
            self.skipTest("OCGFORGE_PHASE1_POST_RAW_ROOT is not configured")
        from tools.episodic.capture_phase1_characterization import collect_characterization

        expected = json.loads(FIXTURE.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(prefix="ocgforge-phase1-post-collector-") as directory:
            actual = collect_characterization(
                Path(raw_root_value),
                Path(directory) / "post-refactor.json",
                require_baseline=False,
            )
        self.assertEqual(semantic_projection(actual), semantic_projection(expected))

    def test_post_refactor_trace_bytes_match_locked_corpus(self) -> None:
        baseline_root_value = os.environ.get("OCGFORGE_PHASE1_RAW_ROOT")
        post_root_value = os.environ.get("OCGFORGE_PHASE1_POST_RAW_ROOT")
        if not baseline_root_value or not post_root_value:
            self.skipTest("both Phase-1 raw-artifact roots are required")
        baseline_root = Path(baseline_root_value)
        post_root = Path(post_root_value)
        trace_paths = sorted((baseline_root / "full-games").glob("*.jsonl"))
        trace_paths += [baseline_root / "shared" / "full.jsonl", baseline_root / "shared" / "nonterminal.jsonl"]
        self.assertEqual(len(trace_paths), 18)
        for baseline_path in trace_paths:
            relative_path = baseline_path.relative_to(baseline_root)
            self.assertEqual(
                baseline_path.read_bytes(),
                (post_root / relative_path).read_bytes(),
                str(relative_path),
            )


if __name__ == "__main__":
    unittest.main()
