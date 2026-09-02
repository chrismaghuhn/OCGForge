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
    )
