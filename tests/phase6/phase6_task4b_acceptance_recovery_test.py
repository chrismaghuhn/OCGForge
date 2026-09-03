import dataclasses
import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4b_verify as verifier
from tools.phase6 import task4b_acceptance_recovery as recovery


BASE_HEAD = "1727f09eb0fdc4e4e25e3f9ced9748feb4058234"
H_SMOKE_EXEC = "8f682d4c9eb53a32be7cd8f6125048583943f19e"
H_FAILURE_EVIDENCE = "5bcec55bd473b1f599c99d7d8cbe5e31ba4c7832"
H_VERIFIER_FIX = "97fd0f6e8445a18a4f7939cc66bb8f131f905dcf"
H_CONTRACT = "1be26a984ac80ba97440c3caf78a1d36b1d1927b"
H_RECOVERY = "c" * 40


def _historical_fixture() -> dict[str, bytes]:
    root = Path(__file__).resolve().parents[2]
    return {
        relative: (root / relative).read_bytes()
        for relative in recovery.HISTORICAL_FILE_SHA256
    }


def _successful_command_records() -> tuple[recovery.RecoveryCommandRecordV1, ...]:
    commands = verifier._fixed_gate_commands(
        build_dir=Path("C:/build"),
        probe_path=Path("C:/build/phase6_task4_corpus_probe.exe"),
        h_exec=H_SMOKE_EXEC,
    )
    return tuple(
        recovery.RecoveryCommandRecordV1(
            command_id=command.command_id,
            argv=command.argv,
            exit_code=0,
            stdout_sha256=hashlib.sha256(b"").hexdigest(),
            stderr_sha256=hashlib.sha256(b"").hexdigest(),
            status="PASS",
        )
        for command in commands
    )


