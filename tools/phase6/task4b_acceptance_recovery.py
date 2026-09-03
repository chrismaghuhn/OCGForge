"""Acceptance-Recovery V1 for the immutable Phase-6 Task-4B smoke attempt.

The recovery API is intentionally separate from ``task4b_verify``.  It reads
the original evidence from Git objects, proves the permitted source boundary,
and, when explicitly invoked by a future authorized workflow, runs only the
corrected post-smoke gate set.  Importing this module never executes a gate,
probe, smoke, model, or optimizer operation.
"""

from __future__ import annotations

import dataclasses
import errno
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Mapping, Sequence

from . import task4_codec as codec
from . import task4_cuda
from . import task4_inference
from . import task4b_verify as verifier


H_SMOKE_EXEC = "8f682d4c9eb53a32be7cd8f6125048583943f19e"
H_FAILURE_EVIDENCE = "5bcec55bd473b1f599c99d7d8cbe5e31ba4c7832"
H_VERIFIER_FIX = "97fd0f6e8445a18a4f7939cc66bb8f131f905dcf"
RECOVERY_CONTRACT_COMMIT = "43a8b7748ce5e6ff3326a52f45e709101e6849aa"
RECOVERY_SCHEMA_ID = "ocgforge.phase6.task4b.acceptance_recovery.v1"
RECOVERY_OUTPUT_DIRECTORY = "docs/p6/task4b/recovery-v1"
RECOVERY_JSON_FILENAME = "task4b-acceptance-recovery.json"
RECOVERY_MARKDOWN_FILENAME = "task4b-acceptance-recovery.md"

HISTORICAL_FILE_SHA256: dict[str, str] = {
    "docs/p6/task4b/corpus.p6c": "0f410d8cd27aa6d40009fae6fdef156475ee9a0f0f8bce661884a2e5465b63a8",
    "docs/p6/task4b/corpus.authority.p6a": "a57d691c1d3f8b0514b17ef57813cac8fbafeef14681b7b6379ee9db7488701e",
    "docs/p6/task4b/checkpoint.p6k": "ccf86148a53c54ec35cb0129be7b652df671c6f7c8114c68705a265d554cc624",
    "docs/p6/task4b/training-run-manifest.p6m": "5511c410528270605700353468843b0453596a86c7b4e6c844a80a98eedd3dfc",
    "docs/p6/task4b/smoke-evidence.p6e": "f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe",
    "docs/p6/task4b/completion-receipt.json": "86a96780322caf4a24bf305c89e365602e06a0210a9cf99d002ee25f9d63d01f",
    "docs/p6/task4b/task4b-execution-report.json": "051ba2320c32b8b64c0ed8954d85d3a4956038a59094610fad131c62464b4b7f",
    "docs/p6/task4b/task4b-verification.json": "dbc93c1a9bede7f1290b092e00bfae048e02faa3df3677f50c8c3f9adb1ce90d",
    "docs/p6/task4b/task4b-acceptance.json": "f63137ccaf0206f194b4661534b9ebac05994280ccb7915bd95e3d58ad7c77f0",
    "docs/p6/task4b/task4b-acceptance.md": "c346a1db721acf1f47e4c380920ff037c40b409c3107a55d09efac1ac30b93a6",
}
HISTORICAL_PROBE_SHA256 = "074a796dab428af07ca8a81489f03a1f1aa52a1e581979726faee4fe2a0190c2"
HISTORICAL_CHECKPOINT_IDENTITY = "phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327"
HISTORICAL_SMOKE_EVIDENCE_IDENTITY = "phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe"
EXPECTED_VERIFIER_FIX_PATHS = frozenset(
    {
        "tests/phase6/phase6_task4b_verification_test.py",
        "tools/phase6/task4b_verify.py",
    }
)
EXPECTED_RECOVERY_SOURCE_PATHS = frozenset(
    {
        "tests/phase6/phase6_task4b_acceptance_recovery_test.py",
        "tools/phase6/task4b_acceptance_recovery.py",
    }
)
_HEX40 = re.compile(r"[0-9a-f]{40}")
_HEX64 = re.compile(r"[0-9a-f]{64}")
_RECOVERY_FAILURE_STAGES = frozenset(
    {
        "historical-evidence-validation",
        "provenance-validation",
        "semantic-source-integrity",
        "build-probe-binding",
        "gate-execution",
        "post-gate-integrity",
    }
)


class Task4BAcceptanceRecoveryError(RuntimeError):
    def __init__(
        self,
        code: str,
        *,
        failure_stage: str | None = None,
        reached_command_count: int = 0,
        records: Sequence["RecoveryCommandRecordV1"] = (),
        ephemeral_probe_regressions: int = 0,
    ) -> None:
        super().__init__(code)
        self.code = code
        self.failure_stage = failure_stage
        self.reached_command_count = reached_command_count
        self.records = tuple(records)
        self.ephemeral_probe_regressions = ephemeral_probe_regressions


@dataclasses.dataclass(frozen=True)
class RecoveryFailureV1:
    error_code: str
    failure_stage: str
    reached_command_count: int

    def __post_init__(self) -> None:
        if not isinstance(self.error_code, str) or not self.error_code:
            raise ValueError("recovery failure code must be non-empty")
        if self.failure_stage not in _RECOVERY_FAILURE_STAGES:
            raise ValueError("recovery failure stage is not accepted")
        if (
            not isinstance(self.reached_command_count, int)
            or isinstance(self.reached_command_count, bool)
            or not 0 <= self.reached_command_count <= 14
        ):
            raise ValueError("recovery failure command count is outside [0, 14]")


@dataclasses.dataclass(frozen=True)
class RecoveryCommandRecordV1:
    command_id: str
    argv: tuple[str, ...]
    exit_code: int | None
    stdout_sha256: str | None
    stderr_sha256: str | None
    status: str

    def __post_init__(self) -> None:
        if not isinstance(self.command_id, str) or not self.command_id:
            raise ValueError("command id must be non-empty")
        if not isinstance(self.argv, tuple) or not all(
            isinstance(value, str) for value in self.argv
        ):
            raise ValueError("command argv must be a string tuple")
        if self.status not in {"PASS", "FAIL", "NOT_RUN"}:
            raise ValueError("command status is not accepted")
        if self.status == "NOT_RUN":
            if any(
                value is not None
                for value in (self.exit_code, self.stdout_sha256, self.stderr_sha256)
            ):
                raise ValueError("NOT_RUN command contains execution facts")
            return
        if (
            not isinstance(self.exit_code, int)
            or isinstance(self.exit_code, bool)
            or not isinstance(self.stdout_sha256, str)
            or _HEX64.fullmatch(self.stdout_sha256) is None
            or not isinstance(self.stderr_sha256, str)
            or _HEX64.fullmatch(self.stderr_sha256) is None
        ):
            raise ValueError("executed command has invalid execution facts")


@dataclasses.dataclass(frozen=True)
class RecoveryProvenanceV1:
    training_code_commit: str
    failed_evidence_commit: str
    verifier_fix_commit: str
    recovery_contract_commit: str
    recovery_verifier_source_commit: str


@dataclasses.dataclass(frozen=True)
class SemanticIntegrityProofV1:
    comparison_base_commit: str
    verifier_fix_commit: str
    observed_non_evidence_paths: tuple[str, ...]
    expected_verifier_fix_paths: tuple[str, ...]
    protected_semantic_diff_paths: tuple[str, ...]
    protected_semantic_diff_sha256: str
    recovery_source_paths: tuple[str, ...]
    expected_recovery_source_paths: tuple[str, ...]
    rules_deck_teacher_phase5_unchanged: bool


@dataclasses.dataclass(frozen=True)
class RecoveryExecutionV1:
    CUDA_SMOKE_RERUN: bool
    AUTHORITATIVE_CORPUS_PROBE_RERUN: bool
    ADDITIONAL_OPTIMIZER_STEPS: int
    MODEL_TRAINING_INVOCATIONS: int
    EPHEMERAL_PROBE_REGRESSION_INVOCATIONS: int
    EVIDENCE_MUTATION: bool


@dataclasses.dataclass(frozen=True)
class _ValidatedHistoricalArtifacts:
    contents: Mapping[str, bytes]
    execution_report: Mapping[str, object]
    verification_report: Mapping[str, object]
    acceptance_report: Mapping[str, object]
    corpus: codec.DerivedCorpusV1
    checkpoint: codec.CheckpointArtifactV1
    training_run_manifest: codec.TrainingRunManifestV1
    smoke_evidence: codec.Task4BSmokeEvidenceV1
    completion_receipt: task4_inference.Task4BCompletionReceiptV1


