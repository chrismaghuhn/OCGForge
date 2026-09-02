import dataclasses
import unittest

import torch

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4_inference
from tools.phase6 import task4_model


def _checkpoint(model=None):
    if model is None:
        torch.manual_seed(1729)
        model = task4_model.Phase6TorchCandidateScorer()
    return task4_inference.export_canonical_checkpoint(
        model,
        source_dataset_identity="1" * 64,
        dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
        card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
    )


def _model_input(keys=("public_action.v1.01", "public_action.v1.00"),
                 public_candidate_domain_digest=None,
                 decision_index=7):
    state, candidates = _rows(len(keys))
    return codec.make_numeric_model_input(
        model_input_identity="model_input.v1." + "a" * 64,
        state_rows=state,
        candidate_rows=candidates,
        routing_keys=keys,
        public_candidate_domain_digest=public_candidate_domain_digest,
        public_semantic_decision_id="b" * 64,
        perspective_player=0,
        decision_index=decision_index,
    )


def _request(checkpoint_identity, model_input=None):
    if model_input is None:
        model_input = _model_input()
    return codec.make_inference_request(
        checkpoint_identity=checkpoint_identity, model_input=model_input
    )


def _bound(loaded, request, model_input):
    return task4_inference.bind_inference_execution(request, model_input, loaded)


def _reload(artifact_bytes):
    return task4_inference.reload_model_from_checkpoint(
        artifact_bytes,
        architecture_config=codec.default_architecture_config(),
        card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
        dataset_identity="1" * 64,
        dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
    )


def _rows(count=2):
    state = ((0.0,) * codec.STATE_ROW_WIDTH,)
    candidates = tuple(
        tuple(float(column == 0 and index) for column in range(codec.CANDIDATE_ROW_WIDTH))
        for index in range(count)
    )
    return state, candidates


