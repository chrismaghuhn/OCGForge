import dataclasses
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools.phase6 import task4_codec as codec
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
             mock.patch.object(runner, "_run_authoritative_corpus_probe") as run_probe:
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner.run_task4b_smoke(
                    build_dir=source_root / "build",
                    output_dir=source_root / "output",
                )

        self.assertIs(raised.exception, dirty)
        build_probe.assert_not_called()
        run_probe.assert_not_called()

    def test_authoritative_composition_connects_clean_head_to_probe_admission(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        probe_hash = "a" * 64
        admitted = _admitted_corpus((_sample("a" * 64, "train", "00"),))
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
             mock.patch.object(runner, "_run_authoritative_corpus_probe", side_effect=record_probe):
            result = runner.run_task4b_smoke(
                build_dir=source_root / "build",
                output_dir=source_root / "output",
            )

        self.assertEqual(calls, ["clean", "build", "probe"])
        self.assertEqual(result.h_exec, "b" * 40)
        self.assertEqual(result.probe_path, probe)
        self.assertEqual(result.probe_sha256, probe_hash)
        self.assertIs(result.admitted_corpus, admitted)

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

        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
             mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, probe_hash)), \
             mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted):
            result = runner.run_task4b_smoke(
                build_dir=source_root / "build",
                output_dir=source_root / "output",
            )

        self.assertEqual(result.source_dataset_identity, "5" * 64)
        self.assertEqual(result.dataset_split_identity, "phase6_dataset_split.v1." + "6" * 64)
        self.assertEqual(result.card_vocabulary_identity, "model_card_vocabulary.v1." + "7" * 64)
        self.assertEqual(result.train_sample_count, 2)
        self.assertEqual(result.validation_sample_count, 1)
        self.assertEqual(result.test_sample_count, 1)
        self.assertEqual(
            tuple(sample.bc_sample_identity for sample in result.ordered_train_samples),
            (train_earlier.bc_sample_identity, train_later.bc_sample_identity),
        )
        self.assertTrue(
            all(sample.partition == "train" for sample in result.ordered_train_samples)
        )

    def test_empty_admitted_train_partition_fails_closed_after_admission(self):
        source_root = Path("C:/source")
        probe = source_root / "build" / "phase6_task4_corpus_probe.exe"
        admitted = _admitted_corpus(
            (_sample("c" * 64, "validation", "02"), _sample("d" * 64, "test", "03"))
        )

        with mock.patch.object(runner, "_canonical_source_root", return_value=source_root), \
             mock.patch.object(runner, "_verify_clean_h_exec", return_value="b" * 40), \
             mock.patch.object(runner, "_build_and_attest_probe", return_value=(probe, "a" * 64)), \
             mock.patch.object(runner, "_run_authoritative_corpus_probe", return_value=admitted):
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
             mock.patch.object(runner, "_ordered_train_samples") as ordered:
            with self.assertRaises(runner.Task4BSmokeError) as raised:
                runner.run_task4b_smoke(
                    build_dir=source_root / "build",
                    output_dir=source_root / "output",
                )

        self.assertIs(raised.exception, admission_failure)
        ordered.assert_not_called()

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