@dataclasses.dataclass(frozen=True)
class _HistoricalEvidence:
    h_smoke_exec: str
    h_failure_evidence: str
    original_execution_report_sha256: str
    original_verification_report_sha256: str
    original_acceptance_report_sha256: str
    original_checkpoint_identity: str | None
    original_smoke_evidence_identity: str | None
    original_probe_sha256: str | None
    original_corpus_probe_source_commit: str | None
    original_smoke_pass: bool | None
    original_task4b_pass: bool | None
    original_failed_gate_id: str | None
    original_failed_gate_exit_code: int | None
    original_command_record_count: int | None
    file_sha256: Mapping[str, str]
    validated_artifacts: _ValidatedHistoricalArtifacts | None = dataclasses.field(
        default=None, repr=False, compare=False
    )


@dataclasses.dataclass(frozen=True)
class Task4BAcceptanceRecoveryEvidenceV1:
    original_attempt: _HistoricalEvidence | None
    provenance: RecoveryProvenanceV1 | None
    semantic_integrity_proof: SemanticIntegrityProofV1 | None
    recovery_execution: RecoveryExecutionV1
    recovery_gate_statuses: tuple[tuple[str, str], ...]
    recovery_command_records: tuple[RecoveryCommandRecordV1, ...]
    ORIGINAL_SMOKE_PASS: bool | None
    ORIGINAL_TASK4B_PASS: bool | None
    TASK4B_RECOVERY_PASS: bool
    TASK4B_FINAL_PASS: bool
    recovery_failure: RecoveryFailureV1 | None = None
    schema_id: str = RECOVERY_SCHEMA_ID

    def __post_init__(self) -> None:
        if self.schema_id != RECOVERY_SCHEMA_ID:
            raise ValueError("recovery evidence schema is not accepted")
        if not isinstance(self.TASK4B_RECOVERY_PASS, bool):
            raise ValueError("recovery status must be boolean")
        if not isinstance(self.TASK4B_FINAL_PASS, bool):
            raise ValueError("final status must be boolean")
        if self.TASK4B_RECOVERY_PASS and self.recovery_failure is not None:
            raise ValueError("successful recovery cannot contain a failure")
        if not self.TASK4B_RECOVERY_PASS and self.recovery_failure is None:
            raise ValueError("failed recovery must contain a failure")
        if self.TASK4B_FINAL_PASS and not self.TASK4B_RECOVERY_PASS:
            raise ValueError("final pass requires recovery pass")

    def to_dict(self) -> dict[str, object]:
        original = None
        if self.original_attempt is not None:
            original = {
                "H_SMOKE_EXEC": self.original_attempt.h_smoke_exec,
                "H_FAILURE_EVIDENCE": self.original_attempt.h_failure_evidence,
                "original_execution_report_sha256": self.original_attempt.original_execution_report_sha256,
                "original_verification_report_sha256": self.original_attempt.original_verification_report_sha256,
                "original_acceptance_report_sha256": self.original_attempt.original_acceptance_report_sha256,
                "original_checkpoint_identity": self.original_attempt.original_checkpoint_identity,
                "original_smoke_evidence_identity": self.original_attempt.original_smoke_evidence_identity,
                "original_probe_sha256": self.original_attempt.original_probe_sha256,
                "original_corpus_probe_source_commit": self.original_attempt.original_corpus_probe_source_commit,
                "ORIGINAL_SMOKE_PASS": self.original_attempt.original_smoke_pass,
                "ORIGINAL_TASK4B_PASS": self.original_attempt.original_task4b_pass,
                "original_failed_gate_id": self.original_attempt.original_failed_gate_id,
                "original_failed_gate_exit_code": self.original_attempt.original_failed_gate_exit_code,
                "original_command_record_count": self.original_attempt.original_command_record_count,
                "original_file_sha256": dict(self.original_attempt.file_sha256),
            }
        return {
            "schema_id": self.schema_id,
            "original_attempt": original,
            "provenance": (
                None
                if self.provenance is None
                else dataclasses.asdict(self.provenance)
            ),
            "semantic_integrity_proof": (
                None
                if self.semantic_integrity_proof is None
                else dataclasses.asdict(self.semantic_integrity_proof)
            ),
            "recovery_execution": dataclasses.asdict(self.recovery_execution),
            "recovery_gate_statuses": dict(self.recovery_gate_statuses),
            "recovery_command_records": [
                dataclasses.asdict(record)
                for record in self.recovery_command_records
            ],
            "ORIGINAL_SMOKE_PASS": self.ORIGINAL_SMOKE_PASS,
            "ORIGINAL_TASK4B_PASS": self.ORIGINAL_TASK4B_PASS,
            "TASK4B_RECOVERY_PASS": self.TASK4B_RECOVERY_PASS,
            "TASK4B_FINAL_PASS": self.TASK4B_FINAL_PASS,
            "recovery_failure": (
                None
                if self.recovery_failure is None
                else dataclasses.asdict(self.recovery_failure)
            ),
        }

    def to_json(self) -> str:
        return _canonical_json_bytes(self.to_dict()).decode("utf-8")


Task4BAcceptanceRecoveryResultV1 = Task4BAcceptanceRecoveryEvidenceV1


@dataclasses.dataclass(frozen=True)
class _RecoveryGateCommand:
    command_id: str
    argv: tuple[str, ...]
    probe_dependent: bool


def _canonical_json_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=True,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(bytes(value)).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise Task4BAcceptanceRecoveryError("PROBE_HASH_FAILED") from error
    return digest.hexdigest()


def _reject_duplicate_json_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _reject_nonfinite_json_constant(value: str) -> object:
    raise ValueError(f"non-finite JSON constant is not allowed: {value}")


def _parse_canonical_json(data: bytes) -> dict[str, object]:
    try:
        value = json.loads(
            data.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=_reject_nonfinite_json_constant,
        )
        canonical = _canonical_json_bytes(value)
    except (UnicodeError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise Task4BAcceptanceRecoveryError("NONCANONICAL_JSON") from error
    if not isinstance(value, dict) or data != canonical:
        raise Task4BAcceptanceRecoveryError("NONCANONICAL_JSON")
    return value


def _run_git_bytes(source_root: Path, *arguments: str) -> bytes:
    try:
        completed = subprocess.run(
            ("git", "-C", str(source_root), *arguments),
            shell=False,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise Task4BAcceptanceRecoveryError("GIT_COMMAND_FAILED") from error
    if completed.returncode != 0:
        raise Task4BAcceptanceRecoveryError("GIT_COMMAND_FAILED")
    return bytes(completed.stdout)


def _run_git_text(source_root: Path, *arguments: str) -> str:
    return _run_git_bytes(source_root, *arguments).decode("utf-8", "strict")


def _canonical_source_root() -> Path:
    module_root = Path(__file__).resolve().parents[2]
    root = Path(
        _run_git_text(module_root, "rev-parse", "--show-toplevel").strip()
    ).resolve()
    if os.path.normcase(str(root)) != os.path.normcase(str(module_root)):
        raise Task4BAcceptanceRecoveryError("SOURCE_ROOT_MISMATCH")
    return root


def _git_object_bytes(source_root: Path, commit: str, relative: str) -> bytes:
    if _HEX40.fullmatch(commit) is None:
        raise Task4BAcceptanceRecoveryError("INVALID_GIT_COMMIT")
    try:
        return _run_git_bytes(source_root, "show", f"{commit}:{relative}")
    except Task4BAcceptanceRecoveryError as error:
        raise Task4BAcceptanceRecoveryError("MISSING_HISTORICAL_FILE") from error


def _validate_original_report_status(
    report: Mapping[str, object],
    *,
    expected_checkpoint_identity: str,
    expected_smoke_evidence_identity: str,
    expected_probe_sha256: str,
) -> None:
    if report.get("H_exec") != H_SMOKE_EXEC:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_H_EXEC_MISMATCH")
    if report.get("SMOKE_PASS") is not True:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_SMOKE_STATUS_MISMATCH")
    if report.get("TASK4B_PASS") is not False:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_TASK4B_STATUS_MISMATCH")
    if report.get("actual_optimizer_steps") != 500:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_STEP_COUNT_MISMATCH")
    if report.get("corpus_probe_source_commit") != H_SMOKE_EXEC:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_PROBE_COMMIT_MISMATCH")
    if report.get("corpus_probe_sha256") != expected_probe_sha256:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_PROBE_HASH_MISMATCH")
    if report.get("checkpoint_identity") != expected_checkpoint_identity:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_CHECKPOINT_MISMATCH")
    if report.get("smoke_evidence_identity") != expected_smoke_evidence_identity:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_EVIDENCE_IDENTITY_MISMATCH")


def _reconstruct_historical_preflight(
    report: Mapping[str, object],
) -> task4_cuda.CudaPreflightResultV1:
    try:
        verifier._validate_cuda_preflight_report(report)
    except verifier.Task4BVerificationError as error:
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_CUDA_PREFLIGHT"
        ) from error
    required = (
        "device_type",
        "device_index",
        "gpu_name",
        "framework_version",
        "torch_cuda_version_reported",
        "device_count",
        "capability_major",
        "capability_minor",
        "cpu_fallback",
    )
    values = {key: report.get(key) for key in required}
    if not all(value is not None for value in values.values()):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_CUDA_PREFLIGHT")
    try:
        preflight = task4_cuda.CudaPreflightResultV1(
            device_type=values["device_type"],  # type: ignore[arg-type]
            device_index=values["device_index"],  # type: ignore[arg-type]
            gpu_name=values["gpu_name"],  # type: ignore[arg-type]
            framework_version=values["framework_version"],  # type: ignore[arg-type]
            torch_cuda_version_reported=values["torch_cuda_version_reported"],  # type: ignore[arg-type]
            device_count=values["device_count"],  # type: ignore[arg-type]
            capability_major=values["capability_major"],  # type: ignore[arg-type]
            capability_minor=values["capability_minor"],  # type: ignore[arg-type]
            actual_optimizer_steps=0,
            cpu_fallback=values["cpu_fallback"],  # type: ignore[arg-type]
            _attestation=task4_cuda._CUDA_PREFLIGHT_ATTESTATION,
        )
        if preflight.cuda_preflight_identity != report.get("cuda_preflight_identity"):
            raise Task4BAcceptanceRecoveryError(
                "ORIGINAL_CUDA_PREFLIGHT_IDENTITY_MISMATCH"
            )
    except (task4_cuda.CudaPreflightError, codec.CodecError, TypeError, ValueError) as error:
        if isinstance(error, Task4BAcceptanceRecoveryError):
            raise
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_CUDA_PREFLIGHT"
        ) from error
    return preflight


