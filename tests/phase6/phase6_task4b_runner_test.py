import dataclasses
import unittest

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
