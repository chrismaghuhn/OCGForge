"""Task-4B runner primitives.

The complete smoke runner is added in later authorized plan tasks.  This
module currently contains only the Task-1 ownership and TRAIN-order helpers.
"""

from __future__ import annotations

import dataclasses
import hashlib
import os
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Sequence

import torch

from . import task4_codec as codec
from . import task4_cuda
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
class Task4BSmokeRunResult:
    """Task-2 preparation result; training fields are added only later."""

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
) -> torch.Tensor:
    if not isinstance(value, torch.Tensor) or value.device != device:
        raise Task4BTrainingError(
            "WRONG_DEVICE",
            f"{name} is not on the required CUDA device",
            0,
        )
    if device != torch.device("cuda:0") or value.device.type != "cuda":
        raise Task4BTrainingError(
            "CPU_FALLBACK_FORBIDDEN",
            f"{name} is not on cuda:0",
            0,
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
        _require_cuda_tensor(state_tensor, device, "state tensor")
        _require_cuda_tensor(candidate_tensor, device, "candidate tensor")
        _require_cuda_tensor(label_tensor, device, "label tensor")
        _require_cuda_tensor(real_candidate_mask, device, "candidate mask")
        logits = model(state_tensor, candidate_tensor)
        _require_cuda_tensor(logits, device, "logits")
        task4_model.validate_logits(logits, len(sample.routing_keys))
        if not torch.isfinite(logits).all().item():
            raise Task4BTrainingError(
                "NONFINITE_LOGITS",
                "CUDA logits are non-finite",
                actual_optimizer_steps,
            )
        loss = task4_model.exact_domain_cross_entropy_from_padded(
            logits.unsqueeze(0),
            label_tensor,
            real_candidate_mask,
        )
        _require_cuda_tensor(loss, device, "loss")
        if not torch.isfinite(loss).item():
            raise Task4BTrainingError(
                "NONFINITE_LOSS",
                "CUDA loss is non-finite",
                actual_optimizer_steps,
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


def _run_cuda_training(
    ordered_train_samples: Sequence[codec.CorpusSampleV1],
) -> Task4BCudaTrainingResultV1:
    if not ordered_train_samples:
        raise Task4BTrainingError(
            "EMPTY_TRAIN_PARTITION",
            "cannot train without admitted TRAIN samples",
            0,
        )
    preflight = task4_cuda.require_task4_cuda()
    device = _require_training_preflight(preflight)
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
        initial_loss_tensor = _loss_for_sample(
            model,
            ordered_train_samples[0],
            device,
            0,
        )
        initial_loss = float(initial_loss_tensor.detach().cpu().item())
        del initial_loss_tensor
        counter = _SuccessfulStepCounter()
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
        if counter.value != codec.SMOKE_MAX_OPTIMIZER_STEPS:
            raise Task4BTrainingError(
                "OPTIMIZER_STEP_COUNT_MISMATCH",
                "training did not complete the frozen optimizer-step bound",
                counter.value,
            )
        torch.cuda.synchronize(device)
        gpu_memory_peak = int(torch.cuda.max_memory_allocated(device))
        optimizer.zero_grad(set_to_none=True)
        torch.cuda.synchronize(device)
        gpu_memory_after = int(torch.cuda.memory_allocated(device))
        with torch.no_grad():
            final_loss_tensor = _loss_for_sample(
                model,
                ordered_train_samples[0],
                device,
                counter.value,
            )
            final_loss = float(final_loss_tensor.detach().cpu().item())
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
        raise
    except (RuntimeError, TypeError, ValueError, codec.CodecError) as error:
        steps = locals().get("counter").value if "counter" in locals() else 0
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
    """Compose the authorized Task-2 preparation seam only.

    CUDA preflight, optimizer creation, model updates, export, and evidence
    finalization are intentionally not part of this Task-2 implementation.
    """

    source_root = _canonical_source_root()
    h_exec = _verify_clean_h_exec(source_root)
    probe_path, probe_sha256 = _build_and_attest_probe(source_root, build_dir)
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
    training_result = _run_cuda_training(ordered_train_samples)
    return Task4BSmokeRunResult(
        source_root=source_root,
        output_dir=Path(output_dir).resolve(),
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
