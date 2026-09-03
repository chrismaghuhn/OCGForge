"""H_exec-owned post-smoke verification for the Task-4B attempt."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Mapping, Sequence

from . import task4_codec as codec


BASE_HEAD = "1727f09eb0fdc4e4e25e3f9ced9748feb4058234"
VERIFICATION_SCHEMA_ID = "ocgforge.phase6.task4b.verification.v1"
ACCEPTANCE_SCHEMA_ID = "ocgforge.phase6.task4b.acceptance.v1"
EXECUTION_REPORT_FILENAME = "task4b-execution-report.json"
ALLOWED_OUTPUT_FILES = frozenset(
    {
        "corpus.p6c",
        "corpus.authority.p6a",
        "checkpoint.p6k",
        "training-run-manifest.p6m",
        "smoke-evidence.p6e",
        "completion-receipt.json",
        "task4b-execution-report.json",
        "task4b-verification.json",
        "task4b-acceptance.json",
        "task4b-acceptance.md",
    }
)
REQUIRED_GATE_IDS = (
    "task4-focused-python",
    "admitted-forward",
    "full-non-long-ctest",
    "project-python",
    "rules-bundle",
    "rules-deck",
    "teacher-binding",
    "public-boundary",
    "source-boundary",
    "base-to-h-exec-diff-check",
)
_EXECUTION_REPORT_KEYS = frozenset(
    {
        "schema_id",
        "H_exec",
        "corpus_probe_sha256",
        "corpus_probe_source_commit",
        "cuda_preflight_identity",
        "cuda_available",
        "framework_version",
        "torch_cuda_version_reported",
        "device_type",
        "device_index",
        "gpu_name",
        "capability_major",
        "capability_minor",
        "device_count",
        "cpu_fallback",
        "backend_identity",
        "distributed_strategy",
        "world_size",
        "deterministic_algorithms",
        "deterministic_warn_only",
        "float32_matmul_precision",
        "source_dataset_identity",
        "dataset_split_identity",
        "card_vocabulary_identity",
        "train_sample_count",
        "validation_sample_count",
        "test_sample_count",
        "actual_optimizer_steps",
        "GPU_MEMORY_BEFORE",
        "GPU_MEMORY_PEAK",
        "GPU_MEMORY_AFTER",
        "error_code",
        "SMOKE_PASS",
        "TASK4B_PASS",
        "checkpoint_identity",
        "smoke_evidence_identity",
        "initial_loss",
        "final_loss",
    }
)
_PREFLIGHT_REPORT_KEYS = (
    "cuda_preflight_identity",
    "cuda_available",
    "framework_version",
    "torch_cuda_version_reported",
    "device_type",
    "device_index",
    "gpu_name",
    "capability_major",
    "capability_minor",
    "device_count",
    "cpu_fallback",
    "backend_identity",
    "distributed_strategy",
    "world_size",
    "deterministic_algorithms",
    "deterministic_warn_only",
    "float32_matmul_precision",
)
_HEX40 = re.compile(r"[0-9a-f]{40}")
_HEX64 = re.compile(r"[0-9a-f]{64}")
_EMPTY_SHA256 = hashlib.sha256(b"").hexdigest()


class Task4BVerificationError(RuntimeError):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclasses.dataclass(frozen=True)
class VerificationCommandV1:
    command_id: str
    argv: tuple[str, ...]
    exit_code: int
    stdout_sha256: str
    stderr_sha256: str
    status: str


@dataclasses.dataclass(frozen=True)
class Task4BVerificationResultV1:
    h_exec: str
    smoke_pass: bool
    task4b_pass: bool
    commands: tuple[VerificationCommandV1, ...]
    execution_report_sha256: str
    verification_json: str


@dataclasses.dataclass(frozen=True)
class _GateCommand:
    command_id: str
    argv: tuple[str, ...]
    probe_dependent: bool = False


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
        raise Task4BVerificationError("PROBE_HASH_FAILED") from error
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


def _run_git(source_root: Path, *arguments: str) -> tuple[str, str]:
    try:
        completed = subprocess.run(
            ("git", "-C", str(source_root), *arguments),
            shell=False,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        raise Task4BVerificationError("GIT_COMMAND_FAILED") from error
    if completed.returncode != 0:
        raise Task4BVerificationError("GIT_COMMAND_FAILED")
    return completed.stdout, completed.stderr


def _canonical_source_root() -> Path:
    module_root = Path(__file__).resolve().parents[2]
    root_text, _ = _run_git(module_root, "rev-parse", "--show-toplevel")
    source_root = Path(root_text.strip()).resolve()
    if os.path.normcase(str(source_root)) != os.path.normcase(str(module_root)):
        raise Task4BVerificationError("SOURCE_ROOT_MISMATCH")
    return source_root


def _load_execution_report(
    output_dir: Path,
) -> tuple[dict[str, object], bytes, str]:
    report_path = output_dir.resolve() / EXECUTION_REPORT_FILENAME
    try:
        report_bytes = report_path.read_bytes()
        report = json.loads(
            report_bytes.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_json_keys,
            parse_constant=_reject_nonfinite_json_constant,
        )
    except (OSError, UnicodeError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT") from error
    if not isinstance(report, dict) or set(report) != _EXECUTION_REPORT_KEYS:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_SCHEMA")
    try:
        canonical = _canonical_json_bytes(report)
    except (TypeError, ValueError) as error:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT") from error
    if report_bytes != canonical:
        raise Task4BVerificationError("NONCANONICAL_EXECUTION_REPORT")
    return report, report_bytes, _sha256_bytes(report_bytes)


def _require_string(report: Mapping[str, object], key: str) -> str:
    value = report.get(key)
    if not isinstance(value, str) or not value:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    return value


def _require_bool(report: Mapping[str, object], key: str) -> bool:
    value = report.get(key)
    if not isinstance(value, bool):
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    return value


def _require_int(
    report: Mapping[str, object],
    key: str,
    *,
    minimum: int | None = None,
    maximum: int | None = None,
) -> int:
    value = report.get(key)
    if not isinstance(value, int) or isinstance(value, bool):
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    if minimum is not None and value < minimum:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    if maximum is not None and value > maximum:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    return value


def _require_hex(report: Mapping[str, object], key: str, length: int) -> str:
    value = _require_string(report, key)
    pattern = _HEX40 if length == 40 else _HEX64
    if pattern.fullmatch(value) is None:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    return value


def _require_prefixed_hex(
    report: Mapping[str, object],
    key: str,
    prefix: str,
) -> str:
    value = _require_string(report, key)
    if re.fullmatch(re.escape(prefix) + r"[0-9a-f]{64}", value) is None:
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    return value


def _require_finite_number(report: Mapping[str, object], key: str) -> float:
    value = report.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    converted = float(value)
    if not math.isfinite(converted):
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    return converted


def _validate_cuda_preflight_report(report: Mapping[str, object]) -> None:
    cuda_available = _require_bool(report, "cuda_available")
    if cuda_available is not True:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    if report.get("cpu_fallback") is not False:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    device_type = _require_string(report, "device_type")
    if device_type != codec.EXPECTED_DEVICE_TYPE:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    device_index = _require_int(report, "device_index", minimum=0)
    if device_index != codec.EXPECTED_DEVICE_INDEX:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    gpu_name = _require_string(report, "gpu_name")
    if gpu_name != codec.EXPECTED_GPU_NAME:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    device_count = _require_int(report, "device_count", minimum=1)
    capability_major = _require_int(report, "capability_major", minimum=0)
    capability_minor = _require_int(report, "capability_minor", minimum=0)
    distributed_strategy = _require_string(report, "distributed_strategy")
    world_size = _require_int(report, "world_size", minimum=1)
    if world_size != 1:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    if distributed_strategy != "single_process":
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    deterministic_algorithms = _require_bool(report, "deterministic_algorithms")
    if deterministic_algorithms is not True:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    deterministic_warn_only = _require_bool(report, "deterministic_warn_only")
    if deterministic_warn_only is not False:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    float32_matmul_precision = _require_string(
        report, "float32_matmul_precision"
    )
    if float32_matmul_precision != "highest":
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID")
    backend_identity = _require_string(report, "backend_identity")
    framework_version = _require_string(report, "framework_version")
    torch_cuda_version_reported = _require_string(
        report, "torch_cuda_version_reported"
    )
    provenance = codec.ExecutionProvenanceV1(
        backend_identity=backend_identity,
        framework_version=framework_version,
        device_type=device_type,
        device_index=device_index,
        gpu_name=gpu_name,
        torch_cuda_version_reported=torch_cuda_version_reported,
        capability_major=capability_major,
        capability_minor=capability_minor,
        distributed_strategy=distributed_strategy,
        world_size=world_size,
        deterministic_algorithms=deterministic_algorithms,
        deterministic_warn_only=deterministic_warn_only,
        float32_matmul_precision=float32_matmul_precision,
    )
    facts = codec.CudaPreflightFactsV1(
        cuda_available=cuda_available,
        device_count=device_count,
        execution_provenance=provenance,
    )
    try:
        expected_identity = codec.cuda_preflight_identity_for(facts)
    except (codec.CodecError, TypeError, ValueError) as error:
        raise Task4BVerificationError("CUDA_PREFLIGHT_INVALID") from error
    if _require_string(report, "cuda_preflight_identity") != expected_identity:
        raise Task4BVerificationError("CUDA_PREFLIGHT_IDENTITY_MISMATCH")


def _validate_optional_failure_facts(report: Mapping[str, object]) -> None:
    if (
        not isinstance(report.get("error_code"), str)
        or not report["error_code"]
    ):
        raise Task4BVerificationError("INVALID_FAILURE_REPORT")
    _require_int(
        report,
        "actual_optimizer_steps",
        minimum=0,
        maximum=codec.SMOKE_MAX_OPTIMIZER_STEPS,
    )

    probe_hash = report.get("corpus_probe_sha256")
    probe_source_commit = report.get("corpus_probe_source_commit")
    if (probe_hash is None) != (probe_source_commit is None):
        raise Task4BVerificationError("INVALID_FAILURE_REPORT")
    if probe_hash is not None and probe_source_commit is not None:
        if not isinstance(probe_hash, str) or _HEX64.fullmatch(probe_hash) is None:
            raise Task4BVerificationError("INVALID_FAILURE_REPORT")
        if (
            not isinstance(probe_source_commit, str)
            or _HEX40.fullmatch(probe_source_commit) is None
            or probe_source_commit != report["H_exec"]
        ):
            raise Task4BVerificationError("INVALID_FAILURE_REPORT")

    metadata_keys = (
        "source_dataset_identity",
        "dataset_split_identity",
        "card_vocabulary_identity",
        "train_sample_count",
        "validation_sample_count",
        "test_sample_count",
    )
    metadata_present = [report.get(key) is not None for key in metadata_keys]
    if any(metadata_present) and not all(metadata_present):
        raise Task4BVerificationError("INVALID_FAILURE_REPORT")
    if all(metadata_present):
        _require_string(report, "source_dataset_identity")
        if _HEX64.fullmatch(report["source_dataset_identity"]) is None:  # type: ignore[arg-type]
            raise Task4BVerificationError("INVALID_FAILURE_REPORT")
        _require_prefixed_hex(
            report, "dataset_split_identity", "phase6_dataset_split.v1."
        )
        _require_prefixed_hex(
            report, "card_vocabulary_identity", "model_card_vocabulary.v1."
        )
        _require_int(report, "train_sample_count", minimum=1)
        _require_int(report, "validation_sample_count", minimum=0)
        _require_int(report, "test_sample_count", minimum=0)

    preflight_present = [
        report.get(key) is not None for key in _PREFLIGHT_REPORT_KEYS
    ]
    if any(preflight_present) and not all(preflight_present):
        raise Task4BVerificationError("INVALID_FAILURE_REPORT")
    if all(preflight_present):
        _validate_cuda_preflight_report(report)

    for key in ("GPU_MEMORY_BEFORE", "GPU_MEMORY_PEAK", "GPU_MEMORY_AFTER"):
        if report.get(key) is not None:
            _require_int(report, key, minimum=0)
    if report.get("initial_loss") is not None:
        _require_finite_number(report, "initial_loss")
    if report.get("final_loss") is not None:
        if report.get("initial_loss") is None:
            raise Task4BVerificationError("INVALID_FAILURE_REPORT")
        _require_finite_number(report, "final_loss")


def _validate_execution_report(report: Mapping[str, object]) -> tuple[str, bool]:
    if report.get("schema_id") != "ocgforge.phase6.task4b.execution_report.v1":
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_SCHEMA")
    h_exec = _require_hex(report, "H_exec", 40)
    smoke_pass = _require_bool(report, "SMOKE_PASS")
    if _require_bool(report, "TASK4B_PASS"):
        raise Task4BVerificationError("PREMATURE_TASK4B_ACCEPTANCE")
    if not smoke_pass:
        if report.get("checkpoint_identity") is not None:
            raise Task4BVerificationError("INVALID_FAILURE_REPORT")
        if report.get("smoke_evidence_identity") is not None:
            raise Task4BVerificationError("INVALID_FAILURE_REPORT")
        _validate_optional_failure_facts(report)
        return h_exec, False
    if report.get("error_code") is not None:
        raise Task4BVerificationError("INVALID_SUCCESS_REPORT")
    source_commit = _require_hex(report, "corpus_probe_source_commit", 40)
    if source_commit != h_exec:
        raise Task4BVerificationError("H_EXEC_PROBE_COMMIT_MISMATCH")
    _require_hex(report, "corpus_probe_sha256", 64)
    _require_string(report, "source_dataset_identity")
    if _HEX64.fullmatch(report["source_dataset_identity"]) is None:  # type: ignore[arg-type]
        raise Task4BVerificationError("INVALID_EXECUTION_REPORT_VALUE")
    _require_prefixed_hex(report, "dataset_split_identity", "phase6_dataset_split.v1.")
    _require_prefixed_hex(report, "card_vocabulary_identity", "model_card_vocabulary.v1.")
    _require_int(report, "train_sample_count", minimum=1)
    _require_int(report, "validation_sample_count", minimum=0)
    _require_int(report, "test_sample_count", minimum=0)
    if _require_int(
        report,
        "actual_optimizer_steps",
        minimum=0,
        maximum=codec.SMOKE_MAX_OPTIMIZER_STEPS,
    ) != codec.SMOKE_MAX_OPTIMIZER_STEPS:
        raise Task4BVerificationError("INVALID_SUCCESS_REPORT")
    for key in ("GPU_MEMORY_BEFORE", "GPU_MEMORY_PEAK", "GPU_MEMORY_AFTER"):
        _require_int(report, key, minimum=0)
    _require_finite_number(report, "initial_loss")
    _require_finite_number(report, "final_loss")
    _require_prefixed_hex(report, "checkpoint_identity", "phase6_checkpoint.v1.")
    _require_prefixed_hex(
        report,
        "smoke_evidence_identity",
        "phase6_task4b_smoke_evidence.v1.",
    )
    _validate_cuda_preflight_report(report)
    return h_exec, True


def _read_cmake_cache(build_dir: Path) -> dict[str, str]:
    cache_path = build_dir / "CMakeCache.txt"
    if not cache_path.is_file():
        raise Task4BVerificationError("MISSING_CMAKE_CACHE")
    values: dict[str, str] = {}
    try:
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            match = re.fullmatch(r"([^:]+):[^=]*=(.*)", line)
            if match is not None:
                values[match.group(1)] = match.group(2)
    except (OSError, UnicodeError) as error:
        raise Task4BVerificationError("INVALID_CMAKE_CACHE") from error
    return values


def _same_path(left: Path, right: Path) -> bool:
    return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


def _verify_cmake_configuration(build_dir: Path, source_root: Path) -> None:
    values = _read_cmake_cache(build_dir)
    if not _same_path(Path(values.get("CMAKE_HOME_DIRECTORY", "")), source_root):
        raise Task4BVerificationError("STALE_BUILD_SOURCE")
    if values.get("CMAKE_GENERATOR") != "Ninja":
        raise Task4BVerificationError("STALE_BUILD_SOURCE")
    if values.get("CMAKE_BUILD_TYPE") != "Release":
        raise Task4BVerificationError("STALE_BUILD_SOURCE")


def _resolve_probe_binary(build_dir: Path) -> Path:
    executable_name = (
        "phase6_task4_corpus_probe.exe"
        if os.name == "nt"
        else "phase6_task4_corpus_probe"
    )
    candidates = tuple(
        sorted(
            (
                path.resolve()
                for path in build_dir.resolve().rglob(executable_name)
                if path.is_file()
            ),
            key=lambda path: str(path).encode("utf-8"),
        )
    )
    if len(candidates) != 1:
        raise Task4BVerificationError(
            "MISSING_PROBE_BINARY" if not candidates else "AMBIGUOUS_PROBE_BINARY"
        )
    probe = candidates[0]
    try:
        probe.relative_to(build_dir.resolve())
    except ValueError as error:
        raise Task4BVerificationError("PROBE_OUTSIDE_BUILD") from error
    return probe


def _probe_hash_matches(probe_path: Path, expected_sha256: str) -> None:
    if _sha256_file(probe_path) != expected_sha256:
        raise Task4BVerificationError("PROBE_HASH_MISMATCH")


def _status_path(record: str) -> str:
    if len(record) < 4:
        raise Task4BVerificationError("INVALID_GIT_STATUS")
    path = record[3:]
    if " -> " in path:
        raise Task4BVerificationError("INVALID_GIT_STATUS")
    return path


def _require_allowed_output_path(
    source_root: Path,
    output_dir: Path,
    relative_path: str,
    allowed_output_files: frozenset[str],
    *,
    untracked: bool,
) -> None:
    candidate = Path(relative_path)
    resolved = (
        candidate.resolve()
        if candidate.is_absolute()
        else (source_root / candidate).resolve()
    )
    if (
        resolved.parent != output_dir.resolve()
        or resolved.name not in allowed_output_files
    ):
        raise Task4BVerificationError(
            "POST_SMOKE_UNEXPECTED_UNTRACKED_FILE"
            if untracked
            else "POST_SMOKE_TRACKED_SOURCE_CHANGED"
        )


def _verify_h_exec_source_integrity(
    *,
    source_root: Path,
    expected_head: str,
    output_dir: Path,
    allowed_output_files: frozenset[str],
) -> None:
    source_root = source_root.resolve()
    output_dir = output_dir.resolve()
    try:
        output_dir.relative_to(source_root)
    except ValueError as error:
        raise Task4BVerificationError("POST_SMOKE_OUTPUT_OUTSIDE_SOURCE") from error
    actual_head = _run_git(source_root, "rev-parse", "HEAD")[0].strip()
    if actual_head != expected_head:
        raise Task4BVerificationError("POST_SMOKE_HEAD_CHANGED")
    for arguments in (
        ("diff", "--name-only", "--diff-filter=ACMRTUXB"),
        ("diff", "--cached", "--name-only", "--diff-filter=ACMRTUXB"),
    ):
        changed = _run_git(source_root, *arguments)[0]
        for relative_path in changed.splitlines():
            if relative_path:
                _require_allowed_output_path(
                    source_root,
                    output_dir,
                    relative_path,
                    allowed_output_files,
                    untracked=False,
                )
    status = _run_git(
        source_root,
        "status",
        "--porcelain",
        "--untracked-files=all",
    )[0]
    for record in status.splitlines():
        if not record:
            continue
        untracked = record.startswith("?? ")
        if not untracked and len(record) >= 2 and record[1] != " ":
            raise Task4BVerificationError("POST_SMOKE_TRACKED_SOURCE_CHANGED")
        _require_allowed_output_path(
            source_root,
            output_dir,
            _status_path(record),
            allowed_output_files,
            untracked=untracked,
        )


def _fixed_gate_commands(
    *,
    build_dir: Path,
    probe_path: Path,
    h_exec: str,
) -> tuple[_GateCommand, ...]:
    probe = str(probe_path.resolve())
    build = str(build_dir.resolve())
    return (
        _GateCommand(
            "task4-focused-python",
            (
                "python",
                "-m",
                "unittest",
                "-v",
                "tests.phase6.phase6_task4a_codec_test",
                "tests.phase6.phase6_task4a_model_test",
                "tests.phase6.phase6_task4a_inference_test",
                "tests.phase6.phase6_task4a_cuda_preflight_test",
                "tests.phase6.phase6_task4b_runner_test",
            ),
        ),
        _GateCommand(
            "admitted-forward",
            ("python", "-m", "tests.phase6.phase6_task4a_corpus_test", probe),
            probe_dependent=True,
        ),
        _GateCommand(
            "admitted-forward",
            (
                "python",
                "-m",
                "tests.phase6.phase6_task4a_admitted_model_test",
                probe,
            ),
            probe_dependent=True,
        ),
        _GateCommand(
            "full-non-long-ctest",
            (
                "ctest",
                "--test-dir",
                build,
                "--output-on-failure",
                "-j",
                "1",
                "-LE",
                "P4A_HEAVY_REPLAY|M4_HEAVY_LIFECYCLE|M4_ACCEPTANCE_SCALE|P6_PYTORCH_REQUIRED",
            ),
        ),
        _GateCommand(
            "project-python",
            ("python", "-m", "unittest", "discover", "-s", "tests/python", "-v"),
        ),
        _GateCommand(
            "rules-bundle",
            (
                "python",
                "tools/verify_rules_bundle.py",
                "--lock",
                "third_party/rules_bundle.lock.json",
                "--cache",
                ".cache/rules_bundle",
            ),
        ),
        _GateCommand(
            "rules-deck",
            (
                "ctest",
                "--test-dir",
                build,
                "--output-on-failure",
                "-j",
                "1",
                "-R",
                "^(fixture_deck_test|deck_loader_test|m3_rules_mode_test|m3_real_deck_privacy_test)$",
            ),
        ),
        _GateCommand(
            "teacher-binding",
            (
                "ctest",
                "--test-dir",
                build,
                "--output-on-failure",
                "-j",
                "1",
                "-R",
                "^(teacher_policy_boundary_compile_test|teacher_domain_preservation_test|teacher_provenance_test|phase4b_teacher_identity_regression_test)$",
            ),
        ),
        _GateCommand(
            "public-boundary",
            (
                "ctest",
                "--test-dir",
                build,
                "--output-on-failure",
                "-j",
                "1",
                "-R",
                "^(episodic_environment_v2_public_projection_test|public_action_identity_test|public_safe_state_test|privacy_projection_test|logical_model_public_boundary_test)$",
            ),
        ),
        _GateCommand(
            "source-boundary",
            ("python", "tests/policy/policy_boundary_test.py"),
        ),
        _GateCommand(
            "source-boundary",
            ("python", "tests/teacher/teacher_public_boundary_test.py"),
        ),
        _GateCommand(
            "source-boundary",
            ("python", "tests/episodic/episode_driver_ownership_guard.py"),
        ),
        _GateCommand(
            "source-boundary",
            (
                "python",
                "tests/model/logical_model_public_boundary_test.py",
                "include/ygo/model",
                "src/model",
            ),
        ),
        _GateCommand(
            "base-to-h-exec-diff-check",
            ("git", "diff", "--check", BASE_HEAD, h_exec),
        ),
    )


def _output_bytes(value: object) -> bytes:
    if value is None:
        return b""
    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        return value.encode("utf-8")
    return str(value).encode("utf-8")


def _run_fixed_command(
    command: _GateCommand,
    source_root: Path,
) -> VerificationCommandV1:
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
    return VerificationCommandV1(
        command_id=command.command_id,
        argv=command.argv,
        exit_code=exit_code,
        stdout_sha256=_sha256_bytes(stdout),
        stderr_sha256=_sha256_bytes(stderr),
        status=status,
    )


def _not_run_gate_statuses() -> dict[str, str]:
    return {command_id: "NOT_RUN" for command_id in REQUIRED_GATE_IDS}


def _gate_statuses(commands: Sequence[VerificationCommandV1]) -> dict[str, str]:
    statuses: dict[str, str] = {}
    for command_id in REQUIRED_GATE_IDS:
        matching = [command for command in commands if command.command_id == command_id]
        if not matching:
            statuses[command_id] = "NOT_RUN"
        elif all(command.status == "PASS" for command in matching):
            statuses[command_id] = "PASS"
        else:
            statuses[command_id] = "FAIL"
    return statuses


def _command_payload(command: VerificationCommandV1) -> dict[str, object]:
    return {
        "command_id": command.command_id,
        "argv": list(command.argv),
        "exit_code": command.exit_code,
        "stdout_sha256": command.stdout_sha256,
        "stderr_sha256": command.stderr_sha256,
        "status": command.status,
    }


def _evidence_metadata(report: Mapping[str, object]) -> dict[str, object]:
    return {
        "H_exec": report.get("H_exec"),
        "corpus_probe_sha256": report.get("corpus_probe_sha256"),
        "corpus_probe_source_commit": report.get("corpus_probe_source_commit"),
        "checkpoint_identity": report.get("checkpoint_identity"),
        "smoke_evidence_identity": report.get("smoke_evidence_identity"),
    }


def _verification_payload(
    result: Task4BVerificationResultV1,
    report: Mapping[str, object],
    gate_statuses: Mapping[str, str],
) -> dict[str, object]:
    payload: dict[str, object] = {
        "schema_id": VERIFICATION_SCHEMA_ID,
        "SMOKE_PASS": result.smoke_pass,
        "TASK4B_PASS": result.task4b_pass,
        "execution_report_sha256": result.execution_report_sha256,
        "commands": [_command_payload(command) for command in result.commands],
        "gate_statuses": dict(gate_statuses),
    }
    payload.update(_evidence_metadata(report))
    return payload


def _acceptance_payload(
    result: Task4BVerificationResultV1,
    report: Mapping[str, object],
    gate_statuses: Mapping[str, str],
) -> dict[str, object]:
    payload: dict[str, object] = {
        "schema_id": ACCEPTANCE_SCHEMA_ID,
        "SMOKE_PASS": result.smoke_pass,
        "TASK4B_PASS": result.task4b_pass,
        "execution_report_sha256": result.execution_report_sha256,
        "commands": [_command_payload(command) for command in result.commands],
        "gate_statuses": dict(gate_statuses),
    }
    payload.update(_evidence_metadata(report))
    return payload


def _acceptance_markdown(
    result: Task4BVerificationResultV1,
    report: Mapping[str, object],
    gate_statuses: Mapping[str, str],
) -> str:
    lines = [
        "# Task4B Acceptance",
        "",
        f"H_exec: {report.get('H_exec')}",
        f"corpus_probe_sha256: {report.get('corpus_probe_sha256')}",
        f"corpus_probe_source_commit: {report.get('corpus_probe_source_commit')}",
        f"checkpoint_identity: {report.get('checkpoint_identity')}",
        f"smoke_evidence_identity: {report.get('smoke_evidence_identity')}",
        f"SMOKE_PASS: {str(result.smoke_pass).lower()}",
        f"TASK4B_PASS: {str(result.task4b_pass).lower()}",
        "",
        "| gate | status |",
        "| --- | --- |",
    ]
    lines.extend(
        f"| {command_id} | {gate_statuses[command_id]} |"
        for command_id in REQUIRED_GATE_IDS
    )
    lines.extend(
        (
            "",
            "| command_id | argv | exit_code | stdout_sha256 | stderr_sha256 | status |",
            "| --- | --- | ---: | --- | --- | --- |",
        )
    )
    for command in result.commands:
        argv = json.dumps(
            list(command.argv), ensure_ascii=True, separators=(",", ":")
        )
        lines.append(
            f"| {command.command_id} | `{argv}` | {command.exit_code} | "
            f"`{command.stdout_sha256}` | `{command.stderr_sha256}` | {command.status} |"
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


def _write_evidence(
    output_dir: Path,
    result: Task4BVerificationResultV1,
    report: Mapping[str, object],
    gate_statuses: Mapping[str, str],
) -> None:
    output_dir = output_dir.resolve()
    _write_atomic_bytes(
        output_dir / "task4b-verification.json",
        result.verification_json.encode("utf-8"),
    )
    _write_atomic_bytes(
        output_dir / "task4b-acceptance.json",
        _canonical_json_bytes(_acceptance_payload(result, report, gate_statuses)),
    )
    _write_atomic_bytes(
        output_dir / "task4b-acceptance.md",
        _acceptance_markdown(result, report, gate_statuses).encode("utf-8"),
    )


def _make_result(
    *,
    h_exec: str,
    smoke_pass: bool,
    commands: tuple[VerificationCommandV1, ...],
    execution_report_sha256: str,
    report: Mapping[str, object],
) -> tuple[Task4BVerificationResultV1, dict[str, str]]:
    gate_statuses = (
        _gate_statuses(commands) if smoke_pass else _not_run_gate_statuses()
    )
    task4b_pass = smoke_pass and all(
        status == "PASS" for status in gate_statuses.values()
    )
    preliminary = Task4BVerificationResultV1(
        h_exec=h_exec,
        smoke_pass=smoke_pass,
        task4b_pass=task4b_pass,
        commands=commands,
        execution_report_sha256=execution_report_sha256,
        verification_json="",
    )
    verification_json = _canonical_json_bytes(
        _verification_payload(preliminary, report, gate_statuses)
    ).decode("utf-8")
    return dataclasses.replace(preliminary, verification_json=verification_json), gate_statuses


def run_post_smoke_verification(
    *,
    build_dir: Path,
    output_dir: Path,
) -> Task4BVerificationResultV1:
    output_dir = Path(output_dir).resolve()
    source_root = _canonical_source_root()
    report, _, report_sha256 = _load_execution_report(output_dir)
    h_exec, smoke_pass = _validate_execution_report(report)
    if not smoke_pass:
        result, gate_statuses = _make_result(
            h_exec=h_exec,
            smoke_pass=False,
            commands=(),
            execution_report_sha256=report_sha256,
            report=report,
        )
        _write_evidence(output_dir, result, report, gate_statuses)
        return result

    build_dir = Path(build_dir).resolve()
    _verify_cmake_configuration(build_dir, source_root)
    probe_path = _resolve_probe_binary(build_dir)
    expected_probe_sha256 = _require_hex(report, "corpus_probe_sha256", 64)
    _probe_hash_matches(probe_path, expected_probe_sha256)
    specifications = _fixed_gate_commands(
        build_dir=build_dir,
        probe_path=probe_path,
        h_exec=h_exec,
    )
    commands: list[VerificationCommandV1] = []
    for specification in specifications:
        _verify_h_exec_source_integrity(
            source_root=source_root,
            expected_head=h_exec,
            output_dir=output_dir,
            allowed_output_files=ALLOWED_OUTPUT_FILES,
        )
        if specification.probe_dependent:
            _probe_hash_matches(probe_path, expected_probe_sha256)
        command = _run_fixed_command(specification, source_root)
        commands.append(command)
        if specification.probe_dependent:
            _probe_hash_matches(probe_path, expected_probe_sha256)
    _probe_hash_matches(probe_path, expected_probe_sha256)
    result, gate_statuses = _make_result(
        h_exec=h_exec,
        smoke_pass=True,
        commands=tuple(commands),
        execution_report_sha256=report_sha256,
        report=report,
    )
    _write_evidence(output_dir, result, report, gate_statuses)
    return result


def check_source_integrity_from_report(*, output_dir: Path) -> None:
    output_dir = Path(output_dir).resolve()
    source_root = _canonical_source_root()
    report, _, _ = _load_execution_report(output_dir)
    h_exec, _ = _validate_execution_report(report)
    _verify_h_exec_source_integrity(
        source_root=source_root,
        expected_head=h_exec,
        output_dir=output_dir,
        allowed_output_files=ALLOWED_OUTPUT_FILES,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--check-source-integrity", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.check_source_integrity:
            check_source_integrity_from_report(output_dir=args.output_dir)
            return 0
        if args.build_dir is None:
            parser.error("--build-dir is required unless --check-source-integrity is set")
        result = run_post_smoke_verification(
            build_dir=args.build_dir,
            output_dir=args.output_dir,
        )
    except Task4BVerificationError as error:
        print(error.code, file=sys.stderr)
        return 1
    print(result.verification_json)
    return 0 if result.task4b_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