class Task4AInferenceTests(unittest.TestCase):
    def test_inference_request_v1_has_no_task4_numeric_identity_field(self):
        self.assertFalse(hasattr(codec.InferenceRequestV1, "numeric_input_identity"))
        self.assertFalse(hasattr(codec.InferenceRequestV1, "public_candidate_domain_digest"))

    def test_canonical_export_mutation_and_wrong_architecture_fail_closed(self):
        exported = _checkpoint()
        loaded = task4_inference.load_checkpoint_artifact(exported.artifact_bytes)
        self.assertEqual(loaded.checkpoint_identity, exported.checkpoint_identity)
        strict = task4_inference.load_checkpoint_for_inference(
            exported.artifact_bytes,
            architecture_config=codec.default_architecture_config(),
            card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
            dataset_identity="1" * 64,
            dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
        )
        self.assertEqual(strict.checkpoint_identity, exported.checkpoint_identity)
        with self.assertRaises(task4_inference.Task4InferenceError):
            task4_inference.load_checkpoint_for_inference(
                exported.artifact_bytes,
                architecture_config=codec.default_architecture_config(),
                card_vocabulary_identity="model_card_vocabulary.v1." + "4" * 64,
                dataset_identity="1" * 64,
                dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
            )

        mutated = bytearray(exported.artifact_bytes)
        mutated[-1] ^= 1
        with self.assertRaises(task4_inference.Task4InferenceError):
            task4_inference.load_checkpoint_artifact(bytes(mutated))

        wrong_config = dataclasses.replace(
            codec.default_architecture_config(), state_hidden_width=17
        )
        with self.assertRaises(task4_inference.Task4InferenceError):
            task4_inference.load_checkpoint_artifact(
                exported.artifact_bytes,
                expected_architecture_config=wrong_config,
            )

    def test_reload_is_fresh_and_deterministic(self):
        exported = _checkpoint()
        loaded, first = _reload(exported.artifact_bytes)
        _, second = _reload(exported.artifact_bytes)
        model_input = _model_input()
        request = _request(loaded.checkpoint_identity, model_input)
        execution = _bound(loaded, request, model_input)
        first_response = task4_inference.infer_request(first, loaded, execution)
        second_response = task4_inference.infer_request(second, loaded, execution)
        self.assertEqual(first_response.scores, second_response.scores)
        self.assertEqual(first_response.selected_public_action_key,
                         second_response.selected_public_action_key)
        self.assertEqual(first_response.response_identity, second_response.response_identity)
        self.assertIsNot(first, second)

    def test_exact_domain_capacity_and_no_network_routing_surface(self):
        exported = _checkpoint()
        loaded, model = _reload(exported.artifact_bytes)
        model_input = _model_input()
        request = _request(loaded.checkpoint_identity, model_input)

        seen = []
        original_forward = model.forward

        def recording_forward(*args, **kwargs):
            seen.append((args, kwargs))
            return original_forward(*args, **kwargs)

        model.forward = recording_forward
        execution = _bound(loaded, request, model_input)
        response = task4_inference.infer_request(model, loaded, execution)
        self.assertEqual(len(response.scores), len(model_input.candidate_rows))
        self.assertEqual(len(seen), 1)
        args, kwargs = seen[0]
        self.assertTrue(all(isinstance(value, torch.Tensor) for value in args))
        self.assertNotIn("routing_keys", kwargs)
        self.assertNotIn("public_action_key", kwargs)
        with self.assertRaises(task4_inference.Task4InferenceError):
            task4_inference.infer_request(
                model, loaded, execution,
                physical_candidate_capacity=1,
            )

    def test_rows_cannot_be_rebound_to_another_model_input_identity(self):
        exported = _checkpoint()
        loaded, model = _reload(exported.artifact_bytes)
        model_input = _model_input()
        request = _request(loaded.checkpoint_identity, model_input)
        changed_rows = dataclasses.replace(
            model_input,
            candidate_rows=tuple(
                (tuple(1.0 for _ in row) if index == 0 else row)
                for index, row in enumerate(model_input.candidate_rows)
            ),
        )
        changed_rows = dataclasses.replace(
            changed_rows,
            numeric_input_identity=codec.numeric_model_input_identity(changed_rows),
        )
        self.assertNotEqual(
            changed_rows.numeric_input_identity, model_input.numeric_input_identity
        )
        runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        runner.submit_request(request, model_input)
        with self.assertRaises(task4_inference.Task4InferenceError):
            runner.infer(request, changed_rows)

    def test_tie_uses_bytewise_public_key_only_after_network_scoring(self):
        model = task4_model.Phase6TorchCandidateScorer()
        with torch.no_grad():
            for parameter in model.parameters():
                parameter.zero_()
        exported = _checkpoint(model)
        loaded, fresh = _reload(exported.artifact_bytes)
        model_input = _model_input()
        request = _request(loaded.checkpoint_identity, model_input)
        response = task4_inference.infer_request(
            fresh, loaded, _bound(loaded, request, model_input)
        )
        self.assertEqual(response.selected_candidate_ordinal, 1)
        self.assertEqual(response.selected_public_action_key, "public_action.v1.00")

    def test_response_ledger_rejects_stale_duplicate_wrong_and_malformed_responses(self):
        exported = _checkpoint()
        loaded, model = _reload(exported.artifact_bytes)
        model_input = _model_input()

        runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        request = _request(loaded.checkpoint_identity, model_input)
        accepted = runner.infer(request, model_input)
        with self.assertRaises(task4_inference.Task4InferenceError):
            runner.accept_response(request, accepted)

        stale_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        first = _request(loaded.checkpoint_identity, model_input)
        second = _request(loaded.checkpoint_identity, model_input)
        # Different decision indices make these distinct request identities.
        second = dataclasses.replace(second, decision_index=8)
        second = dataclasses.replace(second, request_identity=codec.inference_request_identity(second))
        stale_runner.submit_request(first, model_input)
        second_response = codec.make_inference_response(second, (1.0, 2.0), 1)
        with self.assertRaises(task4_inference.Task4InferenceError):
            stale_runner.accept_response(first, second_response)

        malformed_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        malformed_request = _request(loaded.checkpoint_identity, model_input)
        malformed_runner.submit_request(malformed_request, model_input)
        valid = codec.make_inference_response(malformed_request, (1.0, 2.0), 1)
        wrong_input = dataclasses.replace(
            valid, model_input_identity="model_input.v1." + "c" * 64
        )
        with self.assertRaises(task4_inference.Task4InferenceError):
            malformed_runner.accept_response(malformed_request, wrong_input)

        domain_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        domain_request = _request(loaded.checkpoint_identity, model_input)
        domain_runner.submit_request(domain_request, model_input)
        wrong_domain = dataclasses.replace(
            codec.make_inference_response(domain_request, (1.0, 2.0), 1),
            ordered_candidate_domain_identity=codec.ordered_candidate_domain_identity(
                ("public_action.v1.00", "public_action.v1.01")
            ),
        )
        with self.assertRaises(task4_inference.Task4InferenceError):
            domain_runner.accept_response(domain_request, wrong_domain)

        cardinality_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        cardinality_request = _request(loaded.checkpoint_identity, model_input)
        cardinality_runner.submit_request(cardinality_request, model_input)
        with self.assertRaises(task4_inference.Task4InferenceError):
            cardinality_runner.accept_response(
                cardinality_request, dataclasses.replace(valid, request_identity=cardinality_request.request_identity,
                                                         scores=(1.0,))
            )

        ordinal_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        ordinal_request = _request(loaded.checkpoint_identity, model_input)
        ordinal_runner.submit_request(ordinal_request, model_input)
        with self.assertRaises(task4_inference.Task4InferenceError):
            ordinal_runner.accept_response(
                ordinal_request,
                dataclasses.replace(
                    codec.make_inference_response(ordinal_request, (1.0, 2.0), 1),
                    selected_candidate_ordinal=2,
                ),
            )

        nonfinite_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        nonfinite_request = _request(loaded.checkpoint_identity, model_input)
        nonfinite_runner.submit_request(nonfinite_request, model_input)
        with self.assertRaises(task4_inference.Task4InferenceError):
            nonfinite_runner.accept_response(
                nonfinite_request, dataclasses.replace(
                    codec.make_inference_response(nonfinite_request, (1.0, 2.0), 1),
                    scores=(float("nan"), 2.0),
                )
            )

        selection_runner = task4_inference.Phase6InferenceRunnerV1(model, loaded)
        selection_request = _request(loaded.checkpoint_identity, model_input)
        selection_runner.submit_request(selection_request, model_input)
        with self.assertRaises(task4_inference.Task4InferenceError):
            selection_runner.accept_response(
                selection_request,
                codec.make_inference_response(selection_request, (2.0, 1.0), 1),
            )

    def test_wrong_checkpoint_request_and_no_fallback(self):
        exported = _checkpoint()
        loaded, model = _reload(exported.artifact_bytes)
        wrong = _request("phase6_checkpoint.v1." + "f" * 64)
        with self.assertRaises(task4_inference.Task4InferenceError):
            task4_inference.Phase6InferenceRunnerV1(model, loaded).submit_request(
                wrong, _model_input()
            )


if __name__ == "__main__":
    unittest.main()
