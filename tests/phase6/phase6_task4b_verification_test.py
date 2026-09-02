import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4b_verify as verifier


BASE_HEAD = "1727f09eb0fdc4e4e25e3f9ced9748feb4058234"
H_EXEC = "a" * 40
PROBE_BYTES = b"synthetic-probe"
PROBE_SHA256 = hashlib.sha256(PROBE_BYTES).hexdigest()
CHECKPOINT_IDENTITY = "phase6_checkpoint.v1." + "b" * 64
SMOKE_EVIDENCE_IDENTITY = "phase6_task4b_smoke_evidence.v1." + "c" * 64


def _write_cmake_cache(build_dir: Path, source_root: Path) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    source_text = str(source_root).replace("\\", "/")
    (build_dir / "CMakeCache.txt").write_text(
        "\n".join(
            (
                f"CMAKE_HOME_DIRECTORY:INTERNAL={source_text}",
                "CMAKE_GENERATOR:INTERNAL=Ninja",
                "CMAKE_BUILD_TYPE:STRING=Release",
            )
        )
        + "\n",
        encoding="utf-8",
    )


def _preflight_values() -> dict[str, object]:
    provenance = codec.ExecutionProvenanceV1(
        framework_version="synthetic-pytorch",
        torch_cuda_version_reported="12.6",
    )
    facts = codec.CudaPreflightFactsV1(
        cuda_available=True,
        device_count=1,
        execution_provenance=provenance,
    )
    return {
        "cuda_preflight_identity": codec.cuda_preflight_identity_for(facts),
        "cuda_available": True,
        "framework_version": provenance.framework_version,
        "torch_cuda_version_reported": provenance.torch_cuda_version_reported,
        "device_type": provenance.device_type,
        "device_index": provenance.device_index,
        "gpu_name": provenance.gpu_name,
        "capability_major": provenance.capability_major,
        "capability_minor": provenance.capability_minor,
        "device_count": facts.device_count,
        "cpu_fallback": False,
        "backend_identity": provenance.backend_identity,
        "distributed_strategy": provenance.distributed_strategy,
        "world_size": provenance.world_size,
        "deterministic_algorithms": provenance.deterministic_algorithms,
        "deterministic_warn_only": provenance.deterministic_warn_only,
        "float32_matmul_precision": provenance.float32_matmul_precision,
    }


def _report(*, smoke_pass: bool = True) -> dict[str, object]:
    payload: dict[str, object] = {
        "schema_id": "ocgforge.phase6.task4b.execution_report.v1",
        "H_exec": H_EXEC,
        "corpus_probe_sha256": PROBE_SHA256 if smoke_pass else None,
        "corpus_probe_source_commit": H_EXEC,
        "source_dataset_identity": "d" * 64 if smoke_pass else None,
        "dataset_split_identity": (
            "phase6_dataset_split.v1." + "e" * 64 if smoke_pass else None
        ),
        "card_vocabulary_identity": (
            "model_card_vocabulary.v1." + "f" * 64 if smoke_pass else None
        ),
        "train_sample_count": 8 if smoke_pass else None,
        "validation_sample_count": 1 if smoke_pass else None,
        "test_sample_count": 1 if smoke_pass else None,
        "actual_optimizer_steps": 500 if smoke_pass else 0,
        "GPU_MEMORY_BEFORE": 101 if smoke_pass else None,
        "GPU_MEMORY_PEAK": 202 if smoke_pass else None,
        "GPU_MEMORY_AFTER": 111 if smoke_pass else None,
        "error_code": None if smoke_pass else "CUDA_UNAVAILABLE",
        "SMOKE_PASS": smoke_pass,
        "TASK4B_PASS": False,
        "checkpoint_identity": CHECKPOINT_IDENTITY if smoke_pass else None,
        "smoke_evidence_identity": SMOKE_EVIDENCE_IDENTITY if smoke_pass else None,
        "initial_loss": 3.5 if smoke_pass else None,
        "final_loss": 2.5 if smoke_pass else None,
    }
    payload.update(_preflight_values() if smoke_pass else {
        "cuda_preflight_identity": None,
        "cuda_available": None,
        "framework_version": None,
        "torch_cuda_version_reported": None,
        "device_type": None,
        "device_index": None,
        "gpu_name": None,
        "capability_major": None,
        "capability_minor": None,
        "device_count": None,
        "cpu_fallback": None,
        "backend_identity": None,
        "distributed_strategy": None,
        "world_size": None,
        "deterministic_algorithms": None,
        "deterministic_warn_only": None,
        "float32_matmul_precision": None,
    })
    return payload