def _reconstruct_historical_completion_receipt(
    checkpoint_bytes: bytes,
    checkpoint: codec.CheckpointArtifactV1,
    receipt: Mapping[str, object],
) -> task4_inference.Task4BCompletionReceiptV1:
    try:
        exported = task4_inference.ExportedCheckpointV1(
            artifact_bytes=checkpoint_bytes,
            artifact=checkpoint,
            _attestation=task4_inference._CANONICAL_EXPORT_ATTESTATION,
        )
        reconstructed = task4_inference.Task4BCompletionReceiptV1(
            checkpoint_identity=receipt["checkpoint_identity"],  # type: ignore[arg-type]
            model_input_identity=receipt["model_input_identity"],  # type: ignore[arg-type]
            ordered_candidate_domain_identity=receipt[
                "ordered_candidate_domain_identity"
            ],  # type: ignore[arg-type]
            request_identity=receipt["request_identity"],  # type: ignore[arg-type]
            response_identity=receipt["response_identity"],  # type: ignore[arg-type]
            _exported_checkpoint=exported,
            _binding_identity="",
            _attestation=task4_inference._COMPLETION_RECEIPT_ATTESTATION,
        )
        reconstructed = dataclasses.replace(
            reconstructed,
            _binding_identity=task4_inference._completion_receipt_binding_identity(
                reconstructed
            ),
        )
        return task4_inference.validate_task4b_completion_receipt(reconstructed)
    except (
        KeyError,
        codec.CodecError,
        task4_inference.Task4InferenceError,
        TypeError,
        ValueError,
    ) as error:
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_RECEIPT_BINDING"
        ) from error


def _ordered_historical_train_samples(
    corpus: codec.DerivedCorpusV1,
) -> tuple[codec.CorpusSampleV1, ...]:
    return tuple(
        sorted(
            (sample for sample in corpus.samples if sample.partition == "train"),
            key=lambda sample: sample.bc_sample_identity.encode("utf-8"),
        )
    )


def _reconstruct_historical_request(
    corpus: codec.DerivedCorpusV1,
    checkpoint_identity: str,
) -> codec.InferenceRequestV1:
    train_samples = _ordered_historical_train_samples(corpus)
    if not train_samples:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_TRAIN_PARTITION")
    sample = train_samples[0]
    try:
        model_input = codec.make_numeric_model_input(
            model_input_identity=sample.model_input_identity,
            state_rows=sample.state_rows,
            candidate_rows=sample.candidate_rows,
            routing_keys=sample.routing_keys,
            public_candidate_domain_digest=sample.public_candidate_domain_digest,
            public_semantic_decision_id=sample.public_semantic_decision_id,
            perspective_player=sample.perspective_player,
            decision_index=sample.decision_index,
        )
        return codec.make_inference_request(
            checkpoint_identity=checkpoint_identity,
            model_input=model_input,
        )
    except (codec.CodecError, TypeError, ValueError) as error:
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_REQUEST_BINDING"
        ) from error


def _reconstruct_historical_artifacts(
    contents: Mapping[str, bytes],
    execution_report: Mapping[str, object],
    verification_report: Mapping[str, object],
    acceptance_report: Mapping[str, object],
    receipt_projection: Mapping[str, object],
    corpus: codec.DerivedCorpusV1,
    checkpoint: codec.CheckpointArtifactV1,
    checkpoint_identity: str,
    smoke_evidence_identity: str,
) -> _ValidatedHistoricalArtifacts:
    preflight = _reconstruct_historical_preflight(execution_report)
    train_count = sum(sample.partition == "train" for sample in corpus.samples)
    validation_count = sum(
        sample.partition == "validation" for sample in corpus.samples
    )
    test_count = sum(sample.partition == "test" for sample in corpus.samples)
    if (
        execution_report.get("source_dataset_identity")
        != corpus.source_dataset_identity
        or execution_report.get("dataset_split_identity") != corpus.split_identity
        or execution_report.get("card_vocabulary_identity")
        != corpus.card_vocabulary_identity
        or execution_report.get("train_sample_count") != train_count
        or execution_report.get("validation_sample_count") != validation_count
        or execution_report.get("test_sample_count") != test_count
    ):
        raise Task4BAcceptanceRecoveryError("ORIGINAL_CORPUS_METADATA_MISMATCH")
    checkpoint_manifest = checkpoint.manifest
    if (
        checkpoint_manifest.dataset_identity != corpus.source_dataset_identity
        or checkpoint_manifest.dataset_split_identity != corpus.split_identity
        or checkpoint_manifest.card_vocabulary_identity
        != corpus.card_vocabulary_identity
        or checkpoint_manifest.training_contract_identity
        != "ocgforge.phase6.bc_contract.v1"
    ):
        raise Task4BAcceptanceRecoveryError("ORIGINAL_CHECKPOINT_BINDING_MISMATCH")
    try:
        base_manifest = codec.default_training_run_manifest(
            source_dataset_identity=corpus.source_dataset_identity,
            dataset_split_identity=corpus.split_identity,
            card_vocabulary_identity=corpus.card_vocabulary_identity,
            training_code_commit=H_SMOKE_EXEC,
            actual_optimizer_steps=0,
        )
        training_run_manifest = task4_cuda.finalize_training_run_manifest_from_cuda_preflight(
            preflight,
            base_manifest,
            final_exported_checkpoint_identity=checkpoint_identity,
        )
        manifest_bytes = codec.canonical_training_run_manifest_bytes(
            training_run_manifest
        )
    except (codec.CodecError, task4_cuda.CudaPreflightError, TypeError, ValueError) as error:
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_TRAINING_MANIFEST"
        ) from error
    if manifest_bytes != contents["docs/p6/task4b/training-run-manifest.p6m"]:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_TRAINING_MANIFEST_MISMATCH")
    training_run_identity = codec.training_run_identity(training_run_manifest)
    reconstructed_receipt = _reconstruct_historical_completion_receipt(
        contents["docs/p6/task4b/checkpoint.p6k"],
        checkpoint,
        receipt_projection,
    )
    request = _reconstruct_historical_request(corpus, checkpoint_identity)
    first_train_sample = _ordered_historical_train_samples(corpus)[0]
    if (
        receipt_projection.get("checkpoint_identity") != checkpoint_identity
        or receipt_projection.get("model_input_identity")
        != first_train_sample.model_input_identity
        or receipt_projection.get("ordered_candidate_domain_identity")
        != first_train_sample.ordered_candidate_domain_identity
        or receipt_projection.get("request_identity") != request.request_identity
        or reconstructed_receipt.checkpoint_identity != checkpoint_identity
        or not reconstructed_receipt.fresh_checkpoint_reload
        or not reconstructed_receipt.deterministic_frozen_inference
    ):
        raise Task4BAcceptanceRecoveryError("ORIGINAL_RECEIPT_BINDING_MISMATCH")
    try:
        smoke_evidence = task4_cuda.smoke_evidence_from_cuda_preflight(
            preflight,
            training_run_manifest,
            reconstructed_receipt,
            actual_optimizer_steps=execution_report["actual_optimizer_steps"],  # type: ignore[arg-type]
            gpu_memory_before=execution_report["GPU_MEMORY_BEFORE"],  # type: ignore[arg-type]
            gpu_memory_peak=execution_report["GPU_MEMORY_PEAK"],  # type: ignore[arg-type]
            gpu_memory_after=execution_report["GPU_MEMORY_AFTER"],  # type: ignore[arg-type]
        )
        smoke_bytes = codec.canonical_smoke_evidence_bytes(smoke_evidence)
        rebuilt_smoke_identity = codec.smoke_evidence_identity(smoke_evidence)
    except (codec.CodecError, task4_cuda.CudaPreflightError, TypeError, ValueError) as error:
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_SMOKE_EVIDENCE"
        ) from error
    if (
        smoke_bytes != contents["docs/p6/task4b/smoke-evidence.p6e"]
        or rebuilt_smoke_identity != HISTORICAL_SMOKE_EVIDENCE_IDENTITY
        or rebuilt_smoke_identity != smoke_evidence_identity
        or smoke_evidence.training_run_identity != training_run_identity
        or smoke_evidence.cuda_preflight_identity
        != execution_report.get("cuda_preflight_identity")
    ):
        raise Task4BAcceptanceRecoveryError("ORIGINAL_SMOKE_EVIDENCE_MISMATCH")
    return _ValidatedHistoricalArtifacts(
        contents=contents,
        execution_report=execution_report,
        verification_report=verification_report,
        acceptance_report=acceptance_report,
        corpus=corpus,
        checkpoint=checkpoint,
        training_run_manifest=training_run_manifest,
        smoke_evidence=smoke_evidence,
        completion_receipt=reconstructed_receipt,
    )


