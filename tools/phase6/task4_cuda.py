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
    torch_cuda_build: str
    cuda_runtime: str
    capability_major: int
    capability_minor: int
    actual_optimizer_steps: int = 0
    cpu_fallback: bool = False

    @property
    def device(self) -> torch.device:
        return torch.device(self.device_type, self.device_index)

    def execution_provenance(self) -> codec.ExecutionProvenanceV1:
        return codec.ExecutionProvenanceV1(
            framework_version=self.framework_version,
            device_type=self.device_type,
            device_index=self.device_index,
            gpu_name=self.gpu_name,
            torch_cuda_build=self.torch_cuda_build,
            cuda_runtime=self.cuda_runtime,
            capability_major=self.capability_major,
            capability_minor=self.capability_minor,
        )

    @property
    def execution_provenance_identity(self) -> str:
        return codec.execution_provenance_identity_for(self.execution_provenance())


def training_run_manifest_from_cuda_preflight(
    preflight: CudaPreflightResultV1,
    *,
    source_dataset_identity: str,
    dataset_split_identity: str,
    card_vocabulary_identity: str,
    training_code_commit: str,
    actual_optimizer_steps: int = 0,
) -> codec.TrainingRunManifestV1:
    """Bind run provenance to facts returned by the same CUDA preflight."""

    manifest = codec.default_training_run_manifest(
        source_dataset_identity=source_dataset_identity,
        dataset_split_identity=dataset_split_identity,
        card_vocabulary_identity=card_vocabulary_identity,
        training_code_commit=training_code_commit,
        actual_optimizer_steps=actual_optimizer_steps,
    )
    return dataclasses.replace(
        manifest,
        framework_version=preflight.framework_version,
        device_and_distributed_provenance_identity=preflight.execution_provenance_identity,
    )


def require_task4_cuda() -> CudaPreflightResultV1:
    """Require the exact Task-4B execution device without doing any training."""

    if not torch.cuda.is_available():
        raise CudaPreflightError(
            "CUDA_UNAVAILABLE",
            "CUDA is unavailable; CPU fallback is forbidden",
        )
    try:
        if torch.cuda.device_count() < 1:
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
        torch_cuda_build=cuda_build,
        cuda_runtime=cuda_build,
        capability_major=int(capability[0]),
        capability_minor=int(capability[1]),
    )
