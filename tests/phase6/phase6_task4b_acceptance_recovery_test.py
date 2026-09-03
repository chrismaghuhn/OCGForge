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
H_CONTRACT = "43a8b7748ce5e6ff3326a52f45e709101e6849aa"
H_RECOVERY = "c" * 40


def _historical_fixture() -> dict[str, bytes]:
    root = Path(__file__).resolve().parents[2]
    return {
        relative: (root / relative).read_bytes()
        for relative in recovery.HISTORICAL_FILE_SHA256
    }


def _successful_command_records(
    build_dir: Path = Path("C:/build"),
    probe_path: Path = Path("C:/build/phase6_task4_corpus_probe.exe"),
) -> tuple[recovery.RecoveryCommandRecordV1, ...]:
    commands = verifier._fixed_gate_commands(
        build_dir=build_dir,
        probe_path=probe_path,
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


def _synthetic_evidence() -> mock.Mock:
    evidence = mock.Mock()
    evidence.to_json.return_value = (
        '{"schema_id":"ocgforge.phase6.task4b.acceptance_recovery.v1"}'
    )
    evidence.original_attempt = None
    evidence.provenance = None
    evidence.ORIGINAL_SMOKE_PASS = None
    evidence.ORIGINAL_TASK4B_PASS = None
    evidence.TASK4B_RECOVERY_PASS = False
    evidence.TASK4B_FINAL_PASS = False
    evidence.recovery_failure = None
    evidence.recovery_gate_statuses = ()
    evidence.recovery_command_records = ()
    return evidence


class Task4BAcceptanceRecoveryTests(unittest.TestCase):
    def test_recovery_contract_commit_is_final_contract_head(self):
        self.assertEqual(recovery.RECOVERY_CONTRACT_COMMIT, H_CONTRACT)

    def test_not_run_command_record_uses_only_nullable_execution_facts(self):
        planned = recovery._RecoveryGateCommand(
            command_id="synthetic-gate",
            argv=("python", "-m", "synthetic"),
            probe_dependent=False,
        )
        record = recovery._not_run_record(planned)
        self.assertEqual(record.command_id, planned.command_id)
        self.assertEqual(record.argv, planned.argv)
        self.assertEqual(record.status, "NOT_RUN")
        self.assertIsNone(record.exit_code)
        self.assertIsNone(record.stdout_sha256)
        self.assertIsNone(record.stderr_sha256)

    def test_recovery_failure_has_exact_allowed_shape(self):
        failure = recovery.RecoveryFailureV1(
            error_code="HISTORICAL_FILE_HASH_MISMATCH",
            failure_stage="historical-evidence-validation",
            reached_command_count=0,
        )
        self.assertEqual(
            dataclasses.asdict(failure),
            {
                "error_code": "HISTORICAL_FILE_HASH_MISMATCH",
                "failure_stage": "historical-evidence-validation",
                "reached_command_count": 0,
            },
        )
        with self.assertRaises(ValueError):
            recovery.RecoveryFailureV1(
                error_code="PUBLICATION",
                failure_stage="evidence-publication",
                reached_command_count=0,
            )

    def test_executed_command_record_rejects_fabricated_not_run_facts(self):
        with self.assertRaises(ValueError):
            recovery.RecoveryCommandRecordV1(
                command_id="synthetic",
                argv=("python", "-m", "synthetic"),
                exit_code=0,
                stdout_sha256=None,
                stderr_sha256=None,
                status="NOT_RUN",
            )
        with self.assertRaises(ValueError):
            recovery.RecoveryCommandRecordV1(
                command_id="synthetic",
                argv=("python", "-m", "synthetic"),
                exit_code=0,
                stdout_sha256="0" * 64,
                stderr_sha256="0" * 63,
                status="PASS",
            )

    def test_historical_validation_reconstructs_manifest_and_smoke_evidence(self):
        fixture = _historical_fixture()
        with mock.patch.object(
            recovery,
            "_git_object_bytes",
            side_effect=lambda _root, _commit, relative: fixture[relative],
        ):
            historical = recovery._load_historical_evidence(Path("C:/source"))

        self.assertIsNotNone(historical.validated_artifacts)
        artifacts = historical.validated_artifacts
        assert artifacts is not None
        self.assertEqual(
            codec.canonical_training_run_manifest_bytes(artifacts.training_run_manifest),
            fixture["docs/p6/task4b/training-run-manifest.p6m"],
        )
        self.assertEqual(
            codec.canonical_smoke_evidence_bytes(artifacts.smoke_evidence),
            fixture["docs/p6/task4b/smoke-evidence.p6e"],
        )
        self.assertEqual(
            codec.training_run_identity(artifacts.training_run_manifest),
            artifacts.smoke_evidence.training_run_identity,
        )
        self.assertEqual(
            codec.smoke_evidence_identity(artifacts.smoke_evidence),
            recovery.HISTORICAL_SMOKE_EVIDENCE_IDENTITY,
        )

    def test_wrong_reconstructed_manifest_or_smoke_evidence_is_rejected(self):
        fixture = _historical_fixture()
        manifest_path = "docs/p6/task4b/training-run-manifest.p6m"
        wrong_manifest = fixture[manifest_path] + b"x"
        with mock.patch.dict(
            recovery.HISTORICAL_FILE_SHA256,
            {manifest_path: hashlib.sha256(wrong_manifest).hexdigest()},
        ), mock.patch.object(
            recovery,
            "_git_object_bytes",
            side_effect=lambda _root, _commit, relative: (
                wrong_manifest if relative == manifest_path else fixture[relative]
            ),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._load_historical_evidence(Path("C:/source"))
        self.assertEqual(raised.exception.code, "ORIGINAL_TRAINING_MANIFEST_MISMATCH")

        smoke_path = "docs/p6/task4b/smoke-evidence.p6e"
        wrong_smoke = fixture[smoke_path] + b"x"
        with mock.patch.dict(
            recovery.HISTORICAL_FILE_SHA256,
            {smoke_path: hashlib.sha256(wrong_smoke).hexdigest()},
        ), mock.patch.object(
            recovery,
            "_git_object_bytes",
            side_effect=lambda _root, _commit, relative: (
                wrong_smoke if relative == smoke_path else fixture[relative]
            ),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._load_historical_evidence(Path("C:/source"))
        self.assertEqual(raised.exception.code, "ORIGINAL_SMOKE_EVIDENCE_MISMATCH")

    def test_partial_gate_is_fail_and_unreached_gates_are_not_run(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        records = [recovery._not_run_record(command) for command in commands]
        records[1] = recovery.RecoveryCommandRecordV1(
            command_id=commands[1].command_id,
            argv=commands[1].argv,
            exit_code=0,
            stdout_sha256=hashlib.sha256(b"").hexdigest(),
            stderr_sha256=hashlib.sha256(b"").hexdigest(),
            status="PASS",
        )
        statuses = recovery._derive_recovery_gate_statuses(commands, tuple(records))
        self.assertEqual(statuses["admitted-forward"], "FAIL")
        self.assertEqual(statuses["full-non-long-ctest"], "NOT_RUN")

    def test_recovery_commands_fail_fast_after_first_failed_command(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        failed = recovery.RecoveryCommandRecordV1(
            command_id=commands[0].command_id,
            argv=commands[0].argv,
            exit_code=7,
            stdout_sha256=hashlib.sha256(b"out").hexdigest(),
            stderr_sha256=hashlib.sha256(b"err").hexdigest(),
            status="FAIL",
        )
        with mock.patch.object(recovery, "_verify_recovery_worktree"), mock.patch.object(
            recovery,
            "_sha256_file",
            return_value=recovery.HISTORICAL_PROBE_SHA256,
        ), mock.patch.object(
            recovery,
            "_run_recovery_command",
            side_effect=(failed, AssertionError("second command must not run")),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._run_recovery_commands(
                    commands,
                    source_root=Path("C:/source"),
                    expected_head=H_RECOVERY,
                    output_dir=Path("C:/source/docs/p6/task4b/recovery-v1"),
                    probe_path=Path("C:/build/phase6_task4_corpus_probe.exe"),
                )
        self.assertEqual(raised.exception.failure_stage, "gate-execution")
        self.assertEqual(raised.exception.reached_command_count, 1)
        self.assertEqual(len(raised.exception.records), 1)

    def test_fail_fast_within_multi_command_gate_counts_started_probe_once(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        first = recovery.RecoveryCommandRecordV1(
            command_id=commands[1].command_id,
            argv=commands[1].argv,
            exit_code=0,
            stdout_sha256="0" * 64,
            stderr_sha256="1" * 64,
            status="PASS",
        )
        second = recovery.RecoveryCommandRecordV1(
            command_id=commands[2].command_id,
            argv=commands[2].argv,
            exit_code=9,
            stdout_sha256="2" * 64,
            stderr_sha256="3" * 64,
            status="FAIL",
        )
        with mock.patch.object(recovery, "_verify_recovery_worktree"), mock.patch.object(
            recovery,
            "_sha256_file",
            return_value=recovery.HISTORICAL_PROBE_SHA256,
        ), mock.patch.object(
            recovery,
            "_run_recovery_command",
            side_effect=(first, second, AssertionError("later command must not run")),
        ):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._run_recovery_commands(
                    commands[1:],
                    source_root=Path("C:/source"),
                    expected_head=H_RECOVERY,
                    output_dir=Path("C:/source/docs/p6/task4b/recovery-v1"),
                    probe_path=Path("C:/build/phase6_task4_corpus_probe.exe"),
                )
        self.assertEqual(raised.exception.failure_stage, "gate-execution")
        self.assertEqual(raised.exception.reached_command_count, 2)
        self.assertEqual(raised.exception.ephemeral_probe_regressions, 2)

    def test_probe_mutation_after_started_command_is_post_gate_failure(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        passed = recovery.RecoveryCommandRecordV1(
            command_id=commands[1].command_id,
            argv=commands[1].argv,
            exit_code=0,
            stdout_sha256="0" * 64,
            stderr_sha256="1" * 64,
            status="PASS",
        )
        with mock.patch.object(recovery, "_verify_recovery_worktree"), mock.patch.object(
            recovery,
            "_sha256_file",
            side_effect=(recovery.HISTORICAL_PROBE_SHA256, "changed"),
        ), mock.patch.object(recovery, "_run_recovery_command", return_value=passed):
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._run_recovery_commands(
                    commands[1:2],
                    source_root=Path("C:/source"),
                    expected_head=H_RECOVERY,
                    output_dir=Path("C:/source/docs/p6/task4b/recovery-v1"),
                    probe_path=Path("C:/build/phase6_task4_corpus_probe.exe"),
                )
        self.assertEqual(raised.exception.failure_stage, "post-gate-integrity")
        self.assertEqual(raised.exception.reached_command_count, 1)
        self.assertEqual(raised.exception.ephemeral_probe_regressions, 1)

    def test_pre_command_integrity_failure_does_not_start_command(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        with mock.patch.object(
            recovery,
            "_verify_recovery_worktree",
            side_effect=recovery.Task4BAcceptanceRecoveryError("RECOVERY_SOURCE_CHANGED"),
        ), mock.patch.object(
            recovery,
            "_run_recovery_command",
        ) as run_command:
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._run_recovery_commands(
                    commands,
                    source_root=Path("C:/source"),
                    expected_head=H_RECOVERY,
                    output_dir=Path("C:/source/docs/p6/task4b/recovery-v1"),
                    probe_path=Path("C:/build/phase6_task4_corpus_probe.exe"),
                )
        run_command.assert_not_called()
        self.assertEqual(raised.exception.failure_stage, "gate-execution")
        self.assertEqual(raised.exception.reached_command_count, 0)

    def test_complete_gate_remains_pass_after_later_post_gate_failure(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        records = [recovery._not_run_record(command) for command in commands]
        for index in (0, 1):
            records[index] = recovery.RecoveryCommandRecordV1(
                command_id=commands[index].command_id,
                argv=commands[index].argv,
                exit_code=0,
                stdout_sha256="0" * 64,
                stderr_sha256="1" * 64,
                status="PASS",
            )
        statuses = recovery._derive_recovery_gate_statuses(commands, tuple(records))
        self.assertEqual(statuses["task4-focused-python"], "PASS")
        self.assertEqual(statuses["admitted-forward"], "FAIL")
        self.assertEqual(statuses["full-non-long-ctest"], "NOT_RUN")

    def test_early_evaluation_failure_publishes_typed_failure_result(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            output = source / "docs/p6/task4b/recovery-v1"
            with mock.patch.object(recovery, "_canonical_source_root", return_value=source), mock.patch.object(
                recovery, "_git_head", return_value=H_RECOVERY
            ), mock.patch.object(
                recovery,
                "_load_historical_evidence",
                side_effect=recovery.Task4BAcceptanceRecoveryError(
                    "HISTORICAL_FILE_HASH_MISMATCH"
                ),
            ):
                result = recovery.run_acceptance_recovery(
                    build_dir=source / "build",
                    output_dir=output,
                )
            self.assertFalse(result.TASK4B_RECOVERY_PASS)
            self.assertFalse(result.TASK4B_FINAL_PASS)
            self.assertIsNone(result.original_attempt)
            self.assertIsNone(result.provenance)
            self.assertIsNone(result.semantic_integrity_proof)
            self.assertIsNotNone(result.recovery_failure)
            assert result.recovery_failure is not None
            self.assertEqual(
                result.recovery_failure.failure_stage,
                "historical-evidence-validation",
            )
            self.assertEqual(len(result.recovery_command_records), 14)
            self.assertTrue(
                all(
                    record.status == "NOT_RUN"
                    and record.exit_code is None
                    and record.stdout_sha256 is None
                    and record.stderr_sha256 is None
                    for record in result.recovery_command_records
                )
            )
            payload = json.loads(
                (output / recovery.RECOVERY_JSON_FILENAME).read_text(encoding="utf-8")
            )
            self.assertEqual(
                set(payload["recovery_failure"]),
                {"error_code", "failure_stage", "reached_command_count"},
            )
            self.assertIsNone(payload["original_attempt"])
            self.assertIsNone(payload["provenance"])
            self.assertIsNone(payload["semantic_integrity_proof"])

    def test_failure_evaluation_preserves_reached_facts_and_all_fourteen_records(self):
        commands = recovery._recovery_gate_commands(
            Path("C:/build"),
            Path("C:/build/phase6_task4_corpus_probe.exe"),
            H_SMOKE_EXEC,
        )
        records = _successful_command_records()
        state = recovery._RecoveryAttemptState(
            planned_commands=commands,
            historical=recovery._HistoricalEvidence(
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
            ),
            provenance=recovery._make_provenance(H_RECOVERY),
            semantic_integrity_proof=recovery.SemanticIntegrityProofV1(
                comparison_base_commit=H_SMOKE_EXEC,
                verifier_fix_commit=H_VERIFIER_FIX,
                observed_non_evidence_paths=(),
                expected_verifier_fix_paths=(),
                protected_semantic_diff_paths=(),
                protected_semantic_diff_sha256="0" * 64,
                recovery_source_paths=(),
                expected_recovery_source_paths=(),
                rules_deck_teacher_phase5_unchanged=True,
            ),
            records=records,
            reached_command_count=14,
            ephemeral_probe_regressions=3,
            failure_stage="post-gate-integrity",
        )
        failure = recovery.Task4BAcceptanceRecoveryError(
            "RECOVERY_WORKTREE_DIRTY",
            failure_stage="post-gate-integrity",
            reached_command_count=14,
            records=records,
            ephemeral_probe_regressions=3,
        )
        result = recovery._failure_evaluation(state, failure)
        self.assertEqual(len(result.recovery_command_records), 14)
        self.assertEqual(
            result.recovery_execution.EPHEMERAL_PROBE_REGRESSION_INVOCATIONS,
            3,
        )
        self.assertTrue(
            all(status == "PASS" for _, status in result.recovery_gate_statuses)
        )
        self.assertIsNotNone(result.recovery_failure)
        assert result.recovery_failure is not None
        self.assertEqual(result.recovery_failure.reached_command_count, 14)
        self.assertEqual(result.recovery_failure.failure_stage, "post-gate-integrity")
        self.assertFalse(result.TASK4B_RECOVERY_PASS)
        self.assertFalse(result.TASK4B_FINAL_PASS)

    def test_historical_gate_validation_rejects_reordered_or_inconsistent_commands(self):
        fixture = _historical_fixture()
        report = json.loads(fixture["docs/p6/task4b/task4b-verification.json"])
        report["commands"][0], report["commands"][1] = (
            report["commands"][1],
            report["commands"][0],
        )
        with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
            recovery._validate_historical_gate_report(report)
        report = json.loads(fixture["docs/p6/task4b/task4b-verification.json"])
        report["commands"][3]["status"] = "PASS"
        with self.assertRaises(recovery.Task4BAcceptanceRecoveryError):
            recovery._validate_historical_gate_report(report)

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
            recovery_failure=recovery.RecoveryFailureV1(
                error_code="SYNTHETIC_FAILURE",
                failure_stage="gate-execution",
                reached_command_count=0,
            ),
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
        self.assertFalse(
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
                recovery_failure=recovery.RecoveryFailureV1(
                    error_code="GATE_FAILED",
                    failure_stage="gate-execution",
                    reached_command_count=1,
                ),
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
            evidence = _synthetic_evidence()
            recovery._publish_recovery_evidence(
                output,
                evidence,
            )
            self.assertEqual(
                (output / "task4b-acceptance-recovery.json").read_text(encoding="utf-8"),
                '{"schema_id":"ocgforge.phase6.task4b.acceptance_recovery.v1"}',
            )
            self.assertEqual(
                (output / "task4b-acceptance-recovery.md").read_text(encoding="utf-8"),
                recovery._markdown_from_evidence(evidence),
            )
            self.assertFalse((output / "task4b-acceptance-recovery.json.tmp").exists())
            self.assertFalse((output / "task4b-acceptance-recovery.md.tmp").exists())

    def test_publication_fsyncs_staged_files_and_skips_directory_sync_on_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = _synthetic_evidence()
            with mock.patch.object(recovery.os, "name", "nt"), mock.patch.object(
                recovery.os, "open"
            ) as open_directory, mock.patch.object(
                recovery.os, "fsync", wraps=recovery.os.fsync
            ) as fsync:
                recovery._publish_recovery_evidence(
                    output,
                    evidence,
                )
            open_directory.assert_not_called()
            self.assertEqual(fsync.call_count, 2)

    def test_publication_syncs_directories_when_posix_support_is_available(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = _synthetic_evidence()
            directory_path = Path(directory)
            with mock.patch.object(recovery.os, "name", "posix"), mock.patch.object(
                recovery.os,
                "open",
                return_value=41,
            ) as open_directory, mock.patch.object(
                recovery.os,
                "fsync",
            ) as fsync, mock.patch.object(
                recovery.os,
                "close",
            ) as close:
                self.assertTrue(
                    recovery._fsync_directory_if_supported(directory_path)
                )
            open_directory.assert_called_once()
            fsync.assert_called_once_with(41)
            close.assert_called_once_with(41)
            with mock.patch.object(
                recovery,
                "_fsync_directory_if_supported",
                return_value=True,
            ) as sync_directory:
                recovery._publish_recovery_evidence(
                    output,
                    evidence,
                )
            self.assertEqual(sync_directory.call_count, 2)

    def test_publication_write_failure_is_stable_and_does_not_expose_final_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = _synthetic_evidence()
            with mock.patch.object(
                recovery,
                "_write_staged_file",
                side_effect=(
                    None,
                    recovery.Task4BAcceptanceRecoveryError(
                        "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
                    ),
                ),
            ):
                with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                    recovery._publish_recovery_evidence(
                        output,
                        evidence,
                    )
            self.assertEqual(
                raised.exception.code,
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED",
            )
            self.assertFalse(output.exists())

    def test_existing_recovery_directory_rejects_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            output.mkdir()
            evidence = _synthetic_evidence()
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._publish_recovery_evidence(output, evidence)
            self.assertEqual(
                raised.exception.code,
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED",
            )

    def test_publication_rejects_historical_directory_as_target(self):
        with tempfile.TemporaryDirectory() as directory:
            historical_directory = Path(directory) / "task4b"
            evidence = _synthetic_evidence()
            with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                recovery._publish_recovery_evidence(historical_directory, evidence)
            self.assertEqual(
                raised.exception.code,
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED",
            )
            self.assertFalse(historical_directory.exists())

    def test_staging_readback_mismatch_prevents_final_publication(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = _synthetic_evidence()
            with mock.patch.object(
                recovery.Path,
                "read_bytes",
                return_value=b"mismatch",
            ):
                with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                    recovery._publish_recovery_evidence(output, evidence)
            self.assertEqual(
                raised.exception.code,
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED",
            )
            self.assertFalse(output.exists())

    def test_rename_failure_is_stable_and_cleans_private_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = _synthetic_evidence()
            with mock.patch.object(
                recovery.os,
                "replace",
                side_effect=OSError("rename failed"),
            ):
                with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                    recovery._publish_recovery_evidence(
                        output,
                        evidence,
                    )
            self.assertEqual(
                raised.exception.code,
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED",
            )
            self.assertFalse(output.exists())
            self.assertEqual(tuple(Path(directory).glob(".recovery-v1-*")), ())

    def test_post_rename_readback_failure_does_not_rewrite_visible_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "recovery-v1"
            evidence = _synthetic_evidence()
            real_replace = recovery.os.replace

            def replace_and_tamper(source, target):
                real_replace(source, target)
                (Path(target) / recovery.RECOVERY_JSON_FILENAME).write_bytes(b"tampered")

            with mock.patch.object(
                recovery.os,
                "replace",
                side_effect=replace_and_tamper,
            ):
                with self.assertRaises(recovery.Task4BAcceptanceRecoveryError) as raised:
                    recovery._publish_recovery_evidence(
                        output,
                        evidence,
                    )
            self.assertEqual(
                raised.exception.code,
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED",
            )
            self.assertEqual(
                (output / recovery.RECOVERY_JSON_FILENAME).read_bytes(),
                b"tampered",
            )

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
            validated_artifacts=mock.Mock(),
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
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            output = source / "docs/p6/task4b/recovery-v1"
            build = source / "build"
            commands = _successful_command_records(build, build / "probe.exe")
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
            ) as forbidden_verifier, mock.patch.object(
                recovery.subprocess,
                "run",
            ) as forbidden_process:
                result = recovery.run_acceptance_recovery(
                    build_dir=build,
                    output_dir=output,
                )

            forbidden_verifier.assert_not_called()
            forbidden_process.assert_not_called()
            self.assertTrue(result.TASK4B_RECOVERY_PASS)
            self.assertTrue(result.TASK4B_FINAL_PASS)
            self.assertIsNone(result.recovery_failure)
            self.assertEqual(len(result.recovery_command_records), 14)
            self.assertEqual(result.recovery_execution.EPHEMERAL_PROBE_REGRESSION_INVOCATIONS, 3)
            self.assertEqual(result.recovery_execution.MODEL_TRAINING_INVOCATIONS, 0)
            self.assertTrue((output / "task4b-acceptance-recovery.json").is_file())
            self.assertTrue((output / "task4b-acceptance-recovery.md").is_file())
            payload = json.loads(
                (output / recovery.RECOVERY_JSON_FILENAME).read_text(encoding="utf-8")
            )
            self.assertIsNone(payload["recovery_failure"])

    def test_mocked_gate_failure_publishes_partial_typed_result_and_stops(self):
        historical = recovery._HistoricalEvidence(
            h_smoke_exec=H_SMOKE_EXEC,
            h_failure_evidence=H_FAILURE_EVIDENCE,
            original_execution_report_sha256="0" * 64,
            original_verification_report_sha256="1" * 64,
            original_acceptance_report_sha256="2" * 64,
            original_checkpoint_identity="phase6_checkpoint.v1." + "3" * 64,
            original_smoke_evidence_identity="phase6_task4b_smoke_evidence.v1." + "4" * 64,
            original_probe_sha256=recovery.HISTORICAL_PROBE_SHA256,
            original_corpus_probe_source_commit=H_SMOKE_EXEC,
            original_smoke_pass=True,
            original_task4b_pass=False,
            original_failed_gate_id="full-non-long-ctest",
            original_failed_gate_exit_code=8,
            original_command_record_count=14,
            file_sha256=recovery.HISTORICAL_FILE_SHA256,
            validated_artifacts=mock.Mock(),
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
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            output = source / "docs/p6/task4b/recovery-v1"
            build = source / "build"
            commands = _successful_command_records(build, build / "probe.exe")
            failure = recovery.Task4BAcceptanceRecoveryError(
                "RECOVERY_COMMAND_FAILED",
                failure_stage="gate-execution",
                reached_command_count=1,
                records=(commands[0],),
                ephemeral_probe_regressions=0,
            )
            with mock.patch.object(recovery, "_canonical_source_root", return_value=source), mock.patch.object(
                recovery, "_git_head", return_value=H_RECOVERY
            ), mock.patch.object(
                recovery, "_load_historical_evidence", return_value=historical
            ), mock.patch.object(
                recovery, "_prove_source_integrity", return_value=proof
            ), mock.patch.object(
                recovery, "_validate_build_and_probe", return_value=build / "probe.exe"
            ), mock.patch.object(
                recovery, "_run_recovery_commands", side_effect=failure
            ), mock.patch.object(
                recovery, "_verify_recovery_worktree"
            ) as verify_worktree:
                result = recovery.run_acceptance_recovery(
                    build_dir=build,
                    output_dir=output,
                )
            self.assertEqual(verify_worktree.call_count, 1)
            self.assertFalse(result.TASK4B_RECOVERY_PASS)
            self.assertFalse(result.TASK4B_FINAL_PASS)
            self.assertEqual(result.recovery_failure.reached_command_count, 1)
            self.assertEqual(result.recovery_execution.EPHEMERAL_PROBE_REGRESSION_INVOCATIONS, 0)
            self.assertEqual(result.recovery_gate_statuses[0], ("task4-focused-python", "PASS"))
            self.assertEqual(result.recovery_gate_statuses[1], ("admitted-forward", "NOT_RUN"))
            self.assertEqual(result.recovery_command_records[0].status, "PASS")
            self.assertTrue(
                all(record.status == "NOT_RUN" for record in result.recovery_command_records[1:])
            )

    def test_final_post_command_integrity_failure_keeps_completed_gates_and_failure_evidence(self):
        historical = recovery._HistoricalEvidence(
            h_smoke_exec=H_SMOKE_EXEC,
            h_failure_evidence=H_FAILURE_EVIDENCE,
            original_execution_report_sha256="0" * 64,
            original_verification_report_sha256="1" * 64,
            original_acceptance_report_sha256="2" * 64,
            original_checkpoint_identity="phase6_checkpoint.v1." + "3" * 64,
            original_smoke_evidence_identity="phase6_task4b_smoke_evidence.v1." + "4" * 64,
            original_probe_sha256=recovery.HISTORICAL_PROBE_SHA256,
            original_corpus_probe_source_commit=H_SMOKE_EXEC,
            original_smoke_pass=True,
            original_task4b_pass=False,
            original_failed_gate_id="full-non-long-ctest",
            original_failed_gate_exit_code=8,
            original_command_record_count=14,
            file_sha256=recovery.HISTORICAL_FILE_SHA256,
            validated_artifacts=mock.Mock(),
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
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)
            output = source / "docs/p6/task4b/recovery-v1"
            build = source / "build"
            commands = _successful_command_records(build, build / "probe.exe")
            post_failure = recovery.Task4BAcceptanceRecoveryError(
                "RECOVERY_SOURCE_CHANGED",
                failure_stage="post-gate-integrity",
                reached_command_count=14,
                records=commands,
                ephemeral_probe_regressions=3,
            )
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
                recovery,
                "_verify_recovery_worktree",
                side_effect=(None, post_failure),
            ):
                result = recovery.run_acceptance_recovery(
                    build_dir=build,
                    output_dir=output,
                )
            self.assertFalse(result.TASK4B_RECOVERY_PASS)
            self.assertFalse(result.TASK4B_FINAL_PASS)
            self.assertEqual(result.recovery_failure.failure_stage, "post-gate-integrity")
            self.assertEqual(result.recovery_failure.reached_command_count, 14)
            self.assertTrue(
                all(status == "PASS" for _, status in result.recovery_gate_statuses)
            )

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
