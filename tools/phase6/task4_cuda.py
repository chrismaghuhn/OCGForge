"""CUDA-only Task-4A preflight.

The preflight is deliberately separate from training.  It may inspect the
runtime and device, but it never constructs an optimizer or executes a model
update.  Hardware facts are execution provenance and never checkpoint
semantic identity.
"""

from __future__ import annotations

import dataclasses

import torch

from . import task4_codec as codec


_CUDA_PREFLIGHT_ATTESTATION = object()


class CudaPreflightError(RuntimeError):
    def __init__(self, code: str, message: str, actual_optimizer_steps: int = 0) -> None:
        super().__init__(message)
        self.code = code
        self.actual_optimizer_steps = actual_optimizer_steps


@dataclasses.dataclass(frozen=True)
class CudaPreflightResultV1:
    device_type: str
    device_index: int
    gpu_name: str
    framework_version: str
    torch_cuda_version_reported: str
    device_count: int
    capability_major: int
    capability_minor: int
    actual_optimizer_steps: int = 0
    cpu_fallback: bool = False
    _attestation: object = dataclasses.field(
        default=None, repr=False, compare=False
    )

    @property
    def device(self) -> torch.device:
        return torch.device(self.device_type, self.device_index)

    def execution_provenance(self) -> codec.ExecutionProvenanceV1:
        return codec.ExecutionProvenanceV1(
            framework_version=self.framework_version,
            device_type=self.device_type,
            device_index=self.device_index,
            gpu_name=self.gpu_name,
            torch_cuda_version_reported=self.torch_cuda_version_reported,
            capability_major=self.capability_major,
            capability_minor=self.capability_minor,
        )

    @property
    def execution_provenance_identity(self) -> str:
        return codec.execution_provenance_identity_for(self.execution_provenance())

    @property
    def cuda_preflight_identity(self) -> str:
        return codec.cuda_preflight_identity_for(codec.CudaPreflightFactsV1(
            cuda_available=True,
            device_count=self.device_count,
            execution_provenance=self.execution_provenance(),
        ))


def smoke_evidence_from_cuda_preflight(
    preflight: CudaPreflightResultV1,
    *,
    training_run_identity: str,
    source_dataset_identity: str,
    dataset_split_identity: str,
    card_vocabulary_identity: str,
    training_code_commit: str,
    actual_optimizer_steps: int = 0,
    final_exported_checkpoint_identity: str | None = None,
    gpu_memory_before: int | None = None,
    gpu_memory_peak: int | None = None,
    gpu_memory_after: int | None = None,
) -> codec.Task4BSmokeEvidenceV1:
    """Build Task-4B evidence from the same real CUDA preflight result."""

    manifest = codec.default_training_run_manifest(
        source_dataset_identity=source_dataset_identity,
        dataset_split_identity=dataset_split_identity,
        card_vocabulary_identity=card_vocabulary_identity,
        training_code_commit=training_code_commit,
        actual_optimizer_steps=0,
    )
    if preflight.actual_optimizer_steps != 0 or preflight.cpu_fallback:
        raise CudaPreflightError(
            "CUDA_DEVICE_MISMATCH",
            "training provenance requires a clean zero-step CUDA preflight",
        )
    if (actual_optimizer_steps > 0 and
            preflight._attestation is not _CUDA_PREFLIGHT_ATTESTATION):
        raise CudaPreflightError(
            "CUDA_DEVICE_MISMATCH",
            "positive smoke evidence requires the real CUDA preflight path",
        )
    evidence = codec.Task4BSmokeEvidenceV1(
        training_run_identity=training_run_identity,
        source_dataset_identity=manifest.source_dataset_identity,
        dataset_split_identity=manifest.dataset_split_identity,
        model_architecture_config_identity=manifest.model_architecture_config_identity,
        card_vocabulary_identity=manifest.card_vocabulary_identity,
        optimizer_configuration_identity=manifest.optimizer_configuration_identity,
        learning_rate_schedule_identity=manifest.learning_rate_schedule_identity,
        batch_configuration_identity=manifest.batch_configuration_identity,
        gradient_accumulation_configuration_identity=manifest.gradient_accumulation_configuration_identity,
        training_rng_contract_identity=manifest.training_rng_contract_identity,
        training_seed_or_initialization_identity=manifest.training_seed_or_initialization_identity,
        precision_mode_identity=manifest.precision_mode_identity,
        deterministic_execution_configuration_identity=codec.deterministic_execution_identity(),
        device_and_distributed_provenance_identity=preflight.execution_provenance_identity,
        cuda_preflight_identity=preflight.cuda_preflight_identity,
        maximum_optimizer_steps=codec.SMOKE_MAX_OPTIMIZER_STEPS,
        actual_optimizer_steps=actual_optimizer_steps,
        final_exported_checkpoint_identity=final_exported_checkpoint_identity,
        gpu_memory_before=gpu_memory_before,
        gpu_memory_peak=gpu_memory_peak,
        gpu_memory_after=gpu_memory_after,
    )
    try:
        codec.canonical_smoke_evidence_bytes(evidence)
    except codec.CodecError as error:
        raise CudaPreflightError("CUDA_DEVICE_MISMATCH", str(error)) from error
    return evidence


def require_task4_cuda() -> CudaPreflightResultV1:
    """Require the exact Task-4B execution device without doing any training."""

    if not torch.cuda.is_available():
        raise CudaPreflightError(
            "CUDA_UNAVAILABLE",
            "CUDA is unavailable; CPU fallback is forbidden",
        )
    try:
        device_count = torch.cuda.device_count()
        if device_count < 1:
            raise CudaPreflightError(
                "CUDA_UNAVAILABLE",
                "no CUDA device is available; CPU fallback is forbidden",
            )
        device = torch.device("cuda:0")
        if device.type != codec.EXPECTED_DEVICE_TYPE or device.index != codec.EXPECTED_DEVICE_INDEX:
            raise CudaPreflightError(
                "CUDA_DEVICE_MISMATCH",
                "Task-4 CUDA preflight requires cuda:0",
            )
        gpu_name = torch.cuda.get_device_name(codec.EXPECTED_DEVICE_INDEX)
        if gpu_name != codec.EXPECTED_GPU_NAME:
            raise CudaPreflightError(
                "CUDA_DEVICE_MISMATCH",
                f"unexpected GPU {gpu_name!r}; expected {codec.EXPECTED_GPU_NAME!r}",
            )
        capability = torch.cuda.get_device_capability(codec.EXPECTED_DEVICE_INDEX)
        cuda_build = torch.version.cuda
    except CudaPreflightError:
        raise
    except Exception as error:
        raise CudaPreflightError(
            "CUDA_UNAVAILABLE",
            f"CUDA device inspection failed; CPU fallback is forbidden: {error}",
        ) from error
    if not isinstance(cuda_build, str) or not cuda_build:
        raise CudaPreflightError(
            "CUDA_DEVICE_MISMATCH",
            "PyTorch does not expose a CUDA build version",
        )
    return CudaPreflightResultV1(
        device_type=device.type,
        device_index=int(device.index),
        gpu_name=gpu_name,
        framework_version=torch.__version__,
        torch_cuda_version_reported=cuda_build,
        device_count=int(device_count),
        capability_major=int(capability[0]),
        capability_minor=int(capability[1]),
        _attestation=_CUDA_PREFLIGHT_ATTESTATION,
    )