def _validate_historical_gate_report(
    report: Mapping[str, object],
) -> None:
    commands = report.get("commands")
    gate_statuses = report.get("gate_statuses")
    if not isinstance(commands, list) or len(commands) != 14:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
    if (
        not isinstance(gate_statuses, dict)
        or set(gate_statuses) != set(verifier.REQUIRED_GATE_IDS)
        or any(
            gate_statuses[gate_id] not in {"PASS", "FAIL"}
            for gate_id in verifier.REQUIRED_GATE_IDS
        )
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
    command_counts = {gate_id: 0 for gate_id in verifier.REQUIRED_GATE_IDS}
    actual_command_ids: list[str] = []
    for command in commands:
        if not isinstance(command, dict):
            raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
        if set(command) != {
            "command_id",
            "argv",
            "exit_code",
            "stdout_sha256",
            "stderr_sha256",
            "status",
        }:
            raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
        command_id = command.get("command_id")
        argv = command.get("argv")
        if (
            not isinstance(command_id, str)
            or command_id not in command_counts
            or not isinstance(argv, list)
            or not argv
            or not all(isinstance(value, str) for value in argv)
            or not isinstance(command.get("exit_code"), int)
            or isinstance(command.get("exit_code"), bool)
            or not isinstance(command.get("stdout_sha256"), str)
            or _HEX64.fullmatch(command["stdout_sha256"]) is None
            or not isinstance(command.get("stderr_sha256"), str)
            or _HEX64.fullmatch(command["stderr_sha256"]) is None
            or command.get("status") not in {"PASS", "FAIL"}
        ):
            raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
        command_counts[command_id] += 1
        actual_command_ids.append(command_id)
    expected_command_ids = [
        command.command_id
        for command in verifier._fixed_gate_commands(
            build_dir=Path("C:/historical-build"),
            probe_path=Path("C:/historical-build/phase6_task4_corpus_probe.exe"),
            h_exec=H_SMOKE_EXEC,
        )
    ]
    if actual_command_ids != expected_command_ids:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
    expected_counts = {
        gate_id: 0 for gate_id in verifier.REQUIRED_GATE_IDS
    }
    for command in verifier._fixed_gate_commands(
        build_dir=Path("C:/historical-build"),
        probe_path=Path("C:/historical-build/phase6_task4_corpus_probe.exe"),
        h_exec=H_SMOKE_EXEC,
    ):
        expected_counts[command.command_id] += 1
    if command_counts != expected_counts:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
    focused_commands = [
        command
        for command in commands
        if command.get("command_id") == "task4-focused-python"
    ]
    if (
        len(focused_commands) != 1
        or focused_commands[0]["argv"].count(
            "tests.phase6.phase6_task4b_runner_test"
        )
        != 1
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
    if gate_statuses.get("full-non-long-ctest") != "FAIL":
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_FAILURE_GATE")
    if any(
        gate_statuses.get(gate_id) != "PASS"
        for gate_id in verifier.REQUIRED_GATE_IDS
        if gate_id != "full-non-long-ctest"
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")
    full_commands = [
        command
        for command in commands
        if command.get("command_id") == "full-non-long-ctest"
    ]
    if (
        len(full_commands) != 1
        or full_commands[0].get("exit_code") != 8
        or full_commands[0].get("status") != "FAIL"
        or any(
            command.get("status") != "PASS" or command.get("exit_code") != 0
            for command in commands
            if command.get("command_id") != "full-non-long-ctest"
        )
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_FAILURE_GATE")


def _load_historical_evidence(source_root: Path) -> _HistoricalEvidence:
    contents: dict[str, bytes] = {}
    for relative, expected_sha256 in HISTORICAL_FILE_SHA256.items():
        data = _git_object_bytes(source_root, H_FAILURE_EVIDENCE, relative)
        contents[relative] = data
        if _sha256_bytes(data) != expected_sha256:
            raise Task4BAcceptanceRecoveryError("HISTORICAL_FILE_HASH_MISMATCH")

    execution_report = _parse_canonical_json(
        contents["docs/p6/task4b/task4b-execution-report.json"]
    )
    verification_report = _parse_canonical_json(
        contents["docs/p6/task4b/task4b-verification.json"]
    )
    acceptance_report = _parse_canonical_json(
        contents["docs/p6/task4b/task4b-acceptance.json"]
    )
    receipt = _parse_canonical_json(contents["docs/p6/task4b/completion-receipt.json"])
    try:
        verifier._validate_execution_report(execution_report)
    except verifier.Task4BVerificationError as error:
        raise Task4BAcceptanceRecoveryError(
            "INVALID_HISTORICAL_EXECUTION_REPORT"
        ) from error

    checkpoint_identity = execution_report.get("checkpoint_identity")
    smoke_evidence_identity = execution_report.get("smoke_evidence_identity")
    probe_sha256 = execution_report.get("corpus_probe_sha256")
    if not all(
        isinstance(value, str)
        for value in (checkpoint_identity, smoke_evidence_identity, probe_sha256)
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_IDENTITIES")
    _validate_original_report_status(
        execution_report,
        expected_checkpoint_identity=HISTORICAL_CHECKPOINT_IDENTITY,
        expected_smoke_evidence_identity=HISTORICAL_SMOKE_EVIDENCE_IDENTITY,
        expected_probe_sha256=HISTORICAL_PROBE_SHA256,
    )

    if verification_report.get("schema_id") != verifier.VERIFICATION_SCHEMA_ID:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_VERIFICATION")
    if acceptance_report.get("schema_id") != verifier.ACCEPTANCE_SCHEMA_ID:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_ACCEPTANCE")
    for report in (verification_report, acceptance_report):
        if (
            report.get("H_exec") != H_SMOKE_EXEC
            or report.get("corpus_probe_sha256") != HISTORICAL_PROBE_SHA256
            or report.get("corpus_probe_source_commit") != H_SMOKE_EXEC
            or report.get("checkpoint_identity") != HISTORICAL_CHECKPOINT_IDENTITY
            or report.get("smoke_evidence_identity")
            != HISTORICAL_SMOKE_EVIDENCE_IDENTITY
            or report.get("SMOKE_PASS") is not True
            or report.get("TASK4B_PASS") is not False
            or report.get("execution_report_sha256")
            != HISTORICAL_FILE_SHA256["docs/p6/task4b/task4b-execution-report.json"]
        ):
            raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_ACCEPTANCE")
        _validate_historical_gate_report(report)

    expected_receipt_keys = {
        "checkpoint_identity",
        "model_input_identity",
        "ordered_candidate_domain_identity",
        "request_identity",
        "response_identity",
        "fresh_checkpoint_reload",
        "deterministic_frozen_inference",
    }
    if (
        set(receipt) != expected_receipt_keys
        or receipt.get("checkpoint_identity") != checkpoint_identity
        or receipt.get("fresh_checkpoint_reload") is not True
        or receipt.get("deterministic_frozen_inference") is not True
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_RECEIPT")

    try:
        corpus = codec.decode_corpus_artifact(
            contents["docs/p6/task4b/corpus.p6c"]
        )
        authority = codec.decode_corpus_authority_artifact(
            contents["docs/p6/task4b/corpus.authority.p6a"]
        )
        admitted_corpus = codec.admit_corpus_artifact(
            contents["docs/p6/task4b/corpus.p6c"], authority
        )
        checkpoint = codec.decode_checkpoint_artifact(
            contents["docs/p6/task4b/checkpoint.p6k"]
        )
    except (codec.CodecError, TypeError, ValueError) as error:
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_ARTIFACT") from error
    if checkpoint.checkpoint_identity != checkpoint_identity:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_CHECKPOINT_MISMATCH")
    if admitted_corpus != corpus:
        raise Task4BAcceptanceRecoveryError("ORIGINAL_CORPUS_MISMATCH")
    if corpus.source_dataset_identity != execution_report.get("source_dataset_identity"):
        raise Task4BAcceptanceRecoveryError("ORIGINAL_CORPUS_MISMATCH")
    validated_artifacts = _reconstruct_historical_artifacts(
        contents,
        execution_report,
        verification_report,
        acceptance_report,
        receipt,
        corpus,
        checkpoint,
        checkpoint_identity,
        smoke_evidence_identity,
    )

    return _HistoricalEvidence(
        h_smoke_exec=H_SMOKE_EXEC,
        h_failure_evidence=H_FAILURE_EVIDENCE,
        original_execution_report_sha256=HISTORICAL_FILE_SHA256[
            "docs/p6/task4b/task4b-execution-report.json"
        ],
        original_verification_report_sha256=HISTORICAL_FILE_SHA256[
            "docs/p6/task4b/task4b-verification.json"
        ],
        original_acceptance_report_sha256=HISTORICAL_FILE_SHA256[
            "docs/p6/task4b/task4b-acceptance.json"
        ],
        original_checkpoint_identity=HISTORICAL_CHECKPOINT_IDENTITY,
        original_smoke_evidence_identity=HISTORICAL_SMOKE_EVIDENCE_IDENTITY,
        original_probe_sha256=HISTORICAL_PROBE_SHA256,
        original_corpus_probe_source_commit=H_SMOKE_EXEC,
        original_smoke_pass=True,
        original_task4b_pass=False,
        original_failed_gate_id="full-non-long-ctest",
        original_failed_gate_exit_code=8,
        original_command_record_count=14,
        file_sha256=dict(HISTORICAL_FILE_SHA256),
        validated_artifacts=validated_artifacts,
    )


def _git_diff_paths(source_root: Path, base: str, head: str) -> tuple[str, ...]:
    arguments = ["diff", "--name-only", base, head, "--", "."]
    if base == H_SMOKE_EXEC and head == H_VERIFIER_FIX:
        arguments.append(":(exclude)docs/p6/task4b/**")
    output = _run_git_text(source_root, *arguments)
    return tuple(sorted((line for line in output.splitlines() if line), key=lambda value: value.encode("utf-8")))


def _git_is_ancestor(source_root: Path, base: str, head: str) -> bool:
    try:
        completed = subprocess.run(
            (
                "git",
                "-C",
                str(source_root),
                "merge-base",
                "--is-ancestor",
                base,
                head,
            ),
            shell=False,
            check=False,
            capture_output=True,
        )
    except OSError as error:
        raise Task4BAcceptanceRecoveryError("GIT_COMMAND_FAILED") from error
    return completed.returncode == 0


def _git_blob_id(source_root: Path, commit: str, relative: str) -> str:
    try:
        value = _run_git_text(source_root, "rev-parse", f"{commit}:{relative}").strip()
    except Task4BAcceptanceRecoveryError as error:
        return "<missing>"
    return value


def _protected_semantic_diff_sha256(
    source_root: Path,
    base: str,
    head: str,
    paths: Sequence[str],
) -> str:
    body = bytearray()
    for relative in sorted(paths, key=lambda value: value.encode("utf-8")):
        body.extend(relative.encode("utf-8"))
        body.extend(b"\0")
        body.extend(_git_blob_id(source_root, base, relative).encode("ascii"))
        body.extend(b"\0")
        body.extend(_git_blob_id(source_root, head, relative).encode("ascii"))
        body.extend(b"\n")
    return _sha256_bytes(bytes(body))


def _prove_source_integrity(
    source_root: Path,
    recovery_head: str,
) -> SemanticIntegrityProofV1:
    if not _git_is_ancestor(source_root, RECOVERY_CONTRACT_COMMIT, recovery_head):
        raise Task4BAcceptanceRecoveryError("RECOVERY_HEAD_NOT_FROM_CONTRACT")
    observed_verifier = _git_diff_paths(source_root, H_SMOKE_EXEC, H_VERIFIER_FIX)
    unexpected_verifier = set(observed_verifier) - EXPECTED_VERIFIER_FIX_PATHS
    if unexpected_verifier:
        raise Task4BAcceptanceRecoveryError("UNEXPECTED_VERIFIER_FIX_PATH")
    if set(observed_verifier) != EXPECTED_VERIFIER_FIX_PATHS:
        raise Task4BAcceptanceRecoveryError("VERIFIER_FIX_PATH_MISMATCH")

    observed_recovery = _git_diff_paths(
        source_root, RECOVERY_CONTRACT_COMMIT, recovery_head
    )
    unexpected_recovery = set(observed_recovery) - EXPECTED_RECOVERY_SOURCE_PATHS
    if unexpected_recovery:
        raise Task4BAcceptanceRecoveryError("UNEXPECTED_RECOVERY_SOURCE_PATH")
    if set(observed_recovery) != EXPECTED_RECOVERY_SOURCE_PATHS:
        raise Task4BAcceptanceRecoveryError("RECOVERY_SOURCE_PATH_MISMATCH")

    protected = tuple(
        path for path in observed_verifier if path not in EXPECTED_VERIFIER_FIX_PATHS
    )
    protected_hash = _protected_semantic_diff_sha256(
        source_root,
        H_SMOKE_EXEC,
        H_VERIFIER_FIX,
        protected,
    )
    if protected:
        raise Task4BAcceptanceRecoveryError("PROTECTED_SEMANTIC_SOURCE_CHANGED")
    return SemanticIntegrityProofV1(
        comparison_base_commit=H_SMOKE_EXEC,
        verifier_fix_commit=H_VERIFIER_FIX,
        observed_non_evidence_paths=tuple(observed_verifier),
        expected_verifier_fix_paths=tuple(
            sorted(EXPECTED_VERIFIER_FIX_PATHS, key=lambda value: value.encode("utf-8"))
        ),
        protected_semantic_diff_paths=(),
        protected_semantic_diff_sha256=protected_hash,
        recovery_source_paths=tuple(observed_recovery),
        expected_recovery_source_paths=tuple(
            sorted(EXPECTED_RECOVERY_SOURCE_PATHS, key=lambda value: value.encode("utf-8"))
        ),
        rules_deck_teacher_phase5_unchanged=True,
    )


def _make_provenance(recovery_head: str) -> RecoveryProvenanceV1:
    if _HEX40.fullmatch(recovery_head) is None:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_HEAD")
    return RecoveryProvenanceV1(
        training_code_commit=H_SMOKE_EXEC,
        failed_evidence_commit=H_FAILURE_EVIDENCE,
        verifier_fix_commit=H_VERIFIER_FIX,
        recovery_contract_commit=RECOVERY_CONTRACT_COMMIT,
        recovery_verifier_source_commit=recovery_head,
    )


def _read_cmake_cache(build_dir: Path) -> dict[str, str]:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        raise Task4BAcceptanceRecoveryError("MISSING_CMAKE_CACHE")
    values: dict[str, str] = {}
    try:
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            match = re.fullmatch(r"([^:]+):[^=]*=(.*)", line)
            if match is not None:
                values[match.group(1)] = match.group(2)
    except (OSError, UnicodeError) as error:
        raise Task4BAcceptanceRecoveryError("INVALID_CMAKE_CACHE") from error
    return values


def _same_path(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def _validate_build_and_probe(build_dir: Path, source_root: Path) -> Path:
    build_dir = build_dir.resolve()
    source_root = source_root.resolve()
    values = _read_cmake_cache(build_dir)
    if not _same_path(Path(values.get("CMAKE_HOME_DIRECTORY", "")), source_root):
        raise Task4BAcceptanceRecoveryError("STALE_BUILD_SOURCE")
    if values.get("CMAKE_GENERATOR") != "Ninja":
        raise Task4BAcceptanceRecoveryError("STALE_BUILD_SOURCE")
    if values.get("CMAKE_BUILD_TYPE") != "Release":
        raise Task4BAcceptanceRecoveryError("STALE_BUILD_SOURCE")
    executable_name = (
        "phase6_task4_corpus_probe.exe"
        if os.name == "nt"
        else "phase6_task4_corpus_probe"
    )
    candidates = tuple(
        sorted(
            (
                path.resolve()
                for path in build_dir.rglob(executable_name)
                if path.is_file()
            ),
            key=lambda path: str(path).encode("utf-8"),
        )
    )
    if len(candidates) == 0:
        raise Task4BAcceptanceRecoveryError("MISSING_PROBE_BINARY")
    if len(candidates) != 1:
        raise Task4BAcceptanceRecoveryError("AMBIGUOUS_PROBE_BINARY")
    probe = candidates[0]
    try:
        probe.relative_to(build_dir)
    except ValueError as error:
        raise Task4BAcceptanceRecoveryError("PROBE_OUTSIDE_BUILD") from error
    if _sha256_file(probe) != HISTORICAL_PROBE_SHA256:
        raise Task4BAcceptanceRecoveryError("PROBE_HASH_MISMATCH")
    return probe


def _git_head(source_root: Path) -> str:
    head = _run_git_text(source_root, "rev-parse", "HEAD").strip()
    if _HEX40.fullmatch(head) is None:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_HEAD")
    return head


def _git_changed_paths(source_root: Path) -> tuple[str, ...]:
    paths: list[str] = []
    for arguments in (
        ("diff", "--name-only", "--diff-filter=ACMRTUXB"),
        ("diff", "--cached", "--name-only", "--diff-filter=ACMRTUXB"),
    ):
        paths.extend(
            line
            for line in _run_git_text(source_root, *arguments).splitlines()
            if line
        )
    return tuple(paths)


def _git_status_lines(source_root: Path) -> tuple[str, ...]:
    return tuple(
        line
        for line in _run_git_text(
            source_root,
            "status",
            "--porcelain",
            "--untracked-files=all",
        ).splitlines()
        if line
    )


def _verify_recovery_worktree(
    *,
    source_root: Path,
    expected_head: str,
    output_dir: Path,
) -> None:
    del output_dir
    if _git_head(source_root) != expected_head:
        raise Task4BAcceptanceRecoveryError("RECOVERY_HEAD_CHANGED")
    if _git_changed_paths(source_root):
        raise Task4BAcceptanceRecoveryError("RECOVERY_SOURCE_CHANGED")
    if _git_status_lines(source_root):
        raise Task4BAcceptanceRecoveryError("RECOVERY_WORKTREE_DIRTY")


def _recovery_gate_commands(
    build_dir: Path,
    probe_path: Path,
    h_exec: str,
) -> tuple[_RecoveryGateCommand, ...]:
    commands = verifier._fixed_gate_commands(
        build_dir=build_dir,
        probe_path=probe_path,
        h_exec=h_exec,
    )
    return tuple(
        _RecoveryGateCommand(
            command_id=command.command_id,
            argv=command.argv,
            probe_dependent=(
                command.probe_dependent
                or command.command_id == "full-non-long-ctest"
            ),
        )
        for command in commands
    )


def _not_run_record(command: _RecoveryGateCommand) -> RecoveryCommandRecordV1:
    return RecoveryCommandRecordV1(
        command_id=command.command_id,
        argv=command.argv,
        exit_code=None,
        stdout_sha256=None,
        stderr_sha256=None,
        status="NOT_RUN",
    )


def _complete_command_records(
    commands: Sequence[_RecoveryGateCommand],
    records: Sequence[RecoveryCommandRecordV1],
) -> tuple[RecoveryCommandRecordV1, ...]:
    if len(records) > len(commands):
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_COMMAND_RECORDS")
    completed: list[RecoveryCommandRecordV1] = []
    for index, record in enumerate(records):
        command = commands[index]
        if record.command_id != command.command_id or record.argv != command.argv:
            raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_COMMAND_RECORDS")
        completed.append(record)
    completed.extend(
        _not_run_record(command) for command in commands[len(records):]
    )
    return tuple(completed)


def _derive_recovery_gate_statuses(
    commands: Sequence[_RecoveryGateCommand],
    records: Sequence[RecoveryCommandRecordV1],
) -> dict[str, str]:
    if len(commands) != 14 or len(records) != len(commands):
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_COMMAND_RECORDS")
    for index, (command, record) in enumerate(zip(commands, records)):
        del index
        if record.command_id != command.command_id or record.argv != command.argv:
            raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_COMMAND_RECORDS")
    statuses: dict[str, str] = {}
    for gate_id in verifier.REQUIRED_GATE_IDS:
        planned = [
            index for index, command in enumerate(commands)
            if command.command_id == gate_id
        ]
        started = [records[index] for index in planned if records[index].status != "NOT_RUN"]
        if not started:
            statuses[gate_id] = "NOT_RUN"
        elif any(record.status == "FAIL" for record in started):
            statuses[gate_id] = "FAIL"
        elif len(started) < len(planned):
            statuses[gate_id] = "FAIL"
        else:
            statuses[gate_id] = "PASS"
    return statuses


def _output_bytes(value: object) -> bytes:
    if value is None:
        return b""
    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        return value.encode("utf-8")
    return str(value).encode("utf-8")


def _run_recovery_command(
    command: _RecoveryGateCommand,
    source_root: Path,
) -> RecoveryCommandRecordV1:
    try:
        completed = subprocess.run(
            command.argv,
            cwd=source_root,
            shell=False,
            check=False,
            capture_output=True,
            text=False,
        )
        stdout = _output_bytes(completed.stdout)
        stderr = _output_bytes(completed.stderr)
        exit_code = int(completed.returncode)
        status = "PASS" if exit_code == 0 else "FAIL"
    except Exception as error:
        stdout = b""
        stderr = str(error).encode("utf-8")
        exit_code = -1
        status = "FAIL"
    return RecoveryCommandRecordV1(
        command_id=command.command_id,
        argv=command.argv,
        exit_code=exit_code,
        stdout_sha256=_sha256_bytes(stdout),
        stderr_sha256=_sha256_bytes(stderr),
        status=status,
    )


def _run_recovery_commands(
    commands: Sequence[_RecoveryGateCommand],
    *,
    source_root: Path,
    expected_head: str,
    output_dir: Path,
    probe_path: Path,
) -> tuple[RecoveryCommandRecordV1, ...]:
    records: list[RecoveryCommandRecordV1] = []
    ephemeral_probe_regressions = 0
    for command in commands:
        try:
            _verify_recovery_worktree(
                source_root=source_root,
                expected_head=expected_head,
                output_dir=output_dir,
            )
        except Task4BAcceptanceRecoveryError as error:
            raise Task4BAcceptanceRecoveryError(
                error.code,
                failure_stage="gate-execution",
                reached_command_count=len(records),
                records=records,
                ephemeral_probe_regressions=ephemeral_probe_regressions,
            ) from error
        if command.probe_dependent:
            try:
                if _sha256_file(probe_path) != HISTORICAL_PROBE_SHA256:
                    raise Task4BAcceptanceRecoveryError("PROBE_HASH_MISMATCH")
            except Task4BAcceptanceRecoveryError as error:
                raise Task4BAcceptanceRecoveryError(
                    error.code,
                    failure_stage="gate-execution",
                    reached_command_count=len(records),
                    records=records,
                    ephemeral_probe_regressions=ephemeral_probe_regressions,
                ) from error
        if command.probe_dependent:
            ephemeral_probe_regressions += 1
        records.append(_run_recovery_command(command, source_root))
        try:
            _verify_recovery_worktree(
                source_root=source_root,
                expected_head=expected_head,
                output_dir=output_dir,
            )
            if command.probe_dependent:
                if _sha256_file(probe_path) != HISTORICAL_PROBE_SHA256:
                    raise Task4BAcceptanceRecoveryError("PROBE_HASH_CHANGED")
        except Task4BAcceptanceRecoveryError as error:
            raise Task4BAcceptanceRecoveryError(
                error.code,
                failure_stage="post-gate-integrity",
                reached_command_count=len(records),
                records=records,
                ephemeral_probe_regressions=ephemeral_probe_regressions,
            ) from error
        if records[-1].status != "PASS":
            raise Task4BAcceptanceRecoveryError(
                "RECOVERY_COMMAND_FAILED",
                failure_stage="gate-execution",
                reached_command_count=len(records),
                records=records,
                ephemeral_probe_regressions=ephemeral_probe_regressions,
            )
    return tuple(records)


def _derive_recovery_pass(
    *,
    immutable_original: bool,
    exact_provenance: bool,
    semantic_source: bool,
    gates_pass: bool,
    cuda_smoke_rerun: bool,
    authoritative_probe_rerun: bool,
    additional_optimizer_steps: int,
    model_training_invocations: int,
    ephemeral_probe_regressions: int,
    evidence_mutation: bool,
    recovery_failure: RecoveryFailureV1 | None = None,
) -> bool:
    return (
        immutable_original
        and exact_provenance
        and semantic_source
        and gates_pass
        and not cuda_smoke_rerun
        and not authoritative_probe_rerun
        and additional_optimizer_steps == 0
        and model_training_invocations == 0
        and ephemeral_probe_regressions == 3
        and not evidence_mutation
        and recovery_failure is None
    )


def _derive_final_pass(
    *,
    original_smoke_pass: bool,
    original_task4b_pass: bool,
    immutable_original: bool,
    recovery_pass: bool,
) -> bool:
    return (
        original_smoke_pass is True
        and original_task4b_pass is False
        and immutable_original
        and recovery_pass is True
    )


def _markdown_from_evidence(evidence: Task4BAcceptanceRecoveryEvidenceV1) -> str:
    original = evidence.original_attempt
    lines = [
        "# Task4B Acceptance-Recovery V1",
        "",
        f"H_SMOKE_EXEC: {None if original is None else original.h_smoke_exec}",
        f"H_FAILURE_EVIDENCE: {None if original is None else original.h_failure_evidence}",
        f"training_code_commit: {None if evidence.provenance is None else evidence.provenance.training_code_commit}",
        f"failed_evidence_commit: {None if evidence.provenance is None else evidence.provenance.failed_evidence_commit}",
        f"verifier_fix_commit: {None if evidence.provenance is None else evidence.provenance.verifier_fix_commit}",
        f"recovery_verifier_source_commit: {None if evidence.provenance is None else evidence.provenance.recovery_verifier_source_commit}",
        f"ORIGINAL_SMOKE_PASS: {str(evidence.ORIGINAL_SMOKE_PASS).lower()}",
        f"ORIGINAL_TASK4B_PASS: {str(evidence.ORIGINAL_TASK4B_PASS).lower()}",
        f"TASK4B_RECOVERY_PASS: {str(evidence.TASK4B_RECOVERY_PASS).lower()}",
        f"TASK4B_FINAL_PASS: {str(evidence.TASK4B_FINAL_PASS).lower()}",
        f"recovery_failure: {None if evidence.recovery_failure is None else dataclasses.asdict(evidence.recovery_failure)}",
        "",
        "| gate | status |",
        "| --- | --- |",
    ]
    lines.extend(
        f"| {gate_id} | {status} |"
        for gate_id, status in evidence.recovery_gate_statuses
    )
    lines.extend(
        (
            "",
            "| command_id | exit_code | status |",
            "| --- | ---: | --- |",
        )
    )
    lines.extend(
        f"| {record.command_id} | {record.exit_code} | {record.status} |"
        for record in evidence.recovery_command_records
    )
    return "\n".join(lines) + "\n"


def _fsync_directory_if_supported(path: Path) -> bool:
    if os.name == "nt":
        return False
    directory_flags = os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
    try:
        descriptor = os.open(str(path), directory_flags)
    except OSError as error:
        if error.errno in {
            errno.EINVAL,
            errno.ENOTDIR,
            errno.ENOSYS,
            errno.ENOTSUP,
            getattr(errno, "EOPNOTSUPP", errno.ENOTSUP),
        }:
            return False
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        ) from error
    try:
        try:
            os.fsync(descriptor)
        except OSError as error:
            raise Task4BAcceptanceRecoveryError(
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
            ) from error
    finally:
        os.close(descriptor)
    return True


def _write_staged_file(path: Path, data: bytes) -> None:
    try:
        with path.open("xb") as stream:
            stream.write(bytes(data))
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as error:
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        ) from error
    try:
        if path.read_bytes() != bytes(data):
            raise Task4BAcceptanceRecoveryError(
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
            )
    except OSError as error:
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        ) from error


def _publish_recovery_evidence(
    output_dir: Path,
    evidence: Task4BAcceptanceRecoveryEvidenceV1,
    *,
    markdown: str | None = None,
) -> None:
    output_dir = output_dir.resolve()
    if output_dir.name != Path(RECOVERY_OUTPUT_DIRECTORY).name:
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        )
    if output_dir.exists():
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        )
    parent = output_dir.parent
    try:
        parent.mkdir(parents=True, exist_ok=True)
        json_bytes = evidence.to_json().encode("utf-8")
        parsed_json = _parse_canonical_json(json_bytes)
        if parsed_json.get("schema_id") != RECOVERY_SCHEMA_ID:
            raise Task4BAcceptanceRecoveryError(
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
            )
        derived_markdown = _markdown_from_evidence(evidence)
        if markdown is not None and markdown != derived_markdown:
            raise Task4BAcceptanceRecoveryError(
                "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
            )
        markdown_bytes = derived_markdown.encode("utf-8")
        staging_dir = Path(
            tempfile.mkdtemp(prefix=".recovery-v1-", dir=str(parent))
        )
        renamed = False
        try:
            _write_staged_file(
                staging_dir / RECOVERY_JSON_FILENAME,
                json_bytes,
            )
            _write_staged_file(
                staging_dir / RECOVERY_MARKDOWN_FILENAME,
                markdown_bytes,
            )
            _fsync_directory_if_supported(staging_dir)
            if output_dir.exists():
                raise Task4BAcceptanceRecoveryError(
                    "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
                )
            os.replace(staging_dir, output_dir)
            renamed = True
            _fsync_directory_if_supported(parent)
            final_json = (output_dir / RECOVERY_JSON_FILENAME).read_bytes()
            final_markdown = (output_dir / RECOVERY_MARKDOWN_FILENAME).read_bytes()
            if final_json != json_bytes or final_markdown != markdown_bytes:
                raise Task4BAcceptanceRecoveryError(
                    "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
                )
            parsed_final_json = _parse_canonical_json(final_json)
            if parsed_final_json.get("schema_id") != RECOVERY_SCHEMA_ID:
                raise Task4BAcceptanceRecoveryError(
                    "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
                )
        finally:
            if not renamed and staging_dir.exists():
                shutil.rmtree(staging_dir, ignore_errors=True)
    except Task4BAcceptanceRecoveryError as error:
        if error.code == "RECOVERY_EVIDENCE_PUBLICATION_FAILED":
            raise
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        ) from error
    except (OSError, TypeError, UnicodeError, ValueError) as error:
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        ) from error


