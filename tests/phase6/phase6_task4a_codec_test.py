import hashlib
import dataclasses
import struct
import unittest

from tools.phase6 import task4_codec as codec


def sample_corpus() -> codec.DerivedCorpusV1:
    sample = codec.CorpusSampleV1(
        bc_sample_identity="bc_sample.v1." + "a" * 64,
        trajectory_record_id="trajectory_record.v1." + "b" * 64,
        episode_semantic_id="c" * 64,
        public_semantic_decision_id="d" * 64,
        model_input_identity="model_input.v1." + "e" * 64,
        selected_public_action_key="public_action.v1.00",
        partition="train",
        candidate_ordinal=0,
        ordered_candidate_domain_identity="",
        state_rows=((1.0, 0.0, 0.25, 0.0, 0.0, 0.0, 0.0, 0.0),),
        candidate_rows=(
            tuple(float(index) / 28.0 for index in range(codec.CANDIDATE_ROW_WIDTH)),
        ),
        routing_keys=("public_action.v1.00",),
    )
    sample = dataclasses.replace(
        sample,
        ordered_candidate_domain_identity=codec.ordered_candidate_domain_identity(
            sample.routing_keys
        ),
        bc_sample_identity=codec.bc_sample_identity(sample),
    )
    return codec.DerivedCorpusV1(
        source_dataset_identity="1" * 64,
        split_identity="phase6_dataset_split.v1." + "2" * 64,
        derivation_contract_identity=codec.NUMERIC_PROJECTION_CONTRACT_ID,
        card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
        episode_ids=("c" * 64,),
        samples=(sample,),
    )