def _canonical_json(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=True,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _write_report(output_dir: Path, payload: dict[str, object]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "task4b-execution-report.json").write_bytes(
        _canonical_json(payload)
    )


def _git_snapshot(
    expected_head: str = H_EXEC,
    *,
    diff: str = "",
    cached_diff: str = "",
    status: str = "",
):
    def run(_source_root: Path, *arguments: str):
        if arguments == ("rev-parse", "HEAD"):
            return expected_head, ""
        if arguments == ("diff", "--name-only", "--diff-filter=ACMRTUXB"):
            return diff, ""
        if arguments == (
            "diff",
            "--cached",
            "--name-only",
            "--diff-filter=ACMRTUXB",
        ):
            return cached_diff, ""
        if arguments == ("status", "--porcelain", "--untracked-files=all"):
            return status, ""
        raise AssertionError(f"unexpected git query: {arguments}")

    return run


class Task4BVerificationTests(unittest.TestCase):
    def _setup_paths(self, root: Path) -> tuple[Path, Path, Path]:
        source_root = root / "source"
        build_dir = root / "build"
        output_dir = source_root / "docs" / "p6" / "task4b"
        _write_cmake_cache(build_dir, source_root)
        probe = build_dir / "phase6_task4_corpus_probe.exe"
        probe.write_bytes(PROBE_BYTES)
        return source_root, build_dir, output_dir

    def _run_with_successful_commands(
        self,
        *,
        source_root: Path,
        build_dir: Path,
        output_dir: Path,
        report: dict[str, object] | None = None,
        command_side_effect=None,
    ) -> tuple[verifier.Task4BVerificationResultV1, list[tuple[tuple[str, ...], Path]]]:
        calls: list[tuple[tuple[str, ...], Path]] = []

        def run_command(argv, *, cwd, **kwargs):
            del kwargs
            command = tuple(str(value) for value in argv)
            calls.append((command, Path(cwd)))
            if command_side_effect is not None:
                return command_side_effect(command, Path(cwd))
            return subprocess.CompletedProcess(command, 0, stdout="out", stderr="")

        with mock.patch.object(
            verifier, "_canonical_source_root", return_value=source_root
        ), mock.patch.object(
            verifier,
            "_run_git",
            side_effect=_git_snapshot(),
        ), mock.patch.object(
            verifier.subprocess, "run", side_effect=run_command
        ):
            result = verifier.run_post_smoke_verification(
                build_dir=build_dir,
                output_dir=output_dir,
            )
        return result, calls

    def test_success_runs_exact_fixed_gate_groups_and_publishes_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, build_dir, output_dir = self._setup_paths(root)
            _write_report(output_dir, _report())

            result, calls = self._run_with_successful_commands(
                source_root=source_root,
                build_dir=build_dir,
                output_dir=output_dir,
            )

            self.assertTrue(result.smoke_pass)
            self.assertTrue(result.task4b_pass)
            self.assertEqual(result.h_exec, H_EXEC)
            self.assertEqual(len(result.commands), 14)
            self.assertEqual(
                {command.command_id for command in result.commands},
                {
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
                },
            )
            self.assertTrue(all(command.status == "PASS" for command in result.commands))
            for command in result.commands:
                self.assertIsInstance(command.argv, tuple)
                self.assertIsInstance(command.exit_code, int)
                self.assertRegex(command.stdout_sha256, r"^[0-9a-f]{64}$")
                self.assertRegex(command.stderr_sha256, r"^[0-9a-f]{64}$")
            self.assertTrue(all(cwd == source_root for _, cwd in calls))
            self.assertTrue(
                all(
                    "task4b_cuda_smoke.py" not in command.argv
                    for command in result.commands
                )
            )
            admitted_commands = [
                command
                for command in result.commands
                if command.command_id == "admitted-forward"
            ]
            self.assertEqual(len(admitted_commands), 2)
            probe_path = str((build_dir / "phase6_task4_corpus_probe.exe").resolve())
            self.assertEqual(
                [command.argv[-1] for command in admitted_commands],
                [probe_path, probe_path],
            )
            self.assertEqual(
                (output_dir / "task4b-execution-report.json").read_bytes(),
                _canonical_json(_report()),
            )
            for filename in (
                "task4b-verification.json",
                "task4b-acceptance.json",
                "task4b-acceptance.md",
            ):
                self.assertTrue((output_dir / filename).is_file())
                self.assertFalse((output_dir / (filename + ".tmp")).exists())

            verification = json.loads(
                (output_dir / "task4b-verification.json").read_text(encoding="utf-8")
            )
            acceptance = json.loads(
                (output_dir / "task4b-acceptance.json").read_text(encoding="utf-8")
            )
            for payload in (verification, acceptance):
                self.assertEqual(payload["H_exec"], H_EXEC)
                self.assertEqual(payload["corpus_probe_sha256"], PROBE_SHA256)
                self.assertEqual(payload["corpus_probe_source_commit"], H_EXEC)
                self.assertEqual(payload["checkpoint_identity"], CHECKPOINT_IDENTITY)
                self.assertEqual(
                    payload["smoke_evidence_identity"], SMOKE_EVIDENCE_IDENTITY
                )
                self.assertTrue(payload["SMOKE_PASS"])
                self.assertTrue(payload["TASK4B_PASS"])
                self.assertEqual(len(payload["commands"]), 14)

    def test_failed_smoke_never_claims_acceptance_or_runs_gates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            output_dir = source_root / "docs" / "p6" / "task4b"
            _write_report(output_dir, _report(smoke_pass=False))
            with mock.patch.object(
                verifier, "_canonical_source_root", return_value=source_root
            ), mock.patch.object(
                verifier,
                "_run_git",
                side_effect=_git_snapshot(),
            ), mock.patch.object(verifier.subprocess, "run") as command:
                result = verifier.run_post_smoke_verification(
                    build_dir=root / "missing-build",
                    output_dir=output_dir,
                )

            self.assertFalse(result.smoke_pass)
            self.assertFalse(result.task4b_pass)
            self.assertEqual(result.commands, ())
            command.assert_not_called()
            acceptance = json.loads(
                (output_dir / "task4b-acceptance.json").read_text(encoding="utf-8")
            )
            self.assertFalse(acceptance["SMOKE_PASS"])
            self.assertFalse(acceptance["TASK4B_PASS"])
            self.assertEqual(acceptance["gate_statuses"]["task4-focused-python"], "NOT_RUN")

    def test_failing_gate_records_failure_without_positive_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, build_dir, output_dir = self._setup_paths(root)
            _write_report(output_dir, _report())

            def fail_rules_bundle(command, cwd):
                del cwd
                if any("verify_rules_bundle.py" in argument for argument in command):
                    return subprocess.CompletedProcess(command, 7, stdout="", stderr="failed")
                return subprocess.CompletedProcess(command, 0, stdout="out", stderr="")

            result, _ = self._run_with_successful_commands(
                source_root=source_root,
                build_dir=build_dir,
                output_dir=output_dir,
                command_side_effect=fail_rules_bundle,
            )

            self.assertTrue(result.smoke_pass)
            self.assertFalse(result.task4b_pass)
            failed = [
                command
                for command in result.commands
                if command.command_id == "rules-bundle"
            ]
            self.assertEqual(len(failed), 1)
            self.assertEqual(failed[0].status, "FAIL")
            acceptance = json.loads(
                (output_dir / "task4b-acceptance.json").read_text(encoding="utf-8")
            )
            self.assertFalse(acceptance["TASK4B_PASS"])
            self.assertEqual(acceptance["gate_statuses"]["rules-bundle"], "FAIL")

    def test_source_head_change_fails_before_first_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, build_dir, output_dir = self._setup_paths(root)
            _write_report(output_dir, _report())
            with mock.patch.object(
                verifier, "_canonical_source_root", return_value=source_root
            ), mock.patch.object(
                verifier,
                "_run_git",
                side_effect=_git_snapshot(expected_head="b" * 40),
            ), mock.patch.object(verifier.subprocess, "run") as command:
                with self.assertRaises(verifier.Task4BVerificationError) as raised:
                    verifier.run_post_smoke_verification(
                        build_dir=build_dir,
                        output_dir=output_dir,
                    )

            self.assertEqual(raised.exception.code, "POST_SMOKE_HEAD_CHANGED")
            command.assert_not_called()

    def test_source_integrity_rejects_tracked_and_unexpected_untracked_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            output_dir = source_root / "docs" / "p6" / "task4b"

            for kwargs, code in (
                ({"diff": "tools/phase6/task4b_verify.py\n"}, "POST_SMOKE_TRACKED_SOURCE_CHANGED"),
                ({"cached_diff": "tests/phase6/other_test.py\n"}, "POST_SMOKE_TRACKED_SOURCE_CHANGED"),
                ({"status": "?? outside.txt\n"}, "POST_SMOKE_UNEXPECTED_UNTRACKED_FILE"),
                ({"status": "?? docs/p6/task4b/unexpected.txt\n"}, "POST_SMOKE_UNEXPECTED_UNTRACKED_FILE"),
            ):
                with self.subTest(kwargs=kwargs):
                    with mock.patch.object(
                        verifier,
                        "_run_git",
                        side_effect=_git_snapshot(**kwargs),
                    ):
                        with self.assertRaises(verifier.Task4BVerificationError) as raised:
                            verifier._verify_h_exec_source_integrity(
                                source_root=source_root,
                                expected_head=H_EXEC,
                                output_dir=output_dir,
                                allowed_output_files=verifier.ALLOWED_OUTPUT_FILES,
                            )
                    self.assertEqual(raised.exception.code, code)

            with mock.patch.object(
                verifier,
                "_run_git",
                side_effect=_git_snapshot(
                    status="?? docs/p6/task4b/task4b-verification.json\n"
                ),
            ):
                verifier._verify_h_exec_source_integrity(
                    source_root=source_root,
                    expected_head=H_EXEC,
                    output_dir=output_dir,
                    allowed_output_files=verifier.ALLOWED_OUTPUT_FILES,
                )

    def test_source_only_mode_checks_integrity_without_running_gates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            output_dir = source_root / "docs" / "p6" / "task4b"
            _write_report(output_dir, _report())
            with mock.patch.object(
                verifier, "_canonical_source_root", return_value=source_root
            ), mock.patch.object(
                verifier,
                "_run_git",
                side_effect=_git_snapshot(),
            ), mock.patch.object(verifier.subprocess, "run") as command:
                verifier.check_source_integrity_from_report(output_dir=output_dir)
            command.assert_not_called()

    def test_probe_hash_mismatch_fails_before_admitted_forward_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, build_dir, output_dir = self._setup_paths(root)
            payload = _report()
            payload["corpus_probe_sha256"] = "0" * 64
            _write_report(output_dir, payload)
            with mock.patch.object(
                verifier, "_canonical_source_root", return_value=source_root
            ), mock.patch.object(
                verifier,
                "_run_git",
                side_effect=_git_snapshot(),
            ), mock.patch.object(verifier.subprocess, "run") as command:
                with self.assertRaises(verifier.Task4BVerificationError) as raised:
                    verifier.run_post_smoke_verification(
                        build_dir=build_dir,
                        output_dir=output_dir,
                    )
            self.assertEqual(raised.exception.code, "PROBE_HASH_MISMATCH")
            command.assert_not_called()

    def test_preflight_identity_mutation_fails_before_any_gate(self):
        mutations = (
            ("framework_version", "mutated-framework"),
            ("torch_cuda_version_reported", "mutated-cuda"),
            ("cuda_available", False),
            ("cpu_fallback", True),
            ("cuda_preflight_identity", "phase6_cuda_preflight.v1." + "0" * 64),
            ("device_type", "cpu"),
            ("device_index", 1),
            ("gpu_name", "other GPU"),
            ("capability_major", 9),
            ("capability_minor", 0),
            ("device_count", 2),
            ("backend_identity", "mutated-backend"),
            ("distributed_strategy", "mutated-distributed"),
            ("world_size", 2),
            ("deterministic_algorithms", False),
            ("deterministic_warn_only", True),
            ("float32_matmul_precision", "high"),
        )
        for field, value in mutations:
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source_root, build_dir, output_dir = self._setup_paths(root)
                payload = _report()
                payload[field] = value
                _write_report(output_dir, payload)
                with mock.patch.object(
                    verifier, "_canonical_source_root", return_value=source_root
                ), mock.patch.object(
                    verifier,
                    "_run_git",
                    side_effect=_git_snapshot(),
                ), mock.patch.object(verifier.subprocess, "run") as command:
                    with self.assertRaises(verifier.Task4BVerificationError):
                        verifier.run_post_smoke_verification(
                            build_dir=build_dir,
                            output_dir=output_dir,
                        )
                command.assert_not_called()

    def test_cli_source_integrity_mode_does_not_run_verification(self):
        with mock.patch.object(
            verifier, "check_source_integrity_from_report"
        ) as check_source_integrity, mock.patch.object(
            verifier, "run_post_smoke_verification"
        ) as run_verification:
            with mock.patch("builtins.print"):
                result = verifier.main(
                    [
                        "--check-source-integrity",
                        "--output-dir",
                        "docs/p6/task4b",
                    ]
                )

        self.assertEqual(result, 0)
        check_source_integrity.assert_called_once_with(
            output_dir=Path("docs/p6/task4b")
        )
        run_verification.assert_not_called()

    def test_cli_gate_mode_returns_result_status_without_smoke_invocation(self):
        verification = verifier.Task4BVerificationResultV1(
            h_exec=H_EXEC,
            smoke_pass=True,
            task4b_pass=False,
            commands=(),
            execution_report_sha256="0" * 64,
            verification_json='{"TASK4B_PASS":false}',
        )
        with mock.patch.object(
            verifier, "run_post_smoke_verification", return_value=verification
        ) as run_verification, mock.patch("builtins.print") as print_output:
            result = verifier.main(
                [
                    "--build-dir",
                    "build/task4b-cuda-smoke",
                    "--output-dir",
                    "docs/p6/task4b",
                ]
            )

        self.assertEqual(result, 1)
        run_verification.assert_called_once_with(
            build_dir=Path("build/task4b-cuda-smoke"),
            output_dir=Path("docs/p6/task4b"),
        )
        print_output.assert_called_once_with(verification.verification_json)

    def test_json_serialization_rejects_nonfinite_values(self):
        with self.assertRaises(ValueError):
            verifier._canonical_json_bytes({"diagnostic": float("nan")})

    def test_nonzero_command_records_exit_and_output_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root, build_dir, output_dir = self._setup_paths(root)
            _write_report(output_dir, _report())

            def failing_command(command, cwd):
                del cwd
                return subprocess.CompletedProcess(
                    command,
                    3,
                    stdout="captured stdout",
                    stderr="captured stderr",
                )

            result, _ = self._run_with_successful_commands(
                source_root=source_root,
                build_dir=build_dir,
                output_dir=output_dir,
                command_side_effect=failing_command,
            )

            self.assertFalse(result.task4b_pass)
            first = result.commands[0]
            self.assertEqual(first.exit_code, 3)
            self.assertEqual(
                first.stdout_sha256,
                hashlib.sha256(b"captured stdout").hexdigest(),
            )
            self.assertEqual(
                first.stderr_sha256,
                hashlib.sha256(b"captured stderr").hexdigest(),
            )
            self.assertEqual(first.status, "FAIL")


if __name__ == "__main__":
    unittest.main()