@dataclasses.dataclass
class _RecoveryAttemptState:
    planned_commands: tuple[_RecoveryGateCommand, ...]
    recovery_head: str | None = None
    historical: _HistoricalEvidence | None = None
    provenance: RecoveryProvenanceV1 | None = None
    semantic_integrity_proof: SemanticIntegrityProofV1 | None = None
    records: tuple[RecoveryCommandRecordV1, ...] = ()
    reached_command_count: int = 0
    ephemeral_probe_regressions: int = 0
    failure_stage: str = "historical-evidence-validation"


def _zero_recovery_execution(
    *,
    ephemeral_probe_regressions: int = 0,
) -> RecoveryExecutionV1:
    return RecoveryExecutionV1(
        CUDA_SMOKE_RERUN=False,
        AUTHORITATIVE_CORPUS_PROBE_RERUN=False,
        ADDITIONAL_OPTIMIZER_STEPS=0,
        MODEL_TRAINING_INVOCATIONS=0,
        EPHEMERAL_PROBE_REGRESSION_INVOCATIONS=ephemeral_probe_regressions,
        EVIDENCE_MUTATION=False,
    )


def _failure_with_context(
    error: Task4BAcceptanceRecoveryError,
    state: _RecoveryAttemptState,
) -> Task4BAcceptanceRecoveryError:
    records = error.records or state.records
    reached = len(records)
    if error.failure_stage not in _RECOVERY_FAILURE_STAGES:
        failure_stage = state.failure_stage
    else:
        failure_stage = error.failure_stage
    return Task4BAcceptanceRecoveryError(
        error.code,
        failure_stage=failure_stage,
        reached_command_count=reached,
        records=records,
        ephemeral_probe_regressions=max(
            state.ephemeral_probe_regressions,
            error.ephemeral_probe_regressions,
        ),
    )