class Task4ACodecTests(unittest.TestCase):
    def test_f32_is_exact_big_endian_and_rejects_nonfinite(self):
        self.assertEqual(codec.f32_bytes(1.0), b"\x3f\x80\x00\x00")
        self.assertEqual(codec.f32_bytes(-0.0), b"\x80\x00\x00\x00")
        for value in (float("nan"), float("inf"), float("-inf")):
            with self.assertRaises(codec.CodecError):
                codec.f32_bytes(value)

    def test_architecture_and_subidentity_codecs_are_deterministic(self):
        architecture = codec.default_architecture_config()
        first = codec.architecture_config_identity(architecture)
        second = codec.architecture_config_identity(architecture)
        self.assertEqual(first, second)
        self.assertEqual(len(first), len("phase6_architecture_config.v1.") + 64)
        self.assertNotEqual(codec.optimizer_config_identity(), codec.schedule_config_identity())
        self.assertEqual(codec.default_architecture_config().parameter_order,
                         codec.PARAMETER_ORDER)

    def test_weight_export_is_ordered_and_rejects_mutation_values(self):
        tensors = (
            codec.CanonicalTensorV1("state_encoder.input.weight", (16, 8), b"\x00" * (16 * 8 * 4)),
            codec.CanonicalTensorV1("state_encoder.input.bias", (16,), b"\x00" * (16 * 4)),
            codec.CanonicalTensorV1("candidate_encoder.input.weight", (16, 60), b"\x00" * (16 * 60 * 4)),
            codec.CanonicalTensorV1("candidate_encoder.input.bias", (16,), b"\x00" * (16 * 4)),
            codec.CanonicalTensorV1("score_head.weight", (1, 48), b"\x00" * (48 * 4)),
            codec.CanonicalTensorV1("score_head.bias", (1,), b"\x00" * 4),
        )
        body = codec.canonical_weight_export_bytes(
            tensors, codec.architecture_config_identity())
        self.assertTrue(body.startswith(codec.pack_string(codec.WEIGHT_EXPORT_CONTRACT_ID)))
        self.assertEqual(codec.weight_content_identity(body),
                         "phase6_weight_content.v1." + hashlib.sha256(body).hexdigest())
        with self.assertRaises(codec.CodecError):
            codec.canonical_weight_export_bytes(tuple(reversed(tensors)),
                                                 codec.architecture_config_identity())
        bad = list(tensors)
        bad[0] = codec.CanonicalTensorV1(
            bad[0].name, bad[0].shape, struct.pack(">f", float("inf")) + bad[0].raw_bytes[4:])
        with self.assertRaises(codec.CodecError):
            codec.canonical_weight_export_bytes(tuple(bad), codec.architecture_config_identity())

    def test_training_run_bounds_and_checkpoint_exclude_execution_provenance(self):
        manifest = codec.default_training_run_manifest(
            source_dataset_identity="1" * 64,
            dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
            card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
            training_code_commit="4" * 40,
            actual_optimizer_steps=0,
        )
        self.assertTrue(codec.training_run_identity(manifest).startswith("phase6_training_run.v1."))
        self.assertEqual(
            manifest.opponent_policy_source_identities,
            tuple(sorted(codec.TEACHER_SOURCE_IDENTITIES)),
        )
        with self.assertRaises(codec.CodecError):
            codec.training_run_identity(codec.replace_training_run(
                manifest, actual_optimizer_steps=501))
        with self.assertRaises(codec.CodecError):
            codec.training_run_identity(codec.replace_training_run(
                manifest, opponent_policy_source_identities=()))

        checkpoint = codec.default_checkpoint_manifest(
            architecture_config_identity=manifest.model_architecture_config_identity,
            card_vocabulary_identity=manifest.card_vocabulary_identity,
            dataset_identity=manifest.source_dataset_identity,
            dataset_split_identity=manifest.dataset_split_identity,
            canonical_weight_content_identity="phase6_weight_content.v1." + "5" * 64,
        )
        semantic_bytes = codec.canonical_checkpoint_manifest_bytes(checkpoint)
        changed_provenance = codec.replace_training_run(
            manifest, device_and_distributed_provenance_identity="execution.v1." + "6" * 64)
        self.assertEqual(codec.canonical_checkpoint_manifest_bytes(checkpoint), semantic_bytes)
        self.assertNotEqual(manifest.device_and_distributed_provenance_identity,
                            changed_provenance.device_and_distributed_provenance_identity)

    def test_derived_corpus_digest_detects_mutation(self):
        artifact = codec.encode_corpus_artifact(sample_corpus())
        decoded = codec.decode_corpus_artifact(artifact)
        self.assertEqual(codec.encode_corpus_artifact(decoded), artifact)
        mutated = bytearray(artifact)
        mutated[-1] ^= 1
        with self.assertRaises(codec.CodecError):
            codec.decode_corpus_artifact(bytes(mutated))

    def test_selection_response_identity_excludes_score_bytes(self):
        model_input = codec.make_numeric_model_input(
            model_input_identity="model_input.v1." + "b" * 64,
            state_rows=((0.0,) * codec.STATE_ROW_WIDTH,),
            candidate_rows=((0.0,) * codec.CANDIDATE_ROW_WIDTH,
                            (1.0,) + (0.0,) * (codec.CANDIDATE_ROW_WIDTH - 1)),
            routing_keys=("public_action.v1.00", "public_action.v1.01"),
            public_candidate_domain_digest=None,
            public_semantic_decision_id="c" * 64,
            perspective_player=0,
            decision_index=7,
        )
        request = codec.make_inference_request(
            checkpoint_identity="phase6_checkpoint.v1." + "a" * 64,
            model_input=model_input,
        )
        left = codec.make_inference_response(request, (1.0, 2.0), 1)
        right = codec.make_inference_response(request, (100.0, 200.0), 1)
        self.assertEqual(left.response_identity, right.response_identity)
        self.assertEqual(left.selected_public_action_key, "public_action.v1.01")

    def test_phase5_candidate_domain_digest_precedes_task4_fallback(self):
        keys = ("public_action.v1.00", "public_action.v1.01")
        phase5_digest = "9" * 64
        model_input = codec.make_numeric_model_input(
            model_input_identity="model_input.v1." + "8" * 64,
            state_rows=((0.0,) * codec.STATE_ROW_WIDTH,),
            candidate_rows=((0.0,) * codec.CANDIDATE_ROW_WIDTH,) * 2,
            routing_keys=keys,
            public_candidate_domain_digest=phase5_digest,
            public_semantic_decision_id="7" * 64,
            perspective_player=0,
            decision_index=1,
        )
        self.assertEqual(model_input.ordered_candidate_domain_identity, phase5_digest)
        self.assertNotEqual(
            model_input.ordered_candidate_domain_identity,
            codec.ordered_candidate_domain_identity(keys),
        )
        request = codec.make_inference_request(
            checkpoint_identity="phase6_checkpoint.v1." + "6" * 64,
            model_input=model_input,
        )
        self.assertEqual(request.ordered_candidate_domain_identity, phase5_digest)


if __name__ == "__main__":
    unittest.main()
