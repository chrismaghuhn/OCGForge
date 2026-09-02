"""Task-4B runner primitives, CUDA training, and smoke completion.

Post-smoke verification remains a later authorized plan task.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Sequence

import torch

from . import task4_codec as codec
from . import task4_cuda
from . import task4_inference
from . import task4_model


class Task4BSmokeError(RuntimeError):
    """Raised when a Task-4B runner invariant cannot be established."""

    def __init__(
        self,
        code: str,
        message: str,
        *,
        report: "Task4BExecutionReportV1 | None" = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.report = report

    @property
    def report_json(self) -> str:
        return self.report.to_json() if self.report is not None else str(self)


class Task4BTrainingError(RuntimeError):
    """Raised when CUDA-only training cannot satisfy its frozen contract."""

    def __init__(self, code: str, message: str, actual_optimizer_steps: int) -> None:
        super().__init__(message)
        self.code = code
        self.actual_optimizer_steps = actual_optimizer_steps


@dataclasses.dataclass(frozen=True)
class Task4BExecutionReportV1:
    """Machine-readable report for one Task-4B smoke attempt."""

    h_exec: str | None = None
    corpus_probe_sha256: str | None = None
    corpus_probe_source_commit: str | None = None
    cuda_preflight_identity: str | None = None
    cuda_available: bool | None = None
    framework_version: str | None = None
    torch_cuda_version_reported: str | None = None
    device_type: str | None = None
    device_index: int | None = None
    gpu_name: str | None = None
    capability_major: int | None = None
    capability_minor: int | None = None
    device_count: int | None = None
    cpu_fallback: bool | None = None
    backend_identity: str | None = None
    distributed_strategy: str | None = None
    world_size: int | None = None
    deterministic_algorithms: bool | None = None
    deterministic_warn_only: bool | None = None
    float32_matmul_precision: str | None = None
    source_dataset_identity: str | None = None
    dataset_split_identity: str | None = None
    card_vocabulary_identity: str | None = None
    train_sample_count: int | None = None
    validation_sample_count: int | None = None
    test_sample_count: int | None = None
    actual_optimizer_steps: int = 0
    gpu_memory_before: int | None = None
    gpu_memory_peak: int | None = None
    gpu_memory_after: int | None = None
    error_code: str | None = None
    smoke_pass: bool = False
    task4b_pass: bool = False
    checkpoint_identity: str | None = None
    smoke_evidence_identity: str | None = None
    initial_loss: float | None = None
    final_loss: float | None = None
    schema_id: str = "ocgforge.phase6.task4b.execution_report.v1"

    def to_dict(self) -> dict[str, object]:
        return {
            "schema_id": self.schema_id,
            "H_exec": self.h_exec,
            "corpus_probe_sha256": self.corpus_probe_sha256,
            "corpus_probe_source_commit": self.corpus_probe_source_commit,
            "cuda_preflight_identity": self.cuda_preflight_identity,
            "cuda_available": self.cuda_available,
            "framework_version": self.framework_version,
            "torch_cuda_version_reported": self.torch_cuda_version_reported,
            "device_type": self.device_type,
            "device_index": self.device_index,
            "gpu_name": self.gpu_name,
            "capability_major": self.capability_major,
            "capability_minor": self.capability_minor,
            "device_count": self.device_count,
            "cpu_fallback": self.cpu_fallback,
            "backend_identity": self.backend_identity,
            "distributed_strategy": self.distributed_strategy,
            "world_size": self.world_size,
            "deterministic_algorithms": self.deterministic_algorithms,
            "deterministic_warn_only": self.deterministic_warn_only,
            "float32_matmul_precision": self.float32_matmul_precision,
            "source_dataset_identity": self.source_dataset_identity,
            "dataset_split_identity": self.dataset_split_identity,
            "card_vocabulary_identity": self.card_vocabulary_identity,
            "train_sample_count": self.train_sample_count,
            "validation_sample_count": self.validation_sample_count,
            "test_sample_count": self.test_sample_count,
            "actual_optimizer_steps": self.actual_optimizer_steps,
            "GPU_MEMORY_BEFORE": self.gpu_memory_before,
            "GPU_MEMORY_PEAK": self.gpu_memory_peak,
            "GPU_MEMORY_AFTER": self.gpu_memory_after,
            "error_code": self.error_code,
            "SMOKE_PASS": self.smoke_pass,
            "TASK4B_PASS": self.task4b_pass,
            "checkpoint_identity": self.checkpoint_identity,
            "smoke_evidence_identity": self.smoke_evidence_identity,
            "initial_loss": self.initial_loss,
            "final_loss": self.final_loss,
        }

    def to_json(self) -> str:
        return json.dumps(
            self.to_dict(),
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )


@dataclasses.dataclass
class _ExecutionReportState:
    report: Task4BExecutionReportV1 = dataclasses.field(
        default_factory=Task4BExecutionReportV1
    )

    def update(self, **changes: object) -> None:
        self.report = dataclasses.replace(self.report, **changes)


@dataclasses.dataclass
class _SuccessfulStepCounter:
    """Internal count of optimizer steps that returned successfully."""

    value: int = 0

    def mark_success(self) -> None:
        self.value += 1


def _apply_optimizer_update(
    optimizer: torch.optim.Optimizer,
    loss: torch.Tensor,
    counter: _SuccessfulStepCounter,
) -> None:
    """Backpropagate and count only an optimizer step that succeeds."""

    loss.backward()
    optimizer.step()
    counter.mark_success()


def _ordered_train_samples(
    samples: Sequence[codec.CorpusSampleV1],
) -> tuple[codec.CorpusSampleV1, ...]:
    """Return only TRAIN samples in unsigned UTF-8 identity order."""

    train_samples = [sample for sample in samples if sample.partition == "train"]
    ordered = sorted(
        train_samples,
        key=lambda sample: sample.bc_sample_identity.encode("utf-8"),
    )
    if not ordered:
        raise Task4BSmokeError(
            "EMPTY_TRAIN_PARTITION",
            "admitted corpus has no train samples",
        )
    return tuple(ordered)


def _run_git(source_root: Path, *arguments: str) -> tuple[str, str]:
    """Run one read-only Git query and return stdout/stderr."""

    completed = subprocess.run(
        ("git", "-C", str(source_root), *arguments),
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        message = completed.stderr.strip() or "git command failed"
        raise Task4BSmokeError("GIT_COMMAND_FAILED", message)
    return completed.stdout, completed.stderr


def _run_command(argv: Sequence[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    """Run one non-shell command for Task-2 build/probe plumbing."""

    return subprocess.run(
        tuple(argv),
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    )


def _canonical_source_root() -> Path:
    """Resolve the checkout that contains this runner through Git."""

    module_root = Path(__file__).resolve().parents[2]
    git_root_text, _ = _run_git(module_root, "rev-parse", "--show-toplevel")
    git_root = Path(git_root_text.strip()).resolve()
    if os.path.normcase(str(git_root)) != os.path.normcase(str(module_root)):
        raise Task4BSmokeError(
            "SOURCE_ROOT_MISMATCH",
            "runner is not executing from its canonical checkout",
        )
    return git_root


def _verify_clean_h_exec(source_root: Path) -> str:
    """Require an immutable 40-character HEAD and an empty Git status."""

    head_text, _ = _run_git(source_root, "rev-parse", "HEAD")
    head = head_text.strip()
    if re.fullmatch(r"[0-9a-f]{40}", head) is None:
        raise Task4BSmokeError(
            "INVALID_H_EXEC",
            "HEAD is not exactly 40 lowercase hexadecimal characters",
        )
    status, _ = _run_git(
        source_root,
        "status",
        "--porcelain",
        "--untracked-files=all",
    )
    if status != "":
        raise Task4BSmokeError(
            "DIRTY_H_EXEC",
            "source checkout is not clean",
        )
    return head


def _read_cmake_cache(build_dir: Path) -> dict[str, str]:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        raise Task4BSmokeError(
            "MISSING_CMAKE_CACHE",
            f"CMake cache is missing: {cache_path}",
        )
    values: dict[str, str] = {}
    try:
        lines = cache_path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise Task4BSmokeError(
            "INVALID_CMAKE_CACHE",
            f"CMake cache cannot be read: {error}",
        ) from error
    for line in lines:
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        match = re.fullmatch(r"([^:]+):[^=]*=(.*)", line)
        if match is not None:
            values[match.group(1)] = match.group(2)
    return values


def _same_path(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def _verify_cmake_configuration(build_dir: Path, source_root: Path) -> None:
    """Reject a cache not bound to the canonical Ninja/Release source."""

    values = _read_cmake_cache(build_dir)
    configured_source = values.get("CMAKE_HOME_DIRECTORY")
    if configured_source is None or not _same_path(Path(configured_source), source_root):
        raise Task4BSmokeError(
            "STALE_BUILD_SOURCE",
            "CMake cache source directory is not the canonical source root",
        )
    if values.get("CMAKE_GENERATOR") != "Ninja":
        raise Task4BSmokeError(
            "STALE_BUILD_SOURCE",
            "CMake cache generator is not Ninja",
        )
    if values.get("CMAKE_BUILD_TYPE") != "Release":
        raise Task4BSmokeError(
            "STALE_BUILD_SOURCE",
            "CMake cache build type is not Release",
        )


def _require_probe_inside_build(build_dir: Path, probe_path: Path) -> Path:
    build = build_dir.resolve()
    probe = probe_path.resolve()
    try:
        probe.relative_to(build)
    except ValueError as error:
        raise Task4BSmokeError(
            "PROBE_OUTSIDE_BUILD",
            "corpus probe is outside the validated build directory",
        ) from error
    if not probe.is_file():
        raise Task4BSmokeError(
            "MISSING_PROBE_BINARY",
            f"corpus probe binary is missing: {probe}",
        )
    return probe


def _resolve_probe_binary(build_dir: Path) -> Path:
    """Resolve exactly one corpus-probe target under the validated build."""

    executable_name = (
        "phase6_task4_corpus_probe.exe"
        if os.name == "nt"
        else "phase6_task4_corpus_probe"
    )
    candidates = tuple(
        sorted(
            (path.resolve() for path in build_dir.resolve().rglob(executable_name) if path.is_file()),
            key=lambda path: str(path).encode("utf-8"),
        )
    )
    if not candidates:
        raise Task4BSmokeError(
            "MISSING_PROBE_BINARY",
            "CMake did not produce the corpus probe target",
        )
    if len(candidates) != 1:
        raise Task4BSmokeError(
            "AMBIGUOUS_PROBE_BINARY",
            "CMake build contains multiple corpus probe binaries",
        )
    return _require_probe_inside_build(build_dir, candidates[0])


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise Task4BSmokeError(
            "PROBE_HASH_FAILED",
            f"cannot hash probe binary: {error}",
        ) from error
    return digest.hexdigest()


def _build_and_attest_probe(
    source_root: Path,
    build_dir: Path,
) -> tuple[Path, str]:
    """Configure/build the exact probe target and return path plus SHA-256."""

    source_root = source_root.resolve()
    build_dir = build_dir.resolve()
    cache_path = build_dir / "CMakeCache.txt"
    if cache_path.is_file():
        _verify_cmake_configuration(build_dir, source_root)
    else:
        build_dir.parent.mkdir(parents=True, exist_ok=True)
        try:
            _run_command(
                (
                    "cmake",
                    "-S",
                    str(source_root),
                    "-B",
                    str(build_dir),
                    "-G",
                    "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                ),
                source_root,
            )
        except (OSError, subprocess.CalledProcessError) as error:
            raise Task4BSmokeError(
                "CMAKE_CONFIGURE_FAILED",
                f"CMake configure failed: {error}",
            ) from error
        _verify_cmake_configuration(build_dir, source_root)
    try:
        _run_command(
            (
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                "phase6_task4_corpus_probe",
                "--parallel",
                "1",
            ),
            source_root,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise Task4BSmokeError(
            "PROBE_BUILD_FAILED",
            f"corpus probe build failed: {error}",
        ) from error
    probe_path = _resolve_probe_binary(build_dir)
    return probe_path, _sha256_file(probe_path)


@dataclasses.dataclass(frozen=True)
class AdmittedCorpusArtifactsV1:
    probe_sha256: str
    corpus_bytes: bytes
    authority_bytes: bytes
    corpus: codec.DerivedCorpusV1
    authority: codec.CorpusAdmissionAuthorityV1


@dataclasses.dataclass(frozen=True)
class _Task4BPreparationResult:
    """Private Task-2/3 preparation state used before finalization."""

    source_root: Path
    output_dir: Path
    h_exec: str
    probe_path: Path
    probe_sha256: str
    admitted_corpus: AdmittedCorpusArtifactsV1
    source_dataset_identity: str
    dataset_split_identity: str
    card_vocabulary_identity: str
    train_sample_count: int
    validation_sample_count: int
    test_sample_count: int
    ordered_train_samples: tuple[codec.CorpusSampleV1, ...]
    training_result: "Task4BCudaTrainingResultV1"


@dataclasses.dataclass(frozen=True)
class Task4BSmokeRunResult:
    """Value-owned finalized Task-4 result surface."""

    report: Task4BExecutionReportV1
    report_json: str
    corpus_bytes: bytes
    authority_bytes: bytes
    checkpoint_bytes: bytes
    training_run_manifest_bytes: bytes
    smoke_evidence_bytes: bytes
    completion_receipt: task4_inference.Task4BCompletionReceiptV1


def _partition_counts(
    samples: Sequence[codec.CorpusSampleV1],
) -> tuple[int, int, int]:
    counts = {"train": 0, "validation": 0, "test": 0}
    for sample in samples:
        if sample.partition not in counts:
            raise Task4BSmokeError(
                "INVALID_CORPUS_PARTITION",
                f"unknown admitted corpus partition: {sample.partition}",
            )
        counts[sample.partition] += 1
    return counts["train"], counts["validation"], counts["test"]


def _require_cuda_tensor(
    value: torch.Tensor,
    device: torch.device,
    name: str,
    *,
    expected_dtype: torch.dtype | None = None,
    require_finite: bool = False,
    actual_optimizer_steps: int = 0,
) -> torch.Tensor:
    if not isinstance(value, torch.Tensor) or value.device != device:
        raise Task4BTrainingError(
            "WRONG_DEVICE",
            f"{name} is not on the required CUDA device",
            actual_optimizer_steps,
        )
    if device != torch.device("cuda:0") or value.device.type != "cuda":
        raise Task4BTrainingError(
            "CPU_FALLBACK_FORBIDDEN",
            f"{name} is not on cuda:0",
            actual_optimizer_steps,
        )
    if expected_dtype is not None:
        _require_tensor_dtype_and_finiteness(
            value,
            name,
            expected_dtype=expected_dtype,
            require_finite=require_finite,
            actual_optimizer_steps=actual_optimizer_steps,
        )
    return value


def _require_tensor_dtype_and_finiteness(
    value: torch.Tensor,
    name: str,
    *,
    expected_dtype: torch.dtype,
    require_finite: bool,
    actual_optimizer_steps: int,
) -> torch.Tensor:
    if not isinstance(value, torch.Tensor):
        raise Task4BTrainingError(
            "INVALID_TENSOR",
            f"{name} is not a tensor",
            actual_optimizer_steps,
        )
    if value.dtype != expected_dtype:
        raise Task4BTrainingError(
            "WRONG_DTYPE",
            f"{name} is not {expected_dtype}",
            actual_optimizer_steps,
        )
    if require_finite and not torch.isfinite(value).all().item():
        raise Task4BTrainingError(
            "NONFINITE_TENSOR",
            f"{name} contains a non-finite value",
            actual_optimizer_steps,
        )
    return value


def _make_adam_optimizer(
    model: torch.nn.Module,
) -> torch.optim.Optimizer:
    return torch.optim.Adam(
        model.parameters(),
        lr=0.001,
        betas=(0.9, 0.999),
        eps=1e-8,
        weight_decay=0.0,
        foreach=False,
        fused=False,
        amsgrad=False,
        maximize=False,
        capturable=False,
        differentiable=False,
        decoupled_weight_decay=False,
    )


def _require_training_preflight(
    preflight: task4_cuda.CudaPreflightResultV1,
) -> torch.device:
    if not isinstance(preflight, task4_cuda.CudaPreflightResultV1):
        raise Task4BTrainingError(
            "CUDA_DEVICE_MISMATCH",
            "CUDA preflight result is not an attested Task-4 value",
            0,
        )
    if (
        preflight.device_type != codec.EXPECTED_DEVICE_TYPE
        or preflight.device_index != codec.EXPECTED_DEVICE_INDEX
        or preflight.gpu_name != codec.EXPECTED_GPU_NAME
        or preflight.device_count < 1
        or preflight.actual_optimizer_steps != 0
        or preflight.cpu_fallback is not False
    ):
        raise Task4BTrainingError(
            "CUDA_DEVICE_MISMATCH",
            "training requires the clean attested cuda:0 preflight",
            0,
        )
    return preflight.device


def _require_model_on_device(
    model: task4_model.Phase6TorchCandidateScorer,
    device: torch.device,
) -> None:
    parameters = tuple(model.parameters())
    if not parameters or any(parameter.device != device for parameter in parameters):
        raise Task4BTrainingError(
            "WRONG_DEVICE",
            "model parameters are not on cuda:0",
            0,
        )
    if any(parameter.dtype != torch.float32 for parameter in parameters):
        raise Task4BTrainingError(
            "WRONG_DTYPE",
            "model parameters are not float32",
            0,
        )


def _loss_for_sample(
    model: task4_model.Phase6TorchCandidateScorer,
    sample: codec.CorpusSampleV1,
    device: torch.device,
    actual_optimizer_steps: int,
) -> torch.Tensor:
    try:
        numeric_input = codec.make_numeric_model_input(
            model_input_identity=sample.model_input_identity,
            state_rows=sample.state_rows,
            candidate_rows=sample.candidate_rows,
            routing_keys=sample.routing_keys,
            public_candidate_domain_digest=sample.public_candidate_domain_digest,
            public_semantic_decision_id=sample.public_semantic_decision_id,
            perspective_player=sample.perspective_player,
            decision_index=sample.decision_index,
        )
        if (
            sample.candidate_ordinal < 0
            or sample.candidate_ordinal >= len(sample.routing_keys)
            or sample.routing_keys[sample.candidate_ordinal]
            != sample.selected_public_action_key
        ):
            raise Task4BTrainingError(
                "INVALID_TRAIN_LABEL",
                "Teacher candidate ordinal is not paired with its public key",
                actual_optimizer_steps,
            )
        state_tensor = torch.tensor(
            numeric_input.state_rows,
            dtype=torch.float32,
            device=device,
        )
        candidate_tensor = torch.tensor(
            numeric_input.candidate_rows,
            dtype=torch.float32,
            device=device,
        )
        label_tensor = torch.tensor(
            [sample.candidate_ordinal],
            dtype=torch.long,
            device=device,
        )
        real_candidate_mask = torch.ones(
            (1, len(sample.routing_keys)),
            dtype=torch.bool,
            device=device,
        )
        _require_cuda_tensor(
            state_tensor,
            device,
            "state tensor",
            expected_dtype=torch.float32,
            require_finite=True,
            actual_optimizer_steps=actual_optimizer_steps,
        )
        _require_cuda_tensor(
            candidate_tensor,
            device,
            "candidate tensor",
            expected_dtype=torch.float32,
            require_finite=True,
            actual_optimizer_steps=actual_optimizer_steps,
        )
        _require_cuda_tensor(
            label_tensor,
            device,
            "label tensor",
            expected_dtype=torch.long,
            actual_optimizer_steps=actual_optimizer_steps,
        )
        _require_cuda_tensor(
            real_candidate_mask,
            device,
            "candidate mask",
            expected_dtype=torch.bool,
            actual_optimizer_steps=actual_optimizer_steps,
        )
        logits = model(state_tensor, candidate_tensor)
        _require_cuda_tensor(
            logits,
            device,
            "logits",
            expected_dtype=torch.float32,
            require_finite=True,
            actual_optimizer_steps=actual_optimizer_steps,
        )
        task4_model.validate_logits(logits, len(sample.routing_keys))
        loss = task4_model.exact_domain_cross_entropy_from_padded(
            logits.unsqueeze(0),
            label_tensor,
            real_candidate_mask,
        )
        _require_cuda_tensor(
            loss,
            device,
            "loss",
            expected_dtype=torch.float32,
            require_finite=True,
            actual_optimizer_steps=actual_optimizer_steps,
        )
        return loss
    except Task4BTrainingError:
        raise
    except (codec.CodecError, TypeError, ValueError, RuntimeError) as error:
        raise Task4BTrainingError(
            "TRAINING_INPUT_FAILED",
            f"training sample could not reach exact-domain loss: {error}",
            actual_optimizer_steps,
        ) from error


@dataclasses.dataclass(frozen=True)
class Task4BCudaTrainingResultV1:
    preflight: task4_cuda.CudaPreflightResultV1
    model: task4_model.Phase6TorchCandidateScorer
    actual_optimizer_steps: int
    initial_loss: float
    final_loss: float
    gpu_memory_before: int
    gpu_memory_peak: int
    gpu_memory_after: int


@dataclasses.dataclass(frozen=True)
class Task4BFinalizationV1:
    """Attested canonical export, completion receipt, and smoke evidence."""

    exported_checkpoint: task4_inference.ExportedCheckpointV1
    completion_receipt: task4_inference.Task4BCompletionReceiptV1
    training_run_manifest: codec.TrainingRunManifestV1
    smoke_evidence: codec.Task4BSmokeEvidenceV1
    training_run_identity: str
    checkpoint_identity: str
    smoke_evidence_identity: str


def _numeric_model_input_for_sample(
    sample: codec.CorpusSampleV1,
) -> codec.Task4NumericModelInputV1:
    return codec.make_numeric_model_input(
        model_input_identity=sample.model_input_identity,
        state_rows=sample.state_rows,
        candidate_rows=sample.candidate_rows,
        routing_keys=sample.routing_keys,
        public_candidate_domain_digest=sample.public_candidate_domain_digest,
        public_semantic_decision_id=sample.public_semantic_decision_id,
        perspective_player=sample.perspective_player,
        decision_index=sample.decision_index,
    )


def _finalize_task4b_artifacts(
    run_result: _Task4BPreparationResult,
    training_result: Task4BCudaTrainingResultV1,
) -> Task4BFinalizationV1:
    """Export and attest the canonical inference/checkpoint boundary."""

    corpus = run_result.admitted_corpus.corpus
    exported = task4_inference.export_canonical_checkpoint(
        training_result.model,
        source_dataset_identity=corpus.source_dataset_identity,
        dataset_split_identity=corpus.split_identity,
        card_vocabulary_identity=corpus.card_vocabulary_identity,
    )
    codec.decode_checkpoint_artifact(exported.artifact_bytes)
    sample = run_result.ordered_train_samples[0]
    numeric_input = _numeric_model_input_for_sample(sample)
    request = codec.make_inference_request(
        checkpoint_identity=exported.checkpoint_identity,
        model_input=numeric_input,
    )
    completion_receipt = task4_inference.issue_task4b_completion_receipt(
        exported,
        request=request,
        model_input=numeric_input,
        architecture_config=codec.default_architecture_config(),
        card_vocabulary_identity=corpus.card_vocabulary_identity,
        dataset_identity=corpus.source_dataset_identity,
        dataset_split_identity=corpus.split_identity,
    )
    base_manifest = codec.default_training_run_manifest(
        source_dataset_identity=corpus.source_dataset_identity,
        dataset_split_identity=corpus.split_identity,
        card_vocabulary_identity=corpus.card_vocabulary_identity,
        training_code_commit=run_result.h_exec,
        actual_optimizer_steps=0,
    )
    manifest = task4_cuda.finalize_training_run_manifest_from_cuda_preflight(
        training_result.preflight,
        base_manifest,
        final_exported_checkpoint_identity=exported.checkpoint_identity,
    )
    smoke_evidence = task4_cuda.smoke_evidence_from_cuda_preflight(
        training_result.preflight,
        manifest,
        completion_receipt,
        actual_optimizer_steps=training_result.actual_optimizer_steps,
        gpu_memory_before=training_result.gpu_memory_before,
        gpu_memory_peak=training_result.gpu_memory_peak,
        gpu_memory_after=training_result.gpu_memory_after,
    )
    codec.canonical_training_run_manifest_bytes(manifest)
    codec.canonical_smoke_evidence_bytes(smoke_evidence)
    return Task4BFinalizationV1(
        exported_checkpoint=exported,
        completion_receipt=completion_receipt,
        training_run_manifest=manifest,
        smoke_evidence=smoke_evidence,
        training_run_identity=codec.training_run_identity(manifest),
        checkpoint_identity=exported.checkpoint_identity,
        smoke_evidence_identity=codec.smoke_evidence_identity(smoke_evidence),
    )


def _execution_report_preflight_fields(
    preflight: task4_cuda.CudaPreflightResultV1,
) -> dict[str, object]:
    provenance = preflight.execution_provenance()
    return {
        "cuda_preflight_identity": preflight.cuda_preflight_identity,
        "cuda_available": True,
        "framework_version": preflight.framework_version,
        "torch_cuda_version_reported": preflight.torch_cuda_version_reported,
        "device_type": preflight.device_type,
        "device_index": preflight.device_index,
        "gpu_name": preflight.gpu_name,
        "capability_major": preflight.capability_major,
        "capability_minor": preflight.capability_minor,
        "device_count": preflight.device_count,
        "cpu_fallback": preflight.cpu_fallback,
        "backend_identity": provenance.backend_identity,
        "distributed_strategy": provenance.distributed_strategy,
        "world_size": provenance.world_size,
        "deterministic_algorithms": provenance.deterministic_algorithms,
        "deterministic_warn_only": provenance.deterministic_warn_only,
        "float32_matmul_precision": provenance.float32_matmul_precision,
    }


def _write_atomic_bytes(path: Path, data: bytes) -> None:
    path = path.resolve()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("xb") as stream:
        stream.write(bytes(data))
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _write_atomic_json(path: Path, value: object) -> None:
    encoded = json.dumps(
        value,
        ensure_ascii=True,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    _write_atomic_bytes(path, encoded)


def _completion_receipt_projection(
    receipt: task4_inference.Task4BCompletionReceiptV1,
) -> dict[str, object]:
    return {
        "checkpoint_identity": receipt.checkpoint_identity,
        "model_input_identity": receipt.model_input_identity,
        "ordered_candidate_domain_identity": receipt.ordered_candidate_domain_identity,
        "request_identity": receipt.request_identity,
        "response_identity": receipt.response_identity,
        "fresh_checkpoint_reload": receipt.fresh_checkpoint_reload,
        "deterministic_frozen_inference": receipt.deterministic_frozen_inference,
    }


def _write_smoke_artifacts(
    output_dir: Path,
    run_result: _Task4BPreparationResult,
    finalization: Task4BFinalizationV1,
    report: Task4BExecutionReportV1,
) -> None:
    output_dir = output_dir.resolve()
    corpus = run_result.admitted_corpus
    _write_atomic_bytes(output_dir / "corpus.p6c", corpus.corpus_bytes)
    _write_atomic_bytes(
        output_dir / "corpus.authority.p6a",
        corpus.authority_bytes,
    )
    _write_atomic_bytes(
        output_dir / "checkpoint.p6k",
        finalization.exported_checkpoint.artifact_bytes,
    )
    _write_atomic_bytes(
        output_dir / "training-run-manifest.p6m",
        codec.canonical_training_run_manifest_bytes(
            finalization.training_run_manifest
        ),
    )
    _write_atomic_bytes(
        output_dir / "smoke-evidence.p6e",
        codec.canonical_smoke_evidence_bytes(finalization.smoke_evidence),
    )
    _write_atomic_json(
        output_dir / "completion-receipt.json",
        _completion_receipt_projection(finalization.completion_receipt),
    )
    _write_atomic_bytes(
        output_dir / "task4b-execution-report.json",
        report.to_json().encode("utf-8"),
    )


def _run_cuda_training(
    ordered_train_samples: Sequence[codec.CorpusSampleV1],
    *,
    report_state: _ExecutionReportState | None = None,
) -> Task4BCudaTrainingResultV1:
    if not ordered_train_samples:
        raise Task4BTrainingError(
            "EMPTY_TRAIN_PARTITION",
            "cannot train without admitted TRAIN samples",
            0,
        )
    preflight = task4_cuda.require_task4_cuda()
    if report_state is not None:
        report_state.update(**_execution_report_preflight_fields(preflight))
    device = _require_training_preflight(preflight)
    counter = _SuccessfulStepCounter()
    try:
        torch.use_deterministic_algorithms(True, warn_only=False)
        torch.set_float32_matmul_precision("highest")
        torch.manual_seed(1729)
        torch.cuda.manual_seed_all(1729)
        if not torch.are_deterministic_algorithms_enabled():
            raise Task4BTrainingError(
                "DETERMINISM_CONFIGURATION",
                "strict deterministic algorithms were not enabled",
                0,
            )
        if torch.is_deterministic_algorithms_warn_only_enabled():
            raise Task4BTrainingError(
                "DETERMINISM_CONFIGURATION",
                "deterministic algorithms are warn-only",
                0,
            )
        if torch.get_float32_matmul_precision() != "highest":
            raise Task4BTrainingError(
                "DETERMINISM_CONFIGURATION",
                "float32 matmul precision is not highest",
                0,
            )
        model = task4_model.Phase6TorchCandidateScorer().to(device)
        _require_model_on_device(model, device)
        optimizer = _make_adam_optimizer(model)
        torch.cuda.synchronize(device)
        torch.cuda.reset_peak_memory_stats(device)
        gpu_memory_before = int(torch.cuda.memory_allocated(device))
        if report_state is not None:
            report_state.update(gpu_memory_before=gpu_memory_before)
        initial_loss_tensor = _loss_for_sample(
            model,
            ordered_train_samples[0],
            device,
            0,
        )
        initial_loss = float(initial_loss_tensor.detach().cpu().item())
        if report_state is not None:
            report_state.update(initial_loss=initial_loss)
        del initial_loss_tensor
        model.train()
        for step_index in range(codec.SMOKE_MAX_OPTIMIZER_STEPS):
            optimizer.zero_grad(set_to_none=True)
            loss = _loss_for_sample(
                model,
                ordered_train_samples[step_index % len(ordered_train_samples)],
                device,
                counter.value,
            )
            _apply_optimizer_update(optimizer, loss, counter)
            if report_state is not None:
                report_state.update(actual_optimizer_steps=counter.value)
        if counter.value != codec.SMOKE_MAX_OPTIMIZER_STEPS:
            raise Task4BTrainingError(
                "OPTIMIZER_STEP_COUNT_MISMATCH",
                "training did not complete the frozen optimizer-step bound",
                counter.value,
            )
        torch.cuda.synchronize(device)
        gpu_memory_peak = int(torch.cuda.max_memory_allocated(device))
        if report_state is not None:
            report_state.update(gpu_memory_peak=gpu_memory_peak)
        optimizer.zero_grad(set_to_none=True)
        torch.cuda.synchronize(device)
        gpu_memory_after = int(torch.cuda.memory_allocated(device))
        if report_state is not None:
            report_state.update(gpu_memory_after=gpu_memory_after)
        with torch.no_grad():
            final_loss_tensor = _loss_for_sample(
                model,
                ordered_train_samples[0],
                device,
                counter.value,
            )
            final_loss = float(final_loss_tensor.detach().cpu().item())
        if report_state is not None:
            report_state.update(final_loss=final_loss)
        del final_loss_tensor
        return Task4BCudaTrainingResultV1(
            preflight=preflight,
            model=model,
            actual_optimizer_steps=counter.value,
            initial_loss=initial_loss,
            final_loss=final_loss,
            gpu_memory_before=gpu_memory_before,
            gpu_memory_peak=gpu_memory_peak,
            gpu_memory_after=gpu_memory_after,
        )
    except task4_cuda.CudaPreflightError:
        raise
    except Task4BTrainingError:
        if report_state is not None:
            report_state.update(actual_optimizer_steps=counter.value)
        raise
    except (RuntimeError, TypeError, ValueError, codec.CodecError) as error:
        steps = counter.value
        if report_state is not None:
            report_state.update(actual_optimizer_steps=steps)
        raise Task4BTrainingError(
            "CUDA_TRAINING_FAILED",
            f"CUDA training failed: {error}",
            steps,
        ) from error


def _run_authoritative_corpus_probe(
    probe_path: Path,
    temporary_dir: Path,
    *,
    source_root: Path,
    expected_probe_sha256: str,
) -> AdmittedCorpusArtifactsV1:
    """Invoke the already-attested probe once and admit its sidecar-bound output."""

    probe_path = probe_path.resolve()
    if not probe_path.is_file():
        raise Task4BSmokeError(
            "MISSING_PROBE_BINARY",
            f"corpus probe binary is missing: {probe_path}",
        )
    if re.fullmatch(r"[0-9a-f]{64}", expected_probe_sha256) is None:
        raise Task4BSmokeError(
            "PROBE_HASH_MISMATCH",
            "expected corpus probe hash is not a lowercase SHA-256 digest",
        )
    before_execution_hash = _sha256_file(probe_path)
    if before_execution_hash != expected_probe_sha256:
        raise Task4BSmokeError(
            "PROBE_HASH_MISMATCH",
            "corpus probe changed after build attestation",
        )
    temporary_dir = temporary_dir.resolve()
    temporary_dir.mkdir(parents=True, exist_ok=True)
    corpus_path = temporary_dir / "corpus.p6c"
    authority_path = temporary_dir / "corpus.authority.p6a"
    cwd = source_root.resolve()
    try:
        _run_command(
            (
                str(probe_path),
                "--output",
                str(corpus_path),
                "--authority",
                str(authority_path),
            ),
            cwd,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise Task4BSmokeError(
            "CORPUS_PROBE_FAILED",
            f"authoritative corpus probe failed: {error}",
        ) from error
    after_execution_hash = _sha256_file(probe_path)
    if after_execution_hash != expected_probe_sha256:
        raise Task4BSmokeError(
            "PROBE_HASH_CHANGED_DURING_EXECUTION",
            "corpus probe changed during or after its execution",
        )
    try:
        corpus_bytes = corpus_path.read_bytes()
        authority_bytes = authority_path.read_bytes()
        authority = codec.decode_corpus_authority_artifact(authority_bytes)
        codec_corpus = codec.decode_corpus_artifact(corpus_bytes)
        admitted = codec.admit_corpus_artifact(corpus_bytes, authority)
    except (OSError, codec.CodecError, TypeError, ValueError) as error:
        raise Task4BSmokeError(
            "CORPUS_ADMISSION_FAILED",
            f"corpus authority admission failed: {error}",
        ) from error
    if admitted != codec_corpus:
        raise Task4BSmokeError(
            "CORPUS_ADMISSION_FAILED",
            "admitted corpus differs from the decoded corpus",
        )
    return AdmittedCorpusArtifactsV1(
        probe_sha256=after_execution_hash,
        corpus_bytes=corpus_bytes,
        authority_bytes=authority_bytes,
        corpus=admitted,
        authority=authority,
    )


def run_task4b_smoke(
    *,
    build_dir: Path,
    output_dir: Path,
) -> Task4BSmokeRunResult:
    """Run the authoritative Task-2 preparation and Task-3 CUDA seam.

    Canonical export, completion receipts, and execution reports are finalized
    here; post-smoke verification remains a later authorized plan task.
    """

    requested_output_dir = Path(output_dir).resolve()
    report_state = _ExecutionReportState()
    try:
        source_root = _canonical_source_root()
        h_exec = _verify_clean_h_exec(source_root)
        report_state.update(h_exec=h_exec)
        probe_path, probe_sha256 = _build_and_attest_probe(source_root, build_dir)
        report_state.update(
            corpus_probe_sha256=probe_sha256,
            corpus_probe_source_commit=h_exec,
        )
        with tempfile.TemporaryDirectory(prefix="ocgforge-task4b-corpus-") as directory:
            admitted = _run_authoritative_corpus_probe(
                probe_path,
                Path(directory),
                source_root=source_root,
                expected_probe_sha256=probe_sha256,
            )
        corpus = admitted.corpus
        train_count, validation_count, test_count = _partition_counts(corpus.samples)
        ordered_train_samples = _ordered_train_samples(corpus.samples)
        report_state.update(
            source_dataset_identity=corpus.source_dataset_identity,
            dataset_split_identity=corpus.split_identity,
            card_vocabulary_identity=corpus.card_vocabulary_identity,
            train_sample_count=train_count,
            validation_sample_count=validation_count,
            test_sample_count=test_count,
        )
        training_result = _run_cuda_training(
            ordered_train_samples,
            report_state=report_state,
        )
        report_state.update(
            actual_optimizer_steps=training_result.actual_optimizer_steps,
            gpu_memory_before=training_result.gpu_memory_before,
            gpu_memory_peak=training_result.gpu_memory_peak,
            gpu_memory_after=training_result.gpu_memory_after,
            initial_loss=training_result.initial_loss,
            final_loss=training_result.final_loss,
        )
        preparation = _Task4BPreparationResult(
            source_root=source_root,
            output_dir=requested_output_dir,
            h_exec=h_exec,
            probe_path=probe_path,
            probe_sha256=probe_sha256,
            admitted_corpus=admitted,
            source_dataset_identity=corpus.source_dataset_identity,
            dataset_split_identity=corpus.split_identity,
            card_vocabulary_identity=corpus.card_vocabulary_identity,
            train_sample_count=train_count,
            validation_sample_count=validation_count,
            test_sample_count=test_count,
            ordered_train_samples=ordered_train_samples,
            training_result=training_result,
        )
        finalization = _finalize_task4b_artifacts(preparation, training_result)
        report_state.update(
            smoke_pass=True,
            task4b_pass=False,
            checkpoint_identity=finalization.checkpoint_identity,
            smoke_evidence_identity=finalization.smoke_evidence_identity,
        )
        report = report_state.report
        _write_smoke_artifacts(
            requested_output_dir,
            preparation,
            finalization,
            report,
        )
        return Task4BSmokeRunResult(
            report=report,
            report_json=report.to_json(),
            corpus_bytes=preparation.admitted_corpus.corpus_bytes,
            authority_bytes=preparation.admitted_corpus.authority_bytes,
            checkpoint_bytes=finalization.exported_checkpoint.artifact_bytes,
            training_run_manifest_bytes=codec.canonical_training_run_manifest_bytes(
                finalization.training_run_manifest
            ),
            smoke_evidence_bytes=codec.canonical_smoke_evidence_bytes(
                finalization.smoke_evidence
            ),
            completion_receipt=finalization.completion_receipt,
        )
    except task4_cuda.CudaPreflightError as error:
        report_state.update(
            error_code=error.code,
            actual_optimizer_steps=error.actual_optimizer_steps,
        )
        _write_atomic_bytes(
            requested_output_dir / "task4b-execution-report.json",
            report_state.report.to_json().encode("utf-8"),
        )
        raise
    except Task4BTrainingError as error:
        report_state.update(
            error_code=error.code,
            actual_optimizer_steps=error.actual_optimizer_steps,
        )
        _write_atomic_bytes(
            requested_output_dir / "task4b-execution-report.json",
            report_state.report.to_json().encode("utf-8"),
        )
        raise
    except Task4BSmokeError as error:
        report_state.update(error_code=error.code)
        _write_atomic_bytes(
            requested_output_dir / "task4b-execution-report.json",
            report_state.report.to_json().encode("utf-8"),
        )
        if error.report is not None:
            raise
        raise Task4BSmokeError(
            error.code,
            str(error),
            report=report_state.report,
        ) from error
    except (codec.CodecError, task4_inference.Task4InferenceError, OSError, RuntimeError, TypeError, ValueError) as error:
        report_state.update(
            error_code=getattr(error, "code", "TASK4B_FAILED"),
        )
        _write_atomic_bytes(
            requested_output_dir / "task4b-execution-report.json",
            report_state.report.to_json().encode("utf-8"),
        )
        raise Task4BSmokeError(
            report_state.report.error_code or "TASK4B_FAILED",
            str(error),
            report=report_state.report,
        ) from error