def _failure_evaluation(
    state: _RecoveryAttemptState,
    error: Task4BAcceptanceRecoveryError,
) -> Task4BAcceptanceRecoveryEvidenceV1:
    contextual = _failure_with_context(error, state)
    records = _complete_command_records(state.planned_commands, contextual.records)
    statuses = _derive_recovery_gate_statuses(state.planned_commands, records)
    historical = state.historical
    failure = RecoveryFailureV1(
        error_code=contextual.code,
        failure_stage=contextual.failure_stage or state.failure_stage,
        reached_command_count=len(contextual.records),
    )
    execution = _zero_recovery_execution(
        ephemeral_probe_regressions=contextual.ephemeral_probe_regressions,
    )
    return Task4BAcceptanceRecoveryEvidenceV1(
        original_attempt=historical,
        provenance=state.provenance,
        semantic_integrity_proof=state.semantic_integrity_proof,
        recovery_execution=execution,
        recovery_gate_statuses=tuple(
            (gate_id, statuses[gate_id]) for gate_id in verifier.REQUIRED_GATE_IDS
        ),
        recovery_command_records=records,
        ORIGINAL_SMOKE_PASS=(
            None if historical is None else historical.original_smoke_pass
        ),
        ORIGINAL_TASK4B_PASS=(
            None if historical is None else historical.original_task4b_pass
        ),
        TASK4B_RECOVERY_PASS=False,
        TASK4B_FINAL_PASS=False,
        recovery_failure=failure,
    )