class Task4BAcceptanceRecoveryTests(unittest.TestCase):
    def test_historical_git_object_fixture_has_exact_anchors_and_hashes(self):
        fixture = _historical_fixture()

        with mock.patch.object(
            recovery,
            "_git_object_bytes",
            side_effect=lambda _root, _commit, relative: fixture[relative],
        ):
            historical = recovery._load_historical_evidence(Path("C:/source"))

        self.assertEqual(historical.h_smoke_exec, H_SMOKE_EXEC)
        self.assertEqual(historical.h_failure_evidence, H_FAILURE_EVIDENCE)
        self.assertEqual(historical.original_execution_report_sha256,
                         "051ba2320c32b8b64c0ed8954d85d3a4956038a59094610fad131c62464b4b7f")
        self.assertEqual(historical.original_failed_gate_id, "full-non-long-ctest")
        self.assertEqual(historical.original_failed_gate_exit_code, 8)
        self.assertEqual(historical.original_command_record_count, 14)
        self.assertTrue(historical.original_smoke_pass)
        self.assertFalse(historical.original_task4b_pass)
        self.assertEqual(
            historical.file_sha256,
            recovery.HISTORICAL_FILE_SHA256,
        )

    def test_historical_object_missing_and_wrong_bytes_fail_closed(self):
        fixture = _historical_fixture()
        with mock.patch.object(
            recovery,
            "_git_object_bytes",
            side_effect=recovery.Task4BAcceptanceRecoveryError("MISSING_HISTORICAL_OBJECT"),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
                recovery._load_historical_evidence(Path("C:/source"))

        wrong = dict(fixture)
        wrong["docs/p6/task4b/checkpoint.p6k"] = b"wrong"
        with mock.patch.object(
            recovery,
            "_git_object_bytes",
            side_effect=lambda _root, _commit, relative: wrong[relative],
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._load_historical_evidence(Path("C:/source"))
        self.assertEqual(raised.exception.code, "HISTORICAL_FILE_HASH_MISMATCH")

    def test_canonical_json_rejects_duplicates_nonfinite_and_noncanonical_bytes(self):
        with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
            recovery._parse_canonical_json(b'{"a":1,"a":2}')
        with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
            recovery._parse_canonical_json(b'{"a":NaN}')
        with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
            recovery._parse_canonical_json(b'{ "a": 1 }')
        with self.assertRaises(ValueError):
            recovery._canonical_json_bytes({"a": float("nan")})

    def test_recovery_json_uses_exact_original_attempt_contract_keys(self):
        historical = recovery._HistoricalEvidence(
            h_smoke_exec=H_SMOKE_EXEC,
            h_failure_evidence=H_FAILURE_EVIDENCE,
            original_execution_report_sha256="0" * 64,
            original_verification_report_sha256="1" * 64,
            original_acceptance_report_sha256="2" * 64,
            original_checkpoint_identity="phase6_checkpoint.v1." + "3" * 64,
            original_smoke_evidence_identity="phase6_task4b_smoke_evidence.v1." + "4" * 64,
            original_probe_sha256="5" * 64,
            original_corpus_probe_source_commit=H_SMOKE_EXEC,
            original_smoke_pass=True,
            original_task4b_pass=False,
            original_failed_gate_id="full-non-long-ctest",
            original_failed_gate_exit_code=8,
            original_command_record_count=14,
            file_sha256={},
        )
        evidence = recovery.Task4BAcceptanceRecoveryEvidenceV1(
            original_attempt=historical,
            provenance=recovery._make_provenance(H_RECOVERY),
            semantic_integrity_proof=recovery.SemanticIntegrityProofV1(
                comparison_base_commit=H_SMOKE_EXEC,
                verifier_fix_commit=H_VERIFIER_FIX,
                observed_non_evidence_paths=(),
                expected_verifier_fix_paths=(),
                protected_semantic_diff_paths=(),
                protected_semantic_diff_sha256="6" * 64,
                recovery_source_paths=(),
                expected_recovery_source_paths=(),
                rules_deck_teacher_phase5_unchanged=True,
            ),
            recovery_execution=recovery.RecoveryExecutionV1(
                CUDA_SMOKE_RERUN=False,
                AUTHORITATIVE_CORPUS_PROBE_RERUN=False,
                ADDITIONAL_OPTIMIZER_STEPS=0,
                MODEL_TRAINING_INVOCATIONS=0,
                EPHEMERAL_PROBE_REGRESSION_INVOCATIONS=3,
                EVIDENCE_MUTATION=False,
            ),
            recovery_gate_statuses=(),
            recovery_command_records=(),
            ORIGINAL_SMOKE_PASS=True,
            ORIGINAL_TASK4B_PASS=False,
            TASK4B_RECOVERY_PASS=False,
            TASK4B_FINAL_PASS=False,
        )
        original_keys = set(evidence.to_dict()["original_attempt"])
        self.assertEqual(
            original_keys,
            {
                "H_SMOKE_EXEC",
                "H_FAILURE_EVIDENCE",
                "original_execution_report_sha256",
                "original_verification_report_sha256",
                "original_acceptance_report_sha256",
                "original_checkpoint_identity",
                "original_smoke_evidence_identity",
                "original_probe_sha256",
                "original_corpus_probe_source_commit",
                "ORIGINAL_SMOKE_PASS",
                "ORIGINAL_TASK4B_PASS",
                "original_failed_gate_id",
                "original_failed_gate_exit_code",
                "original_command_record_count",
                "original_file_sha256",
            },
        )

    def test_original_status_and_identities_are_immutable(self):
        report = {
            "H_exec": H_SMOKE_EXEC,
            "SMOKE_PASS": True,
            "TASK4B_PASS": False,
            "actual_optimizer_steps": 500,
            "checkpoint_identity": recovery.HISTORICAL_CHECKPOINT_IDENTITY,
            "smoke_evidence_identity": recovery.HISTORICAL_SMOKE_EVIDENCE_IDENTITY,
            "corpus_probe_sha256": recovery.HISTORICAL_PROBE_SHA256,
            "corpus_probe_source_commit": H_SMOKE_EXEC,
        }
        recovery._validate_original_report_status(
            report,
            expected_checkpoint_identity=recovery.HISTORICAL_CHECKPOINT_IDENTITY,
            expected_smoke_evidence_identity=recovery.HISTORICAL_SMOKE_EVIDENCE_IDENTITY,
            expected_probe_sha256=recovery.HISTORICAL_PROBE_SHA256,
        )
        for field, value in (
            ("TASK4B_PASS", True),
            ("checkpoint_identity", "phase6_checkpoint.v1." + "d" * 64),
            ("smoke_evidence_identity", "phase6_task4b_smoke_evidence.v1." + "e" * 64),
            ("corpus_probe_sha256", "f" * 64),
            ("corpus_probe_source_commit", "1" * 40),
        ):
            with self.subTest(field=field):
                changed = dict(report)
                changed[field] = value
                with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
                    recovery._validate_original_report_status(
                        changed,
                        expected_checkpoint_identity=recovery.HISTORICAL_CHECKPOINT_IDENTITY,
                        expected_smoke_evidence_identity=recovery.HISTORICAL_SMOKE_EVIDENCE_IDENTITY,
                        expected_probe_sha256=recovery.HISTORICAL_PROBE_SHA256,
                    )

    def test_provenance_separation_never_relabels_verifier_as_training(self):
        provenance = recovery._make_provenance(H_RECOVERY)
        self.assertEqual(provenance.training_code_commit, H_SMOKE_EXEC)
        self.assertEqual(provenance.failed_evidence_commit, H_FAILURE_EVIDENCE)
        self.assertEqual(provenance.verifier_fix_commit, H_VERIFIER_FIX)
        self.assertEqual(provenance.recovery_contract_commit, H_CONTRACT)
        self.assertEqual(provenance.recovery_verifier_source_commit, H_RECOVERY)
        self.assertNotEqual(provenance.training_code_commit, provenance.verifier_fix_commit)
        self.assertNotEqual(provenance.training_code_commit, provenance.recovery_verifier_source_commit)

    def test_source_proof_accepts_exact_allowlists_and_rejects_third_paths(self):
        expected_verifier = tuple(sorted(recovery.EXPECTED_VERIFIER_FIX_PATHS))
        expected_recovery = tuple(sorted(recovery.EXPECTED_RECOVERY_SOURCE_PATHS))
        with mock.patch.object(
            recovery,
            "_git_is_ancestor",
            return_value=True,
        ), mock.patch.object(
            recovery,
            "_git_diff_paths",
            side_effect=(expected_verifier, expected_recovery),
        ):
            proof = recovery._prove_source_integrity(Path("C:/source"), H_RECOVERY)
        self.assertEqual(proof.observed_non_evidence_paths, expected_verifier)
        self.assertEqual(proof.protected_semantic_diff_paths, ())
        self.assertEqual(proof.recovery_source_paths, expected_recovery)
        self.assertEqual(proof.protected_semantic_diff_sha256,
                         hashlib.sha256(b"").hexdigest())
        self.assertTrue(proof.rules_deck_teacher_phase5_unchanged)

        with mock.patch.object(
            recovery,
            "_git_is_ancestor",
            return_value=True,
        ), mock.patch.object(
            recovery,
            "_git_diff_paths",
            side_effect=(expected_verifier + ("src/model/changed.cpp",), expected_recovery),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._prove_source_integrity(Path("C:/source"), H_RECOVERY)
        self.assertEqual(raised.exception.code, "UNEXPECTED_VERIFIER_FIX_PATH")

        with mock.patch.object(
            recovery,
            "_git_is_ancestor",
            return_value=True,
        ), mock.patch.object(
            recovery,
            "_git_diff_paths",
            side_effect=(expected_verifier, expected_recovery + ("CMakeLists.txt",)),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._prove_source_integrity(Path("C:/source"), H_RECOVERY)
        self.assertEqual(raised.exception.code, "UNEXPECTED_RECOVERY_SOURCE_PATH")

    def test_gate_set_has_exact_ids_records_and_corrected_exclusion(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        self.assertEqual(len(commands), 14)
        self.assertEqual(
            {command.command_id for command in commands},
            set(verifier.REQUIRED_GATE_IDS),
        )
        focused = [command for command in commands if command.command_id == "task4-focused-python"]
        self.assertEqual(len(focused), 1)
        self.assertEqual(
            focused[0].argv.count("tests.phase6.phase6_task4b_runner_test"),
            1,
        )
        full = next(command for command in commands if command.command_id == "full-non-long-ctest")
        self.assertEqual(
            full.argv[-1],
            "P4A_HEAVY_REPLAY|M4_HEAVY_LIFECYCLE|M4_ACCEPTANCE_SCALE|P6_PYTORCH_REQUIRED",
        )
        self.assertEqual(
            sum(command.probe_dependent for command in commands),
            3,
        )

    def test_status_formula_requires_all_counters_and_historical_predicates(self):
        self.assertTrue(
            recovery._derive_recovery_pass(
                immutable_original=True,
                exact_provenance=True,
                semantic_source=True,
                gates_pass=True,
                cuda_smoke_rerun=False,
                authoritative_probe_rerun=False,
                additional_optimizer_steps=0,
                model_training_invocations=0,
                ephemeral_probe_regressions=3,
                evidence_mutation=False,
            )
        )
        for field, value in (
            ("immutable_original", False),
            ("exact_provenance", False),
            ("semantic_source", False),
            ("gates_pass", False),
            ("cuda_smoke_rerun", True),
            ("authoritative_probe_rerun", True),
            ("additional_optimizer_steps", 1),
            ("model_training_invocations", 1),
            ("ephemeral_probe_regressions", 2),
            ("evidence_mutation", True),
        ):
            with self.subTest(field=field):
                values = dict(
                    immutable_original=True,
                    exact_provenance=True,
                    semantic_source=True,
                    gates_pass=True,
                    cuda_smoke_rerun=False,
                    authoritative_probe_rerun=False,
                    additional_optimizer_steps=0,
                    model_training_invocations=0,
                    ephemeral_probe_regressions=3,
                    evidence_mutation=False,
                )
                values[field] = value
                self.assertFalse(recovery._derive_recovery_pass(**values))
        self.assertTrue(
            recovery._derive_final_pass(
                original_smoke_pass=True,
                original_task4b_pass=False,
                immutable_original=True,
                recovery_pass=True,
            )
        )
        self.assertFalse(
            recovery._derive_final_pass(
                original_smoke_pass=True,
                original_task4b_pass=True,
                immutable_original=True,
                recovery_pass=True,
            )
        )

    def test_worktree_status_rejects_source_and_allows_no_unexpected_changes(self):
        with mock.patch.object(recovery, "_git_head", return_value=H_RECOVERY), mock.patch.object(
            recovery, "_git_changed_paths", return_value=()
        ), mock.patch.object(recovery, "_git_status_lines", return_value=()):
            recovery._verify_recovery_worktree(
                source_root=Path("C:/source"),
                expected_head=H_RECOVERY,
                output_dir=Path("C:/source/docs/p6/task4b/recovery-v1"),
            )

        for status in ((" M src/model/model.cpp",), ("?? unexpected.txt",), ("AM tools/phase6/x.py",)):
            with self.subTest(status=status):
                with mock.patch.object(recovery, "_git_head", return_value=H_RECOVERY), mock.patch.object(
                    recovery, "_git_changed_paths", return_value=()
                ), mock.patch.object(recovery, "_git_status_lines", return_value=status):
                    with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
                        recovery._verify_recovery_worktree(
                            source_root=Path("C:/source"),
                            expected_head=H_RECOVERY,
                            output_dir=Path("C:/source/docs/p6/task4b/recovery-v1"),
                        )

    def test_build_binding_requires_cache_probe_location_and_historical_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "build"
            build.mkdir()
            source = root / "source"
            (build / "CMakeCache.txt").write_text(
                "\n".join(
                    (
                        f"CMAKE_HOME_DIRECTORY:INTERNAL={str(source).replace('\\', '/')}",
                        "CMAKE_GENERATOR:INTERNAL=Ninja",
                        "CMAKE_BUILD_TYPE:STRING=Release",
                    )
                )
                + "\n",
                encoding="utf-8",
            )
            probe = build / "phase6_task4_corpus_probe.exe"
            probe.write_bytes(b"probe")
            with mock.patch.object(
                recovery,
                "_sha256_file",
                return_value=recovery.HISTORICAL_PROBE_SHA256,
            ):
                resolved = recovery._validate_build_and_probe(build, source)
            self.assertEqual(resolved, probe.resolve())

            (build / "CMakeCache.txt").write_text(
                (build / "CMakeCache.txt").read_text(encoding="utf-8").replace(
                    "CMAKE_GENERATOR:INTERNAL=Ninja",
                    "CMAKE_GENERATOR:INTERNAL=Visual Studio",
                ),
                encoding="utf-8",
            )
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
                recovery._validate_build_and_probe(build, source)

    def test_atomic_recovery_publication_writes_only_new_recovery_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = mock.Mock()
            evidence.to_json.return_value = '{"schema_id":"synthetic"}'
            recovery._publish_recovery_evidence(
                output,
                evidence,
                markdown="# synthetic\n",
            )
            self.assertEqual(
                (output / "task4b-acceptance-recovery.json").read_text(encoding="utf-8"),
                '{"schema_id":"synthetic"}',
            )
            self.assertEqual(
                (output / "task4b-acceptance-recovery.md").read_text(encoding="utf-8"),
                "# synthetic\n",
            )
            self.assertFalse((output / "task4b-acceptance-recovery.json.tmp").exists())
            self.assertFalse((output / "task4b-acceptance-recovery.md.tmp").exists())

    def test_mocked_recovery_success_publishes_new_evidence_without_real_gates(self):
        fixture = _historical_fixture()
        historical = recovery._HistoricalEvidence(
            h_smoke_exec=H_SMOKE_EXEC,
            h_failure_evidence=H_FAILURE_EVIDENCE,
            original_execution_report_sha256="051ba2320c32b8b64c0ed8954d85d3a4956038a59094610fad131c62464b4b7f",
            original_verification_report_sha256=recovery.HISTORICAL_FILE_SHA256["docs/p6/task4b/task4b-verification.json"],
            original_acceptance_report_sha256=recovery.HISTORICAL_FILE_SHA256["docs/p6/task4b/task4b-acceptance.json"],
            original_checkpoint_identity="phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327",
            original_smoke_evidence_identity="phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe",
            original_probe_sha256=recovery.HISTORICAL_PROBE_SHA256,
            original_corpus_probe_source_commit=H_SMOKE_EXEC,
            original_smoke_pass=True,
            original_task4b_pass=False,
            original_failed_gate_id="full-non-long-ctest",
            original_failed_gate_exit_code=8,
            original_command_record_count=14,
            file_sha256=recovery.HISTORICAL_FILE_SHA256,
        )
        proof = recovery.SemanticIntegrityProofV1(
            comparison_base_commit=H_SMOKE_EXEC,
            verifier_fix_commit=H_VERIFIER_FIX,
            observed_non_evidence_paths=tuple(sorted(recovery.EXPECTED_VERIFIER_FIX_PATHS)),
            expected_verifier_fix_paths=tuple(sorted(recovery.EXPECTED_VERIFIER_FIX_PATHS)),
            protected_semantic_diff_paths=(),
            protected_semantic_diff_sha256=hashlib.sha256(b"").hexdigest(),
            recovery_source_paths=tuple(sorted(recovery.EXPECTED_RECOVERY_SOURCE_PATHS)),
            expected_recovery_source_paths=tuple(sorted(recovery.EXPECTED_RECOVERY_SOURCE_PATHS)),
            rules_deck_teacher_phase5_unchanged=True,
        )
        commands = _successful_command_records()
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            output = source / "docs/p6/task4b/recovery-v1"
            build = source / "build"
            with mock.patch.object(recovery, "_canonical_source_root", return_value=source), mock.patch.object(
                recovery, "_git_head", return_value=H_RECOVERY
            ), mock.patch.object(
                recovery, "_load_historical_evidence", return_value=historical
            ), mock.patch.object(
                recovery, "_prove_source_integrity", return_value=proof
            ), mock.patch.object(
                recovery, "_validate_build_and_probe", return_value=build / "probe.exe"
            ), mock.patch.object(
                recovery, "_run_recovery_commands", return_value=commands
            ), mock.patch.object(
                recovery, "_verify_recovery_worktree"
            ), mock.patch.object(
                verifier, "run_post_smoke_verification"
            ) as forbidden_verifier:
                result = recovery.run_acceptance_recovery(
                    build_dir=build,
                    output_dir=output,
                )

            forbidden_verifier.assert_not_called()
            self.assertTrue(result.TASK4B_RECOVERY_PASS)
            self.assertTrue(result.TASK4B_FINAL_PASS)
            self.assertEqual(result.recovery_execution.EPHEMERAL_PROBE_REGRESSION_INVOCATIONS, 3)
            self.assertEqual(result.recovery_execution.MODEL_TRAINING_INVOCATIONS, 0)
            self.assertTrue((output / "task4b-acceptance-recovery.json").is_file())
            self.assertTrue((output / "task4b-acceptance-recovery.md").is_file())

    def test_mocked_failed_gate_remains_recovery_failure(self):
        commands = list(_successful_command_records())
        commands[3] = dataclasses.replace(commands[3], exit_code=8, status="FAIL")
        statuses = verifier._gate_statuses(tuple(commands))
        self.assertEqual(statuses["full-non-long-ctest"], "FAIL")
        self.assertFalse(
            recovery._derive_recovery_pass(
                immutable_original=True,
                exact_provenance=True,
                semantic_source=True,
                gates_pass=False,
                cuda_smoke_rerun=False,
                authoritative_probe_rerun=False,
                additional_optimizer_steps=0,
                model_training_invocations=0,
                ephemeral_probe_regressions=3,
                evidence_mutation=False,
            )
        )


if __name__ == "__main__":
    unittest.main()
