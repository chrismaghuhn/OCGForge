"""Acceptance-Recovery V1 for the immutable Phase-6 Task-4B smoke attempt.

The recovery API is intentionally separate from ``task4b_verify``.  It reads
the original evidence from Git objects, proves the permitted source boundary,
and, when explicitly invoked by a future authorized workflow, runs only the
corrected post-smoke gate set.  Importing this module never executes a gate,
probe, smoke, model, or optimizer operation.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import math
import os
import re
import subprocess
from pathlib import Path
from typing import Any, Mapping, Sequence

from . import task4_codec as codec
from . import task4b_verify as verifier


H_SMOKE_EXEC = "8f682d4c9eb53a32be7cd8f6125048583943f19e"
H_FAILURE_EVIDENCE = "5bcec55bd473b1f599c99d7d8cbe5e31ba4c7832"
H_VERIFIER_FIX = "97fd0f6e8445a18a4f7939cc66bb8f131f905dcf"
RECOVERY_CONTRACT_COMMIT = "1be26a984ac80ba97440c3caf78a1d36b1d1927b"
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
_EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()


class Task4BAcceptanceRecoveryError(RuntimeError):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclasses.dataclass(frozen=True)
class RecoveryCommandRecordV1:
    command_id: str
    argv: tuple[str, ...]
    exit_code: int
    stdout_sha256: str
    stderr_sha256: str
    status: str


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
class _HistoricalEvidence:
    h_smoke_exec: str
    h_failure_evidence: str
    original_execution_report_sha256: str
    original_verification_report_sha256: str
    original_acceptance_report_sha256: str
    original_checkpoint_identity: str
    original_smoke_evidence_identity: str
    original_probe_sha256: str
    original_corpus_probe_source_commit: str
    original_smoke_pass: bool
    original_task4b_pass: bool
    original_failed_gate_id: str
    original_failed_gate_exit_code: int
    original_command_record_count: int
    file_sha256: Mapping[str, str]


@dataclasses.dataclass(frozen=True)
class Task4BAcceptanceRecoveryEvidenceV1:
    original_attempt: _HistoricalEvidence
    provenance: RecoveryProvenanceV1
    semantic_integrity_proof: SemanticIntegrityProofV1
    recovery_execution: RecoveryExecutionV1
    recovery_gate_statuses: tuple[tuple[str, str], ...]
    recovery_command_records: tuple[RecoveryCommandRecordV1, ...]
    ORIGINAL_SMOKE_PASS: bool
    ORIGINAL_TASK4B_PASS: bool
    TASK4B_RECOVERY_PASS: bool
    TASK4B_FINAL_PASS: bool
    schema_id: str = RECOVERY_SCHEMA_ID

    def to_dict(self) -> dict[str, object]:
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
            "provenance": dataclasses.asdict(self.provenance),
            "semantic_integrity_proof": dataclasses.asdict(
                self.semantic_integrity_proof
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
        commands = report.get("commands")
        gate_statuses = report.get("gate_statuses")
        if (
            not isinstance(commands, list)
            or len(commands) != 14
            or not isinstance(gate_statuses, dict)
            or set(gate_statuses) != set(verifier.REQUIRED_GATE_IDS)
        ):
            raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")

    failed_commands = [
        command
        for command in verification_report["commands"]
        if isinstance(command, dict)
        and command.get("command_id") == "full-non-long-ctest"
    ]
    if (
        verification_report["gate_statuses"].get("full-non-long-ctest") != "FAIL"
        or len(failed_commands) != 1
        or failed_commands[0].get("exit_code") != 8
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_FAILURE_GATE")
    if any(
        status != "PASS"
        for gate_id, status in verification_report["gate_statuses"].items()
        if gate_id != "full-non-long-ctest"
    ):
        raise Task4BAcceptanceRecoveryError("INVALID_HISTORICAL_GATE_RECORDS")

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
    except OSError as error:
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
    for command in commands:
        _verify_recovery_worktree(
            source_root=source_root,
            expected_head=expected_head,
            output_dir=output_dir,
        )
        if command.probe_dependent:
            if _sha256_file(probe_path) != HISTORICAL_PROBE_SHA256:
                raise Task4BAcceptanceRecoveryError("PROBE_HASH_MISMATCH")
        records.append(_run_recovery_command(command, source_root))
        if command.probe_dependent:
            if _sha256_file(probe_path) != HISTORICAL_PROBE_SHA256:
                raise Task4BAcceptanceRecoveryError("PROBE_HASH_CHANGED")
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
    )


def _derive_final_pass(
    *,
    original_smoke_pass: bool,
    original_task4b_pass: bool,
    immutable_original: bool,
    recovery_pass: bool,
) -> bool:
    return (
        original_smoke_pass
        and original_task4b_pass is False
        and immutable_original
        and recovery_pass
    )


def _markdown_from_evidence(evidence: Task4BAcceptanceRecoveryEvidenceV1) -> str:
    lines = [
        "# Task4B Acceptance-Recovery V1",
        "",
        f"H_SMOKE_EXEC: {evidence.original_attempt.h_smoke_exec}",
        f"H_FAILURE_EVIDENCE: {evidence.original_attempt.h_failure_evidence}",
        f"training_code_commit: {evidence.provenance.training_code_commit}",
        f"failed_evidence_commit: {evidence.provenance.failed_evidence_commit}",
        f"verifier_fix_commit: {evidence.provenance.verifier_fix_commit}",
        f"recovery_verifier_source_commit: {evidence.provenance.recovery_verifier_source_commit}",
        f"ORIGINAL_SMOKE_PASS: {str(evidence.ORIGINAL_SMOKE_PASS).lower()}",
        f"ORIGINAL_TASK4B_PASS: {str(evidence.ORIGINAL_TASK4B_PASS).lower()}",
        f"TASK4B_RECOVERY_PASS: {str(evidence.TASK4B_RECOVERY_PASS).lower()}",
        f"TASK4B_FINAL_PASS: {str(evidence.TASK4B_FINAL_PASS).lower()}",
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


def _write_atomic_bytes(path: Path, data: bytes) -> None:
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("xb") as stream:
        stream.write(bytes(data))
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _publish_recovery_evidence(
    output_dir: Path,
    evidence: Task4BAcceptanceRecoveryEvidenceV1,
    *,
    markdown: str | None = None,
) -> None:
    output_dir = output_dir.resolve()
    _write_atomic_bytes(
        output_dir / RECOVERY_JSON_FILENAME,
        evidence.to_json().encode("utf-8"),
    )
    _write_atomic_bytes(
        output_dir / RECOVERY_MARKDOWN_FILENAME,
        (markdown if markdown is not None else _markdown_from_evidence(evidence)).encode(
            "utf-8"
        ),
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
    recovery_head = _git_head(source_root)
    expected_output = (source_root / RECOVERY_OUTPUT_DIRECTORY).resolve()
    requested_output = expected_output if output_dir is None else Path(output_dir).resolve()
    if requested_output != expected_output:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_OUTPUT_DIRECTORY")
    if requested_output.exists():
        raise Task4BAcceptanceRecoveryError("RECOVERY_OUTPUT_ALREADY_EXISTS")

    historical = _load_historical_evidence(source_root)
    proof = _prove_source_integrity(source_root, recovery_head)
    provenance = _make_provenance(recovery_head)
    _verify_recovery_worktree(
        source_root=source_root,
        expected_head=recovery_head,
        output_dir=requested_output,
    )
    probe_path = _validate_build_and_probe(Path(build_dir), source_root)
    commands = _recovery_gate_commands(Path(build_dir), probe_path, H_SMOKE_EXEC)
    if len(commands) != 14 or sum(command.probe_dependent for command in commands) != 3:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_GATE_PLAN")
    if sum(
        command.argv.count("tests.phase6.phase6_task4b_runner_test")
        for command in commands
        if command.command_id == "task4-focused-python"
    ) != 1:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_GATE_PLAN")
    if any(
        "task4b_cuda_smoke.py" in command.argv or "run_task4b_smoke" in command.argv
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
    _verify_recovery_worktree(
        source_root=source_root,
        expected_head=recovery_head,
        output_dir=requested_output,
    )
    if len(records) != 14:
        raise Task4BAcceptanceRecoveryError("INVALID_RECOVERY_COMMAND_RECORDS")
    gate_statuses = verifier._gate_statuses(records)
    recovery_gate_statuses = tuple(
        (gate_id, gate_statuses[gate_id]) for gate_id in verifier.REQUIRED_GATE_IDS
    )
    execution = RecoveryExecutionV1(
        CUDA_SMOKE_RERUN=False,
        AUTHORITATIVE_CORPUS_PROBE_RERUN=False,
        ADDITIONAL_OPTIMIZER_STEPS=0,
        MODEL_TRAINING_INVOCATIONS=0,
        EPHEMERAL_PROBE_REGRESSION_INVOCATIONS=sum(
            command.probe_dependent for command in commands
        ),
        EVIDENCE_MUTATION=False,
    )
    recovery_pass = _derive_recovery_pass(
        immutable_original=True,
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
    )
    evidence = Task4BAcceptanceRecoveryEvidenceV1(
        original_attempt=historical,
        provenance=provenance,
        semantic_integrity_proof=proof,
        recovery_execution=execution,
        recovery_gate_statuses=recovery_gate_statuses,
        recovery_command_records=records,
        ORIGINAL_SMOKE_PASS=historical.original_smoke_pass,
        ORIGINAL_TASK4B_PASS=historical.original_task4b_pass,
        TASK4B_RECOVERY_PASS=recovery_pass,
        TASK4B_FINAL_PASS=_derive_final_pass(
            original_smoke_pass=historical.original_smoke_pass,
            original_task4b_pass=historical.original_task4b_pass,
            immutable_original=True,
            recovery_pass=recovery_pass,
        ),
    )
    _publish_recovery_evidence(
        requested_output,
        evidence,
        markdown=_markdown_from_evidence(evidence),
    )
    return evidence