def run_acceptance_recovery(
    *,
    build_dir: Path,
    output_dir: Path | None = None,
) -> Task4BAcceptanceRecoveryEvidenceV1:
    """Run one separately authorized recovery attempt.

    This function is an API for a future recovery authorization. It is not
    called during the implementation task; importing this module has no side
    effects.
    """

    source_root = _canonical_source_root()
    expected_output = (source_root / RECOVERY_OUTPUT_DIRECTORY).resolve()
    requested_output = (
        expected_output if output_dir is None else Path(output_dir).resolve()
    )
    if requested_output != expected_output:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_OUTPUT_DIRECTORY")
    if requested_output.exists():
        raise Task4BAcceptanceRecoveryError(
            "RECOVERY_EVIDENCE_PUBLICATION_FAILED"
        )

    build_path = Path(build_dir).resolve()
    planned_probe_name = (
        "phase6_task4_corpus_probe.exe"
        if os.name == "nt"
        else "phase6_task4_corpus_probe"
    )
    planned_commands = _recovery_gate_commands(
        build_path,
        build_path / planned_probe_name,
        H_SMOKE_EXEC,
    )
    state = _RecoveryAttemptState(planned_commands=planned_commands)
    evaluation: Task4BAcceptanceRecoveryEvidenceV1
    try:
        state.failure_stage = "provenance-validation"
        recovery_head = _git_head(source_root)
        state.recovery_head = recovery_head

        state.failure_stage = "historical-evidence-validation"
        state.historical = _load_historical_evidence(source_root)

        state.failure_stage = "semantic-source-integrity"
        state.semantic_integrity_proof = _prove_source_integrity(
            source_root, recovery_head
        )
        state.failure_stage = "provenance-validation"
        state.provenance = _make_provenance(recovery_head)

        state.failure_stage = "semantic-source-integrity"
        _verify_recovery_worktree(
            source_root=source_root,
            expected_head=recovery_head,
            output_dir=requested_output,
        )

        state.failure_stage = "build-probe-binding"
        probe_path = _validate_build_and_probe(build_path, source_root)
        commands = _recovery_gate_commands(build_path, probe_path, H_SMOKE_EXEC)
        state.planned_commands = commands
        state.failure_stage = "gate-execution"
        if (
            len(commands) != 14
            or sum(command.probe_dependent for command in commands) != 3
        ):
            raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_GATE_PLAN")
        if sum(
            command.argv.count("tests.phase6.phase6_task4b_runner_test")
            for command in commands
            if command.command_id == "task4-focused-python"
        ) != 1:
            raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_GATE_PLAN")
        if any(
            "task4b_cuda_smoke.py" in command.argv
            or "run_task4b_smoke" in command.argv
            for command in commands
        ):
            raise Task4BAcceptanceRecoveryError("UNAUTHORIZED_SMOKE_COMMAND")

        records = _run_recovery_commands(
            commands,
            source_root=source_root,
            expected_head=recovery_head,
            output_dir=requested_output,
            probe_path=probe_path,
        )
        state.records = records
        state.reached_command_count = len(records)
        state.ephemeral_probe_regressions = sum(
            command.probe_dependent
            for command, record in zip(commands, records)
            if record.status != "NOT_RUN"
        )
        if any(record.status != "PASS" for record in records):
            raise Task4BAcceptanceRecoveryError(
                "RECOVERY_GATE_FAILED",
                failure_stage="gate-execution",
                reached_command_count=len(records),
                records=records,
                ephemeral_probe_regressions=state.ephemeral_probe_regressions,
            )

        state.failure_stage = "post-gate-integrity"
        _verify_recovery_worktree(
            source_root=source_root,
            expected_head=recovery_head,
            output_dir=requested_output,
        )
        complete_records = _complete_command_records(commands, records)
        gate_statuses = _derive_recovery_gate_statuses(commands, complete_records)
        recovery_gate_statuses = tuple(
            (gate_id, gate_statuses[gate_id])
            for gate_id in verifier.REQUIRED_GATE_IDS
        )
        execution = _zero_recovery_execution(
            ephemeral_probe_regressions=state.ephemeral_probe_regressions,
        )
        historical = state.historical
        proof = state.semantic_integrity_proof
        provenance = state.provenance
        if historical is None or proof is None or provenance is None:
            raise Task4BAcceptanceRecoveryError("INCOMPLETE_RECOVERY_STATE")
        recovery_pass = _derive_recovery_pass(
            immutable_original=historical.validated_artifacts is not None,
            exact_provenance=(
                provenance.training_code_commit == H_SMOKE_EXEC
                and provenance.failed_evidence_commit == H_FAILURE_EVIDENCE
                and provenance.verifier_fix_commit == H_VERIFIER_FIX
                and provenance.recovery_contract_commit == RECOVERY_CONTRACT_COMMIT
                and provenance.recovery_verifier_source_commit == recovery_head
            ),
            semantic_source=proof.rules_deck_teacher_phase5_unchanged,
            gates_pass=all(status == "PASS" for _, status in recovery_gate_statuses),
            cuda_smoke_rerun=execution.CUDA_SMOKE_RERUN,
            authoritative_probe_rerun=execution.AUTHORITATIVE_CORPUS_PROBE_RERUN,
            additional_optimizer_steps=execution.ADDITIONAL_OPTIMIZER_STEPS,
            model_training_invocations=execution.MODEL_TRAINING_INVOCATIONS,
            ephemeral_probe_regressions=execution.EPHEMERAL_PROBE_REGRESSION_INVOCATIONS,
            evidence_mutation=execution.EVIDENCE_MUTATION,
            recovery_failure=None,
        )
        evaluation = Task4BAcceptanceRecoveryEvidenceV1(
            original_attempt=historical,
            provenance=provenance,
            semantic_integrity_proof=proof,
            recovery_execution=execution,
            recovery_gate_statuses=recovery_gate_statuses,
            recovery_command_records=complete_records,
            ORIGINAL_SMOKE_PASS=historical.original_smoke_pass,
            ORIGINAL_TASK4B_PASS=historical.original_task4b_pass,
            TASK4B_RECOVERY_PASS=recovery_pass,
            TASK4B_FINAL_PASS=_derive_final_pass(
                original_smoke_pass=historical.original_smoke_pass,
                original_task4b_pass=historical.original_task4b_pass,
                immutable_original=historical.validated_artifacts is not None,
                recovery_pass=recovery_pass,
            ),
            recovery_failure=None,
        )
    except Task4BAcceptanceRecoveryError as error:
        if error.records:
            state.records = tuple(error.records)
            state.reached_command_count = len(error.records)
            state.ephemeral_probe_regressions = max(
                state.ephemeral_probe_regressions,
                error.ephemeral_probe_regressions,
            )
        evaluation = _failure_evaluation(state, error)
    except Exception as error:
        unexpected = Task4BAcceptanceRecoveryError(
            "UNEXPECTED_RECOVERY_FAILURE",
            failure_stage=state.failure_stage,
            reached_command_count=len(state.records),
            records=state.records,
            ephemeral_probe_regressions=state.ephemeral_probe_regressions,
        )
        evaluation = _failure_evaluation(state, unexpected)

    _publish_recovery_evidence(
        requested_output,
        evaluation,
        markdown=_markdown_from_evidence(evaluation),
    )
    return evaluation
