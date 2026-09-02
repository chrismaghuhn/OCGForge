import dataclasses
import contextlib
import hashlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import torch

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4_cuda
from tools.phase6 import task4b_cuda_smoke as smoke_cli
from tools.phase6 import task4_inference
from tools.phase6 import task4b_runner as runner


class _FakeLoss:
    def __init__(self) -> None:
        self.backward_calls = 0

    def backward(self) -> None:
        self.backward_calls += 1


class _FakeOptimizer:
    def __init__(self, *, raises: bool) -> None:
        self.raises = raises
        self.step_calls = 0

    def step(self) -> None:
        self.step_calls += 1
        if self.raises:
            raise RuntimeError("synthetic optimizer failure")


class _SyntheticReport:
    def to_json(self) -> str:
        return '{"schema_id":"synthetic"}'


def _git_result(head: str, status: str):
    def result(_source_root: Path, *arguments: str):
        if arguments == ("rev-parse", "HEAD"):
            return head, ""
        if arguments == ("status", "--porcelain", "--untracked-files=all"):
            return status, ""
        raise AssertionError(f"unexpected git command: {arguments}")

    return result


def _write_cmake_cache(
    build_dir: Path,
    source_root: Path,
    *,
    generator: str = "Ninja",
    build_type: str = "Release",
) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    source_text = str(source_root).replace("\\", "/")
    (build_dir / "CMakeCache.txt").write_text(
        "\n".join(
            (
                f"CMAKE_HOME_DIRECTORY:INTERNAL={source_text}",
                f"CMAKE_GENERATOR:INTERNAL={generator}",
                f"CMAKE_BUILD_TYPE:STRING={build_type}",
            )
        )
        + "\n",
        encoding="utf-8",
    )


def _admitted_corpus(samples, *, authority=None):
    corpus = codec.DerivedCorpusV1(
        source_dataset_identity="5" * 64,
        split_identity="phase6_dataset_split.v1." + "6" * 64,
        derivation_contract_identity=codec.NUMERIC_PROJECTION_CONTRACT_ID,
        card_vocabulary_identity="model_card_vocabulary.v1." + "7" * 64,
        episode_ids=("2" * 64,),
        samples=tuple(samples),
    )
    return runner.AdmittedCorpusArtifactsV1(
        probe_sha256="a" * 64,
        corpus_bytes=b"corpus",
        authority_bytes=b"authority",
        corpus=corpus,
        authority=object() if authority is None else authority,
    )


def _finalization_stub() -> runner.Task4BFinalizationV1:
    return runner.Task4BFinalizationV1(
        exported_checkpoint=mock.Mock(),
        completion_receipt=mock.Mock(),
        training_run_manifest=mock.Mock(),
        smoke_evidence=mock.Mock(),
        training_run_identity="phase6_training_run.v1." + "1" * 64,
        checkpoint_identity="phase6_checkpoint.v1." + "2" * 64,
        smoke_evidence_identity="phase6_task4b_smoke_evidence.v1." + "3" * 64,
    )


def _training_result_stub():
    return mock.Mock(
        preflight=mock.Mock(),
        actual_optimizer_steps=500,
        gpu_memory_before=1,
        gpu_memory_peak=2,
        gpu_memory_after=1,
        initial_loss=3.0,
        final_loss=2.0,
    )


def _attested_preflight() -> task4_cuda.CudaPreflightResultV1:
    return task4_cuda.CudaPreflightResultV1(
        device_type=codec.EXPECTED_DEVICE_TYPE,
        device_index=codec.EXPECTED_DEVICE_INDEX,
        gpu_name=codec.EXPECTED_GPU_NAME,
        framework_version="synthetic-pytorch",
        torch_cuda_version_reported="12.6",
        device_count=1,
        capability_major=8,
        capability_minor=9,
        _attestation=task4_cuda._CUDA_PREFLIGHT_ATTESTATION,
    )


def _sample(identity_suffix: str, partition: str, action_suffix: str) -> codec.CorpusSampleV1:
    action_key = f"public_action.v1.{action_suffix}"
    sample = codec.CorpusSampleV1(
        bc_sample_identity="",
        trajectory_record_id="trajectory_record.v1." + "1" * 64,
        episode_semantic_id="2" * 64,
        public_semantic_decision_id="3" * 64,
        model_input_identity="model_input.v1." + "4" * 64,
        selected_public_action_key=action_key,
        partition=partition,
        candidate_ordinal=0,
        ordered_candidate_domain_identity=codec.ordered_candidate_domain_identity((action_key,)),
        state_rows=((0.0,) * codec.STATE_ROW_WIDTH,),
        candidate_rows=((0.0,) * codec.CANDIDATE_ROW_WIDTH,),
        routing_keys=(action_key,),
    )
    return dataclasses.replace(
        sample,
        bc_sample_identity=codec.BC_SAMPLE_IDENTITY_PREFIX + identity_suffix,
    )


class Task4BRunnerTests(unittest.TestCase):
    def test_cuda_smoke_cli_help_has_only_build_and_output_paths(self):
        with mock.patch.object(smoke_cli.task4b_runner, "run_task4b_smoke") as run:
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                with self.assertRaises(SystemExit) as raised:
                    smoke_cli.main(["--help"])

        self.assertEqual(raised.exception.code, 0)
        help_text = stdout.getvalue()
        self.assertIn("--build-dir", help_text)
        self.assertIn("--output-dir", help_text)
        for forbidden in (
            "--max-optimizer-steps",
            "--actual-optimizer-steps",
            "--gpu-memory-before",
            "--gpu-memory-peak",
            "--gpu-memory-after",
            "--checkpoint-identity",
            "--smoke-evidence-identity",
        ):
            self.assertNotIn(forbidden, help_text)
        run.assert_not_called()

    def test_cuda_smoke_cli_rejects_evidence_step_override_before_runner(self):
        with mock.patch.object(smoke_cli.task4b_runner, "run_task4b_smoke") as run:
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                with self.assertRaises(SystemExit) as raised:
                    smoke_cli.main(
                        [
                            "--build-dir",
                            "build/task4b-cuda-smoke",
                            "--output-dir",
                            "docs/p6/task4b",
                            "--actual-optimizer-steps",
                            "1",
                        ]
                    )

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("unrecognized arguments", stderr.getvalue())
        run.assert_not_called()

    def test_cuda_smoke_cli_delegates_only_build_and_output_paths(self):
        result = mock.Mock(report_json='{"SMOKE_PASS":true}')
        with mock.patch.object(
            smoke_cli.task4b_runner,
            "run_task4b_smoke",
            return_value=result,
        ) as run:
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = smoke_cli.main(
                    [
                        "--build-dir",
                        "build/task4b-cuda-smoke",
                        "--output-dir",
                        "docs/p6/task4b",
                    ]
                )

        self.assertEqual(exit_code, 0)
        run.assert_called_once_with(
            build_dir=Path("build/task4b-cuda-smoke"),
            output_dir=Path("docs/p6/task4b"),
        )
        self.assertEqual(stdout.getvalue(), result.report_json + "\n")

    def test_cuda_smoke_cli_reports_structured_runner_failure(self):
        failure = runner.Task4BSmokeError(
            "SYNTHETIC_FAILURE",
            "synthetic failure",
            report=_SyntheticReport(),
        )
        with mock.patch.object(
            smoke_cli.task4b_runner,
            "run_task4b_smoke",
            side_effect=failure,
        ):
            stderr = io.StringIO()
            with contextlib.redirect_stderr(stderr):
                exit_code = smoke_cli.main(
                    [
                        "--build-dir",
                        "build/task4b-cuda-smoke",
                        "--output-dir",
                        "docs/p6/task4b",
                    ]
                )

        self.assertEqual(exit_code, 1)
        self.assertEqual(stderr.getvalue(), failure.report_json + "\n")

    def test_smoke_error_report_json_uses_fallback_or_attached_report(self):
        fallback = runner.Task4BSmokeError("SYNTHETIC", "fallback message")
        self.assertIsNone(fallback.report)
        self.assertEqual(fallback.report_json, str(fallback))

        report = _SyntheticReport()
        attached = runner.Task4BSmokeError(
            "SYNTHETIC",
            "attached message",
            report=report,
        )
        self.assertIs(attached.report, report)
        self.assertEqual(attached.report_json, report.to_json())

    def test_clean_h_exec_rejects_invalid_or_dirty_source(self):
        source_root = Path("C:/source")
        for invalid_head in ("A" * 40, "a" * 39, "g" * 40):
            with self.subTest(head=invalid_head):
                with mock.patch.object(
                    runner,
                    "_run_git",
                    side_effect=_git_result(invalid_head, ""),
                ):
                    with self.assertRaises(runner.Task4BSmokeError) as raised:
                        runner._verify_clean_h_exec(source_root)
                self.assertEqual(raised.exception.code, "INVALID_H_EXEC")

        with mock.patch.object(
            runner,
            "_run_git",
            side_effect=_git_result("a" * 40, " M tools/phase6/task4b_runner.py\n"),
        ):
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner._verify_clean_h_exec(source_root)
        self.assertEqual(raised.exception.code, "DIRTY_H_EXEC")

    def test_clean_h_exec_accepts_only_empty_porcelain_status(self):
        source_root = Path("C:/source")
        with mock.patch.object(
            runner,
            "_run_git",
            side_effect=_git_result("a" * 40, ""),
        ):
            self.assertEqual(runner._verify_clean_h_exec(source_root), "a" * 40)

        with mock.patch.object(
            runner,
            "_run_git",
            side_effect=_git_result("a" * 40, " \n"),
        ):
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner._verify_clean_h_exec(source_root)
        self.assertEqual(raised.exception.code, "DIRTY_H_EXEC")

    def test_cmake_cache_requires_canonical_source_ninja_and_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            build_dir = root / "build"
            _write_cmake_cache(build_dir, source_root)
            runner._verify_cmake_configuration(build_dir, source_root)

            for cache_source, kwargs in (
                (root / "other-source", {}),
                (source_root, {"generator": "Visual Studio"}),
                (source_root, {"build_type": "Debug"}),
            ):
                with self.subTest(cache_source=cache_source, kwargs=kwargs):
                    _write_cmake_cache(build_dir, cache_source, **kwargs)
                    with self.assertRaises(runner.Task4BSmokeError) as raised:
                        runner._verify_cmake_configuration(build_dir, source_root)
                    self.assertEqual(raised.exception.code, "STALE_BUILD_SOURCE")

    def test_probe_path_and_hash_are_bound_to_one_build_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_dir = root / "build"
            build_dir.mkdir()
            probe = build_dir / "phase6_task4_corpus_probe.exe"
            probe.write_bytes(b"x")
            self.assertEqual(
                runner._sha256_file(probe), hashlib.sha256(b"x").hexdigest()
            )
            self.assertEqual(runner._resolve_probe_binary(build_dir), probe.resolve())

            outside = root / "outside.exe"
            outside.write_bytes(b"x")
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner._require_probe_inside_build(build_dir, outside)
            self.assertEqual(raised.exception.code, "PROBE_OUTSIDE_BUILD")

            (build_dir / "nested").mkdir()
            (build_dir / "nested" / "phase6_task4_corpus_probe.exe").write_bytes(b"y")
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner._resolve_probe_binary(build_dir)
            self.assertEqual(raised.exception.code, "AMBIGUOUS_PROBE_BINARY")

    def test_authoritative_composition_rejects_dirty_h_exec_before_build_or_probe(self):
        source_root = Path("C:/source")
        dirty = runner.Task4BSmokeError("DIRTY_H_EXEC", "source checkout is not clean")
        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", side_effect=dirty), \
             mock.patch.object(runner, "_build_and_attest_probe") as build_probe, \
             mock.patch.object(runner, "_run_authoritative_corpus_probe") as run_probe, \
             mock.patch.object(runner, "_write_atomic_bytes") as write_report:
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner.run_task4b_smoke(
                    build_dir=source_root / "build",
                    output_dir=source_root / "output",
                )

        self.assertEqual(raised.exception.code, dirty.code)
        self.assertIsNotNone(raised.exception.report)
        build_probe.assert_not_called()
        run_probe.assert_not_called()
        write_report.assert_called_once()

    def test_authoritative_composition_connects_clean_head_to_probe_admission(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        probe_hash = "a" * 64
        admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
        training_result = _training_result_stub()
        calls = []

        def record_clean(_source_root):
            calls.append("clean")
            return "b" * 40

        def record_build(_source_root, _build_dir):
            calls.append("build")
            return probe, probe_hash

        def record_probe(
            probe_path,
            temporary_dir,
            *,
            source_root: Path,
            expected_probe_sha256,
        ):
            calls.append("probe")
            self.assertEqual(probe_path, probe)
            self.assertEqual(source_root, Path("C:/source"))
            self.assertEqual(expected_probe_sha256, probe_hash)
            self.assertTrue(temporary_dir.is_dir())
            return admitted

        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", side_effect=record_clean), \
             mock.patch.object(runner, "_build_and_attest_probe", side_effect=record_build), \
             mock.patch.object(runner, "_run_authoritative_corpus_probe", side_effect=record_probe), \
             mock.patch.object(runner, "_run_cuda_training", return_value=training_result), \
             mock.patch.object(runner, "_execution_report_preflight_fields", return_value={}), \
             mock.patch.object(runner, "_finalize_task4b_artifacts", return_value=_finalization_stub()), \
             mock.patch.object(runner, "_write_smoke_artifacts"), \
             mock.patch.object(runner, "_write_execution_report"), \
             mock.patch.object(codec, "canonical_training_run_manifest_bytes", return_value=b"manifest"), \
             mock.patch.object(codec, "canonical_smoke_evidence_bytes", return_value=b"evidence"):
            result = runner.run_task4b_smoke(
                build_dir=source_root / "build",
                output_dir=source_root / "output",
            )

        self.assertEqual(calls, ["clean", "build", "probe"])
        self.assertEqual(result.report.h_exec, "b" * 40)
        self.assertEqual(result.report.corpus_probe_sha256, probe_hash)
        self.assertEqual(
            result.report.source_dataset_identity,
            admitted.corpus.source_dataset_identity,
        )
        self.assertTrue(result.report.smoke_pass)
        self.assertFalse(result.report.task4b_pass)
        self.assertEqual(result.report_json, result.report.to_json())
        self.assertEqual(result.corpus_bytes, admitted.corpus_bytes)
        self.assertEqual(result.authority_bytes, admitted.authority_bytes)

    def test_composition_derives_admitted_metadata_counts_and_train_order(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        probe_hash = "a" * 64
        train_later = _sample("b" * 64, "train", "01")
        train_earlier = _sample("a" * 64, "train", "00")
        validation = _sample("c" * 64, "validation", "02")
        test = _sample("d" * 64, "test", "03")
        admitted = _admitted_corpus(
            (test, train_later, validation, train_earlier),
            authority=mock.Mock(
                source_dataset_identity="caller-dataset",
                split_identity="caller-split",
                card_vocabulary_identity="caller-vocabulary",
            ),
        )
        training_result = _training_result_stub()

        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
             mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, probe_hash)), \
             mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted), \
             mock.patch.object(runner, "_run_cuda_training", return_value=training_result), \
             mock.patch.object(runner, "_execution_report_preflight_fields", return_value={}), \
             mock.patch.object(runner, "_finalize_task4b_artifacts", return_value=_finalization_stub()), \
             mock.patch.object(runner, "_write_smoke_artifacts"), \
             mock.patch.object(runner, "_write_execution_report"), \
             mock.patch.object(codec, "canonical_training_run_manifest_bytes", return_value=b"manifest"), \
             mock.patch.object(codec, "canonical_smoke_evidence_bytes", return_value=b"evidence"):
            result = runner.run_task4b_smoke(
                build_dir=source_root / "build",
                output_dir=source_root / "output",
            )

        self.assertEqual(result.report.source_dataset_identity, "5" * 64)
        self.assertEqual(
            result.report.dataset_split_identity,
            "phase6_dataset_split.v1." + "6" * 64,
        )
        self.assertEqual(
            result.report.card_vocabulary_identity,
            "model_card_vocabulary.v1." + "7" * 64,
        )
        self.assertEqual(result.report.train_sample_count, 2)
        self.assertEqual(result.report.validation_sample_count, 1)
        self.assertEqual(result.report.test_sample_count, 1)
        self.assertEqual(result.corpus_bytes, admitted.corpus_bytes)

    def test_empty_admitted_train_partition_fails_closed_after_admission(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        admitted = _admitted_corpus(
            (_sample("c" * 64, "validation", "02"), _sample("d" * 64, "test", "03"))
        )

        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
             mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
             mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted), \
             mock.patch.object(runner, "_write_atomic_bytes"):
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner.run_task4b_smoke(
                    build_dir=source_root / "build",
                    output_dir=source_root / "output",
                )

        self.assertEqual(raised.exception.code, "EMPTY_TRAIN_PARTITION")

    def test_composition_does_not_order_before_successful_admission(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        admission_failure = runner.Task4BSmokeError(
            "CORPUS_ADMISSION_FAILED",
            "synthetic admission failure",
        )
        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
             mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
             mock.patch.object(
                 runner,
                 "_run_authoritative_corpus_probe",
                 side_effect=admission_failure,
             ), \
             mock.patch.object(runner, "_write_atomic_bytes") as write_report, \
             mock.patch.object(runner, "_ordered_train_samples") as ordered:
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner.run_task4b_smoke(
                    build_dir=source_root / "build",
                    output_dir=source_root / "output",
                )

        self.assertEqual(raised.exception.code, admission_failure.code)
        self.assertIsNotNone(raised.exception.report)
        ordered.assert_not_called()
        write_report.assert_called_once()

    def test_cuda_preflight_is_after_admission_and_before_optimizer(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
        events = []

        def record_build(_source_root, _build_dir):
            events.append("build")
            return probe, "a" * 64

        def record_admission(*args, **kwargs):
            del args, kwargs
            events.append("admission")
            return admitted

        def fail_preflight():
            events.append("preflight")
            raise task4_cuda.CudaPreflightError(
                "CUDA_UNAVAILABLE",
                "synthetic CUDA failure",
            )

        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
             mock.patch.object(runner, "_build_and_attest_probe", side_effect=record_build), \
             mock.patch.object(runner, "_run_authoritative_corpus_probe", side_effect=record_admission), \
             mock.patch.object(runner.task4_cuda, "require_task4_cuda", side_effect=fail_preflight), \
             mock.patch.object(torch.optim, "Adam") as optimizer, \
             mock.patch.object(runner, "_write_atomic_bytes") as write_report:
            with self.assertRaises(task4_cuda.CudaPreflightError) as raised:
                runner.run_task4b_smoke(
                    build_dir=source_root / "build",
                    output_dir=source_root / "output",
                )

        self.assertEqual(raised.exception.code, "CUDA_UNAVAILABLE")
        self.assertEqual(raised.exception.actual_optimizer_steps, 0)
        self.assertEqual(events, ["build", "admission", "preflight"])
        optimizer.assert_not_called()
        write_report.assert_called_once()

    def test_preflight_failure_persists_no_cuda_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_root = root / "source"
            output_dir = root / "output"
            probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
            admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
            failure = task4_cuda.CudaPreflightError(
                "CUDA_UNAVAILABLE",
                "synthetic CUDA failure",
            )
            with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
                 mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
                 mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
                 mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted), \
                 mock.patch.object(runner.task4_cuda, "require_task4_cuda", side_effect=failure):
                with self.assertRaises(task4_cuda.CudaPreflightError):
                    runner.run_task4b_smoke(
                        build_dir=source_root / "build",
                        output_dir=output_dir,
                    )

            report = json.loads(
                (output_dir / "task4b-execution-report.json").read_text(
                    encoding="utf-8"
                )
            )
            for key in (
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
            ):
                self.assertIsNone(report[key])
            self.assertEqual(report["actual_optimizer_steps"], 0)
            self.assertEqual(report["error_code"], "CUDA_UNAVAILABLE")

    def test_attested_preflight_failure_preserves_all_preflight_fields(self):
        preflight = _attested_preflight()
        state = runner._ExecutionReportState()
        sample = _sample("a" * 64, "train", "00")

        with mock.patch.object(
            runner.task4_cuda,
            "require_task4_cuda",
            return_value=preflight,
        ), mock.patch.object(
            runner.task4_model,
            "Phase6TorchCandidateScorer",
            side_effect=RuntimeError("synthetic model construction failure"),
        ), mock.patch.object(torch.cuda, "manual_seed_all"):
            with self.assertRaises(runner.Task4BTrainingError) as raised:
                runner._run_cuda_training((sample,), report_state=state)

        self.assertEqual(raised.exception.actual_optimizer_steps, 0)
        for key, value in runner._execution_report_preflight_fields(preflight).items():
            self.assertEqual(state.report.to_dict()[key], value)

    def test_post_training_preflight_failure_preserves_reached_report_facts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output_dir = root / "output"
            source_root = root / "source"
            probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
            admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
            preflight = _attested_preflight()
            training_result = _training_result_stub()
            training_result.actual_optimizer_steps = 500
            training_result.gpu_memory_before = 101
            training_result.gpu_memory_peak = 202
            training_result.gpu_memory_after = 111
            training_result.initial_loss = 3.5
            training_result.final_loss = 2.5
            finalization_failure = task4_cuda.CudaPreflightError(
                "INVALID_SMOKE_EVIDENCE",
                "synthetic post-training preflight failure",
            )

            def complete_training(_ordered_samples, *, report_state):
                report_state.update(
                    **runner._execution_report_preflight_fields(preflight),
                    actual_optimizer_steps=500,
                    gpu_memory_before=101,
                    gpu_memory_peak=202,
                    gpu_memory_after=111,
                    initial_loss=3.5,
                    final_loss=2.5,
                )
                return training_result

            with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
                 mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
                 mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
                 mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted), \
                 mock.patch.object(runner, "_run_cuda_training", side_effect=complete_training), \
                 mock.patch.object(runner, "_finalize_task4b_artifacts", side_effect=finalization_failure):
                with self.assertRaises(task4_cuda.CudaPreflightError) as raised:
                    runner.run_task4b_smoke(
                        build_dir=source_root / "build",
                        output_dir=output_dir,
                    )

            self.assertIs(raised.exception, finalization_failure)
            report = json.loads(
                (output_dir / "task4b-execution-report.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(report["actual_optimizer_steps"], 500)
            for key, value in runner._execution_report_preflight_fields(preflight).items():
                self.assertEqual(report[key], value)
            self.assertEqual(report["GPU_MEMORY_BEFORE"], 101)
            self.assertEqual(report["GPU_MEMORY_PEAK"], 202)
            self.assertEqual(report["GPU_MEMORY_AFTER"], 111)
            self.assertEqual(report["initial_loss"], 3.5)
            self.assertEqual(report["final_loss"], 2.5)
            self.assertEqual(report["error_code"], "INVALID_SMOKE_EVIDENCE")
            self.assertFalse(report["SMOKE_PASS"])
            self.assertFalse(report["TASK4B_PASS"])
            self.assertIsNone(report["checkpoint_identity"])
            self.assertIsNone(report["smoke_evidence_identity"])


    def test_adam_configuration_is_explicit_and_frozen(self):
        model = mock.Mock()
        model.parameters.return_value = ()
        with mock.patch.object(torch.optim, "Adam", return_value="optimizer") as adam:
            self.assertEqual(runner._make_adam_optimizer(model), "optimizer")

        adam.assert_called_once_with(
            (),
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

    def test_training_tensor_placement_rejects_cpu_tensor_for_cuda_execution(self):
        value = torch.zeros((1, codec.STATE_ROW_WIDTH), dtype=torch.float32)
        with self.assertRaises(runner.Task4BTrainingError):
            runner._require_cuda_tensor(
                value,
                torch.device("cuda:0"),
                "state tensor",
            )

    def test_per_sample_validation_failure_preserves_current_step_count(self):
        sample = _sample("a" * 64, "train", "00")
        with self.assertRaises(runner.Task4BTrainingError) as raised:
            runner._loss_for_sample(
                mock.Mock(),
                sample,
                torch.device("cpu"),
                237,
            )
        self.assertEqual(raised.exception.actual_optimizer_steps, 237)

    def test_float_state_dtype_validation_fails_closed(self):
        with self.assertRaises(runner.Task4BTrainingError) as raised:
            runner._require_tensor_dtype_and_finiteness(
                torch.zeros((1, codec.STATE_ROW_WIDTH), dtype=torch.float64),
                "state tensor",
                expected_dtype=torch.float32,
                require_finite=True,
                actual_optimizer_steps=41,
            )
        self.assertEqual(raised.exception.actual_optimizer_steps, 41)

    def test_nonfinite_float_candidate_validation_fails_closed(self):
        with self.assertRaises(runner.Task4BTrainingError) as raised:
            runner._require_tensor_dtype_and_finiteness(
                torch.tensor(
                    [[float("nan")] + [0.0] * (codec.CANDIDATE_ROW_WIDTH - 1)],
                    dtype=torch.float32,
                ),
                "candidate tensor",
                expected_dtype=torch.float32,
                require_finite=True,
                actual_optimizer_steps=42,
            )
        self.assertEqual(raised.exception.actual_optimizer_steps, 42)

    def test_label_and_mask_dtype_validation_fails_closed(self):
        with self.assertRaises(runner.Task4BTrainingError) as label_raised:
            runner._require_tensor_dtype_and_finiteness(
                torch.tensor([0.0], dtype=torch.float32),
                "label tensor",
                expected_dtype=torch.long,
                require_finite=False,
                actual_optimizer_steps=43,
            )
        self.assertEqual(label_raised.exception.actual_optimizer_steps, 43)

        with self.assertRaises(runner.Task4BTrainingError) as mask_raised:
            runner._require_tensor_dtype_and_finiteness(
                torch.ones((1, 2), dtype=torch.uint8),
                "candidate mask",
                expected_dtype=torch.bool,
                require_finite=False,
                actual_optimizer_steps=44,
            )
        self.assertEqual(mask_raised.exception.actual_optimizer_steps, 44)

    def test_execution_report_has_required_fields_and_rejects_nonfinite_json(self):
        report = runner.Task4BExecutionReportV1()
        payload = json.loads(report.to_json())
        self.assertEqual(payload["schema_id"], "ocgforge.phase6.task4b.execution_report.v1")
        for key in (
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
        ):
            self.assertIn(key, payload)
        self.assertEqual(
            set(payload),
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
            },
        )
        with self.assertRaises(ValueError):
            dataclasses.replace(report, initial_loss=float("nan")).to_json()

    def test_final_smoke_result_has_only_value_owned_contract_fields(self):
        expected = {
            "report",
            "report_json",
            "corpus_bytes",
            "authority_bytes",
            "checkpoint_bytes",
            "training_run_manifest_bytes",
            "smoke_evidence_bytes",
            "completion_receipt",
        }
        self.assertEqual(
            {field.name for field in dataclasses.fields(runner.Task4BSmokeRunResult)},
            expected,
        )
        self.assertNotIn("training_result", expected)
        self.assertNotIn("finalization", expected)
        self.assertNotIn("execution_report", expected)
        result = runner.Task4BSmokeRunResult(
            report=runner.Task4BExecutionReportV1(),
            report_json="{}",
            corpus_bytes=b"corpus",
            authority_bytes=b"authority",
            checkpoint_bytes=b"checkpoint",
            training_run_manifest_bytes=b"manifest",
            smoke_evidence_bytes=b"evidence",
            completion_receipt=mock.Mock(),
        )
        self.assertFalse(hasattr(result, "training_result"))
        self.assertFalse(hasattr(result, "model"))

    def test_atomic_bytes_writer_publishes_final_file_without_temp_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "task4b-execution-report.json"
            runner._write_atomic_bytes(path, b"report")
            self.assertEqual(path.read_bytes(), b"report")
            self.assertFalse(path.with_name(path.name + ".tmp").exists())

    def test_task4_finalization_uses_export_completion_and_evidence_paths(self):
        sample = _sample("a" * 64, "train", "00")
        admitted = _admitted_corpus((sample,))
        run_result = mock.Mock(
            admitted_corpus=admitted,
            ordered_train_samples=(sample,),
            h_exec="b" * 40,
        )
        training_result = mock.Mock(
            model=mock.Mock(),
            preflight=mock.Mock(),
            actual_optimizer_steps=500,
            gpu_memory_before=1,
            gpu_memory_peak=2,
            gpu_memory_after=1,
        )
        exported = mock.Mock(
            checkpoint_identity="phase6_checkpoint.v1." + "a" * 64,
            artifact_bytes=b"checkpoint",
        )
        numeric_input = mock.Mock()
        request = mock.Mock()
        receipt = mock.Mock()
        manifest = mock.Mock()
        evidence = mock.Mock()

        with mock.patch.object(
            task4_inference,
            "export_canonical_checkpoint",
            return_value=exported,
        ) as export, mock.patch.object(
            codec,
            "decode_checkpoint_artifact",
        ) as decode, mock.patch.object(
            codec,
            "make_numeric_model_input",
            return_value=numeric_input,
        ) as make_numeric, mock.patch.object(
            codec,
            "make_inference_request",
            return_value=request,
        ) as make_request, mock.patch.object(
            task4_inference,
            "issue_task4b_completion_receipt",
            return_value=receipt,
        ) as issue, mock.patch.object(
            codec,
            "default_training_run_manifest",
            return_value=manifest,
        ) as default_manifest, mock.patch.object(
            task4_cuda,
            "finalize_training_run_manifest_from_cuda_preflight",
            return_value=manifest,
        ) as finalize_manifest, mock.patch.object(
            task4_cuda,
            "smoke_evidence_from_cuda_preflight",
            return_value=evidence,
        ) as smoke_evidence, mock.patch.object(
            codec,
            "canonical_training_run_manifest_bytes",
            return_value=b"manifest",
        ), mock.patch.object(
            codec,
            "canonical_smoke_evidence_bytes",
            return_value=b"evidence",
        ), mock.patch.object(
            codec,
            "training_run_identity",
            return_value="phase6_training_run.v1." + "c" * 64,
        ), mock.patch.object(
            codec,
            "smoke_evidence_identity",
            return_value="phase6_task4b_smoke_evidence.v1." + "d" * 64,
        ):
            result = runner._finalize_task4b_artifacts(run_result, training_result)

        export.assert_called_once_with(
            training_result.model,
            source_dataset_identity=admitted.corpus.source_dataset_identity,
            dataset_split_identity=admitted.corpus.split_identity,
            card_vocabulary_identity=admitted.corpus.card_vocabulary_identity,
        )
        decode.assert_called_once_with(exported.artifact_bytes)
        make_numeric.assert_called_once()
        make_request.assert_called_once_with(
            checkpoint_identity=exported.checkpoint_identity,
            model_input=numeric_input,
        )
        issue.assert_called_once()
        default_manifest.assert_called_once()
        finalize_manifest.assert_called_once_with(
            training_result.preflight,
            manifest,
            final_exported_checkpoint_identity=exported.checkpoint_identity,
        )
        smoke_evidence.assert_called_once_with(
            training_result.preflight,
            manifest,
            receipt,
            actual_optimizer_steps=500,
            gpu_memory_before=1,
            gpu_memory_peak=2,
            gpu_memory_after=1,
        )
        self.assertIs(result.exported_checkpoint, exported)
        self.assertIs(result.completion_receipt, receipt)

    def test_training_failure_persists_execution_report_with_actual_count(self):
        with tempfile.TemporaryDirectory() as directory:
            output_dir = Path(directory) / "output"
            source_root = Path(directory) / "source"
            probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
            admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
            preflight = _attested_preflight()
            failure = runner.Task4BTrainingError(
                "CUDA_TRAINING_FAILED",
                "synthetic failure",
                237,
            )

            def fail_after_attested_facts(_ordered_samples, *, report_state):
                report_state.update(
                    **runner._execution_report_preflight_fields(preflight),
                    actual_optimizer_steps=237,
                    gpu_memory_before=101,
                    gpu_memory_peak=202,
                    initial_loss=3.5,
                )
                raise failure

            with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
                 mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
                 mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
                 mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted), \
                 mock.patch.object(runner, "_run_cuda_training", side_effect=fail_after_attested_facts):
                with self.assertRaises(runner.Task4BTrainingError) as raised:
                    runner.run_task4b_smoke(
                        build_dir=source_root / "build",
                        output_dir=output_dir,
                    )

            self.assertIs(raised.exception, failure)
            report = json.loads(
                (output_dir / "task4b-execution-report.json").read_text(encoding="utf-8")
            )
            self.assertEqual(report["actual_optimizer_steps"], 237)
            self.assertEqual(report["error_code"], "CUDA_TRAINING_FAILED")
            for key, value in runner._execution_report_preflight_fields(preflight).items():
                self.assertEqual(report[key], value)
            self.assertEqual(report["GPU_MEMORY_BEFORE"], 101)
            self.assertEqual(report["GPU_MEMORY_PEAK"], 202)
            self.assertEqual(report["initial_loss"], 3.5)
            self.assertIsNone(report["GPU_MEMORY_AFTER"])
            self.assertIsNone(report["final_loss"])
            self.assertFalse(report["SMOKE_PASS"])
            self.assertFalse(report["TASK4B_PASS"])
            self.assertIsNone(report["checkpoint_identity"])
            self.assertIsNone(report["smoke_evidence_identity"])

    def test_any_smoke_publication_failure_persists_failure_semantics(self):
        publication_paths = (
            "corpus.p6c",
            "corpus.authority.p6a",
            "checkpoint.p6k",
            "training-run-manifest.p6m",
            "smoke-evidence.p6e",
            "completion-receipt.json",
            "task4b-execution-report.json",
        )
        for failure_name in publication_paths:
            with self.subTest(failure_name=failure_name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output_dir = root / "output"
                source_root = root / "source"
                probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
                admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
                preflight = _attested_preflight()
                training_result = _training_result_stub()
                training_result.actual_optimizer_steps = 500
                training_result.gpu_memory_before = 101
                training_result.gpu_memory_peak = 202
                training_result.gpu_memory_after = 111
                training_result.initial_loss = 3.5
                training_result.final_loss = 2.5
                finalization = _finalization_stub()
                finalization.exported_checkpoint.artifact_bytes = b"checkpoint"
                receipt = finalization.completion_receipt
                receipt.checkpoint_identity = finalization.checkpoint_identity
                receipt.model_input_identity = "model_input.v1." + "4" * 64
                receipt.ordered_candidate_domain_identity = "candidate_domain.v1." + "5" * 64
                receipt.request_identity = "inference_request.v1." + "6" * 64
                receipt.response_identity = "inference_response.v1." + "7" * 64
                receipt.fresh_checkpoint_reload = True
                receipt.deterministic_frozen_inference = True

                def complete_training(_ordered_samples, *, report_state):
                    report_state.update(
                        **runner._execution_report_preflight_fields(preflight),
                        actual_optimizer_steps=500,
                        gpu_memory_before=101,
                        gpu_memory_peak=202,
                        gpu_memory_after=111,
                        initial_loss=3.5,
                        final_loss=2.5,
                    )
                    return training_result

                original_write = runner._write_atomic_bytes
                report_write_attempts = 0

                def fail_publication(path, data):
                    nonlocal report_write_attempts
                    path = Path(path)
                    if path.name == failure_name:
                        if failure_name != "task4b-execution-report.json":
                            raise OSError(f"synthetic failure writing {failure_name}")
                        report_write_attempts += 1
                        if report_write_attempts == 1:
                            raise OSError("synthetic failure writing success report")
                    return original_write(path, data)

                with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
                     mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
                     mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
                     mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted), \
                     mock.patch.object(runner, "_run_cuda_training", side_effect=complete_training), \
                     mock.patch.object(runner, "_finalize_task4b_artifacts", return_value=finalization), \
                     mock.patch.object(codec, "canonical_training_run_manifest_bytes", return_value=b"manifest"), \
                     mock.patch.object(codec, "canonical_smoke_evidence_bytes", return_value=b"evidence"), \
                     mock.patch.object(runner, "_write_atomic_bytes", side_effect=fail_publication):
                    with self.assertRaises(runner.Task4BSmokeError):
                        runner.run_task4b_smoke(
                            build_dir=source_root / "build",
                            output_dir=output_dir,
                        )

                report = json.loads(
                    (output_dir / "task4b-execution-report.json").read_text(
                        encoding="utf-8"
                    )
                )
                self.assertEqual(report["actual_optimizer_steps"], 500)
                for key, value in runner._execution_report_preflight_fields(preflight).items():
                    self.assertEqual(report[key], value)
                self.assertEqual(report["GPU_MEMORY_BEFORE"], 101)
                self.assertEqual(report["GPU_MEMORY_PEAK"], 202)
                self.assertEqual(report["GPU_MEMORY_AFTER"], 111)
                self.assertEqual(report["initial_loss"], 3.5)
                self.assertEqual(report["final_loss"], 2.5)
                self.assertFalse(report["SMOKE_PASS"])
                self.assertFalse(report["TASK4B_PASS"])
                self.assertIsNone(report["checkpoint_identity"])
                self.assertIsNone(report["smoke_evidence_identity"])
                if failure_name == "task4b-execution-report.json":
                    self.assertEqual(report_write_attempts, 2)

    def test_authoritative_probe_rejects_wrong_expected_hash_before_invocation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / "phase6_task4_corpus_probe.exe"
            probe.write_bytes(b"probe")
            with mock.patch.object(runner, "_run_command") as command:
                with self.assertRaises(runner.Task4BSmokeError) as raised:
                    runner._run_authoritative_corpus_probe(
                        probe,
                        root / "temporary",
                        source_root=root,
                        expected_probe_sha256="0" * 64,
                    )
            self.assertEqual(raised.exception.code, "PROBE_HASH_MISMATCH")
            command.assert_not_called()

    def test_authoritative_probe_rejects_binary_mutation_after_invocation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / "phase6_task4_corpus_probe.exe"
            probe.write_bytes(b"probe")
            expected_hash = hashlib.sha256(b"probe").hexdigest()

            def emit_outputs_and_mutate(argv, _source_root):
                Path(argv[2]).write_bytes(b"corpus-bytes")
                Path(argv[4]).write_bytes(b"authority-bytes")
                probe.write_bytes(b"mutated")

            with mock.patch.object(
                runner,
                "_run_command",
                side_effect=emit_outputs_and_mutate,
            ) as command:
                with self.assertRaises(runner.Task4BSmokeError) as raised:
                    runner._run_authoritative_corpus_probe(
                        probe,
                        root / "temporary",
                        source_root=root,
                        expected_probe_sha256=expected_hash,
                    )

            command.assert_called_once()
            self.assertEqual(
                raised.exception.code,
                "PROBE_HASH_CHANGED_DURING_EXECUTION",
            )

    def test_authoritative_probe_is_invoked_once_and_admitted_from_sidecar(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / "phase6_task4_corpus_probe.exe"
            probe.write_bytes(b"probe")
            authority = object()
            corpus = object()

            def emit_outputs(argv, _source_root):
                Path(argv[2]).write_bytes(b"corpus-bytes")
                Path(argv[4]).write_bytes(b"authority-bytes")

            with mock.patch.object(runner, "_run_command", side_effect=emit_outputs) as command, \
                 mock.patch.object(codec, "decode_corpus_authority_artifact", return_value=authority) as decode_authority, \
                 mock.patch.object(codec, "decode_corpus_artifact", return_value=corpus) as decode_corpus, \
                 mock.patch.object(codec, "admit_corpus_artifact", return_value=corpus) as admit:
                result = runner._run_authoritative_corpus_probe(
                    probe,
                    root,
                    source_root=root,
                    expected_probe_sha256=hashlib.sha256(b"probe").hexdigest(),
                )

            command.assert_called_once()
            argv, source_root = command.call_args.args
            self.assertEqual(argv[0], str(probe.resolve()))
            self.assertEqual(argv[1], "--output")
            self.assertEqual(argv[3], "--authority")
            self.assertEqual(source_root, root.resolve())
            decode_authority.assert_called_once_with(b"authority-bytes")
            decode_corpus.assert_called_once_with(b"corpus-bytes")
            admit.assert_called_once_with(b"corpus-bytes", authority)
            self.assertIs(result.corpus, corpus)
            self.assertIs(result.authority, authority)
            self.assertEqual(result.probe_sha256, hashlib.sha256(b"probe").hexdigest())

    def test_step_counter_marks_only_successful_optimizer_steps(self):
        counter = runner._SuccessfulStepCounter()
        optimizer = _FakeOptimizer(raises=False)
        loss = _FakeLoss()

        runner._apply_optimizer_update(optimizer, loss, counter)

        self.assertEqual(optimizer.step_calls, 1)
        self.assertEqual(loss.backward_calls, 1)
        self.assertEqual(counter.value, 1)

        failed_counter = runner._SuccessfulStepCounter()
        failed_optimizer = _FakeOptimizer(raises=True)
        with self.assertRaises(RuntimeError):
            runner._apply_optimizer_update(
                failed_optimizer,
                _FakeLoss(),
                failed_counter,
            )

        self.assertEqual(failed_optimizer.step_calls, 1)
        self.assertEqual(failed_counter.value, 0)

    def test_ordered_train_samples_filters_partitions_and_preserves_pairing(self):
        train_later = _sample("b" * 64, "train", "01")
        train_earlier = _sample("a" * 64, "train", "00")
        validation = _sample("c" * 64, "validation", "02")
        test = _sample("d" * 64, "test", "03")

        selected = runner._ordered_train_samples(
            (test, train_later, validation, train_earlier)
        )

        self.assertEqual(tuple(sample.partition for sample in selected), ("train", "train"))
        self.assertEqual(
            tuple(sample.bc_sample_identity for sample in selected),
            (train_earlier.bc_sample_identity, train_later.bc_sample_identity),
        )
        self.assertEqual(
            tuple(sample.routing_keys[sample.candidate_ordinal] for sample in selected),
            tuple(sample.selected_public_action_key for sample in selected),
        )


if __name__ == "__main__":
    unittest.main()
