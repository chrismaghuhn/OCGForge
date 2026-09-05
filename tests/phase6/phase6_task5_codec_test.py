import copy
import dataclasses
import os
from pathlib import Path
import subprocess
import sys
import unittest

from tools.phase6 import task5_codec as codec


TEST_EVALUATOR_SOURCE_COMMIT = "1" * 40


def _job() -> codec.EvaluationJobV1:
    return codec.default_evaluation_job(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _jobs() -> tuple[codec.EvaluationJobV1, ...]:
    return codec.implementation_acceptance_jobs(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _corpus() -> codec.EvaluationCorpusV1:
    return codec.default_evaluation_corpus(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _identity() -> codec.EvaluationIdentityV1:
    return codec.default_evaluation_identity(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _manifest() -> codec.EvaluationManifestV1:
    return codec.default_evaluation_manifest(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _job_manifest() -> codec.EvaluationJobManifestV1:
    return codec.default_evaluation_job_manifest(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _context() -> codec.EvaluationContextV1:
    return codec.default_evaluation_context(
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT
    )


def _meaningful_context(
    checkpoint_identity: str = "phase6_checkpoint.v1." + "e" * 64,
) -> codec.EvaluationContextV1:
    jobs = codec.meaningful_fixed_matchup_jobs(
        checkpoint_identity=checkpoint_identity,
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT,
    )
    job_ids = tuple(codec.evaluation_job_identity(job) for job in jobs)
    corpus = codec.EvaluationCorpusV1(
        corpus_profile_identity=codec.MEANINGFUL_FIXED_MATCHUP_PROFILE,
        corpus_kind=codec.MEANINGFUL_FIXED_MATCHUP_KIND,
        checkpoint_identity=checkpoint_identity,
        evaluation_job_identities=job_ids,
    )
    corpus_id = codec.evaluation_corpus_identity(corpus)
    root = codec.EvaluationIdentityV1(
        evaluation_corpus_identity=corpus_id,
        checkpoint_identity=checkpoint_identity,
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT,
    )
    root_id = codec.evaluation_identity(root)
    job_manifest = codec.EvaluationJobManifestV1(
        evaluation_identity=root_id,
        evaluation_contract_identity=codec.evaluation_contract_identity(),
        evaluation_corpus_identity=corpus_id,
        evaluation_job_identities=job_ids,
    )
    job_manifest_id = codec.evaluation_job_manifest_identity(job_manifest)
    manifest = codec.EvaluationManifestV1(
        evaluation_identity=root_id,
        evaluation_contract_identity=codec.evaluation_contract_identity(),
        evaluation_corpus_identity=corpus_id,
        evaluation_job_manifest_identity=job_manifest_id,
        checkpoint_identity=checkpoint_identity,
        evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT,
    )
    context = codec.EvaluationContextV1(root, manifest, corpus, job_manifest, jobs)
    context.validate()
    return context


def _key(index: int) -> str:
    return codec.public_action_key(
        codec.PublicCandidateDescriptorV1(
            action_kind="card_selection",
            choice=codec.PublicChoiceV1(kind=1, value=index % 2),
            source_reference=codec.PublicReferenceV1(
                reference_kind=0,
                public_locator_token=f"visible:{index}",
            ),
            phase=3,
            source_index=index,
        )
    )


def _descriptor(index: int = 0) -> codec.PublicCandidateDescriptorV1:
    return codec.PublicCandidateDescriptorV1(
        action_kind="card_selection",
        choice=codec.PublicChoiceV1(kind=1, value=index % 2),
        source_reference=codec.PublicReferenceV1(
            reference_kind=0,
            public_locator_token=f"visible:{index}",
            current_entity_ordinal=index,
        ),
        target_reference=None,
        phase=3,
        position=None,
        source_index=index,
        amount=None,
        continuation_operation="",
        submits_engine_response=True,
    )


def _score_vector(count: int = 2) -> codec.ScoreVectorV1:
    return codec.ScoreVectorV1(
        public_action_keys=tuple(_key(index) for index in range(count)),
        score_f32_bits=tuple(
            codec.score_f32_bits(float(index)) for index in range(count)
        ),
    )


def _divergence_record(
    kind: int = codec.DIVERGENCE,
) -> codec.FirstDivergenceV1:
    job = _job()
    if kind == codec.DIVERGENCE:
        scores = _score_vector()
        return codec.FirstDivergenceV1(
            evaluation_job_identity=codec.evaluation_job_identity(job),
            record_kind=kind,
            observed_public_decision_count=4,
            semantic_decision_identity="a" * 64,
            public_observation_digest="b" * 64,
            model_input_identity="model_input.v1." + "c" * 64,
            ordered_candidate_domain_identity=codec.public_candidate_domain_digest(
                "card_selection", scores.public_action_keys
            ),
            candidate_count=2,
            candidate_public_action_keys=scores.public_action_keys,
            candidate_descriptors=(_descriptor(0), _descriptor(1)),
            score_vector_identity=codec.score_vector_identity(scores),
            score_f32_bits=scores.score_f32_bits,
            teacher_selected_public_action_key=_key(0),
            model_selected_public_action_key=_key(1),
            decision_request_family="card_selection",
            continuation_context=codec.ContinuationContextV1(
                is_continuation=False,
                public_continuation_operation=None,
            ),
            first_divergence_ordinal=4,
        )
    if kind == codec.NO_DIVERGENCE_TERMINAL:
        return codec.FirstDivergenceV1(
            evaluation_job_identity=codec.evaluation_job_identity(job),
            record_kind=kind,
            observed_public_decision_count=9,
            terminal_outcome=codec.TerminalOutcomeV1(
                terminal=True,
                winner=0,
                win_reason=7,
            ),
        )
    return codec.FirstDivergenceV1(
        evaluation_job_identity=codec.evaluation_job_identity(job),
        record_kind=kind,
        observed_public_decision_count=0,
        failure_before_divergence=codec.FailureBeforeDivergenceV1(
            failure_stage="before_public_decision",
            error_code="MODEL_TIMEOUT",
            failed_decision_ordinal=0,
        ),
    )


class Task5JsonTests(unittest.TestCase):
    def test_canonical_json_rejects_noncanonical_duplicate_and_float_values(self):
        value = {"schema_id": "example.v1", "value": 1}
        encoded = codec.canonical_json_bytes(value)
        self.assertEqual(
            encoded,
            b'{"schema_id":"example.v1","value":1}\n',
        )
        self.assertEqual(codec.parse_canonical_json(encoded), value)
        with self.assertRaises(codec.CodecError):
            codec.parse_canonical_json(b'{ "schema_id":"example.v1","value":1 }\n')
        with self.assertRaises(codec.CodecError):
            codec.parse_canonical_json(b'{"schema_id":"example.v1","value":1,"value":2}\n')
        with self.assertRaises(codec.CodecError):
            codec.canonical_json_bytes({"score": 1.0})

    def test_manifest_json_rejects_missing_and_unknown_fields(self):
        manifest = _manifest()
        payload = manifest.to_dict(context=_context())
        self.assertEqual(
            codec.decode_evaluation_manifest_json(
                codec.canonical_json_bytes(payload)
            ),
            manifest,
        )
        missing = copy.deepcopy(payload)
        del missing["checkpoint_identity"]
        with self.assertRaises(codec.CodecError):
            codec.decode_evaluation_manifest_json(codec.canonical_json_bytes(missing))
        unknown = copy.deepcopy(payload)
        unknown["CoreHost"] = "private"
        with self.assertRaises(codec.CodecError):
            codec.decode_evaluation_manifest_json(codec.canonical_json_bytes(unknown))

    def test_json_transport_rejects_bom_missing_lf_cr_and_private_nested_fields(self):
        with self.assertRaises(codec.CodecError):
            codec.parse_canonical_json(b'\xef\xbb\xbf{"value":1}\n')
        with self.assertRaises(codec.CodecError):
            codec.parse_canonical_json(b'{"value":1}')
        with self.assertRaises(codec.CodecError):
            codec.parse_canonical_json(b'{"value":1}\r\n')
        record = codec.FirstDivergenceV1(
            evaluation_job_identity=codec.evaluation_job_identity(_job()),
            record_kind=codec.NO_DIVERGENCE_TERMINAL,
            terminal_outcome=codec.TerminalOutcomeV1(),
        ).to_field_dict()
        record["terminal_outcome"]["CoreHost"] = "private"
        with self.assertRaises(codec.CodecError):
            codec.TerminalOutcomeV1.from_dict(record["terminal_outcome"])


class Task5PrimitiveAndIdentityTests(unittest.TestCase):
    def test_public_action_keys_use_the_complete_accepted_descriptor_codec(self):
        valid = _key(0)
        self.assertTrue(codec.is_public_action_key(valid))
        self.assertFalse(codec.is_public_action_key("public_action.v1.00"))
        golden_descriptor = codec.PublicCandidateDescriptorV1(
            action_kind="card_selection",
            source_reference=codec.PublicReferenceV1(
                reference_kind=1,
                public_locator_token="p0:SPELL_TRAP_ZONE:0",
            ),
            source_index=3,
        )
        golden_key = codec.public_action_key(golden_descriptor)
        self.assertEqual(
            golden_key,
            "public_action.v1.000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
            "000000226f6367666f7267652e7075626c69635f616374696f6e5f6964656e746974792e7631"
            "0000000e636172645f73656c656374696f6e0001010000001470303a5350454c4c5f545241505f5a4f4e453a3000000001000000030000000000",
        )
        self.assertEqual(
            codec.public_candidate_domain_digest("card_selection", (golden_key,)),
            "b35640b35822c76ed165a65f86c6d7ac5520abdf4359482b7608bf125274e1e6",
        )
        with self.assertRaises(codec.CodecError):
            codec.PublicChoiceV1(kind=1, value=1, response_index=0).validate()
        with self.assertRaises(codec.CodecError):
            codec.PublicChoiceV1(kind=4, value=1, response_index=None).validate()
        with self.assertRaises(codec.CodecError):
            codec.PublicCandidateDescriptorV1(
                action_kind="unsupported_action",
            ).validate()
        with self.assertRaises(codec.CodecError):
            codec.PublicCandidateDescriptorV1(
                action_kind="card_selection",
                continuation_operation="arbitrary_token",
            ).validate()

    def test_score_bits_preserve_zero_sign_and_reject_invalid_values(self):
        self.assertEqual(codec.score_f32_bits(0.0), "00000000")
        self.assertEqual(codec.score_f32_bits(-0.0), "80000000")
        self.assertEqual(codec.score_f32_bits(1.0), "3f800000")
        for value in ("3F800000", "3f80000", "3f80000000", "zzzzzzzz"):
            with self.assertRaises(codec.CodecError):
                codec.score_f32_bytes(value)
        for value in ("nan", "inf", "-inf"):
            with self.assertRaises(codec.CodecError):
                codec.score_f32_bits(float(value))
        for value in ("7f800000", "ff800000", "7fc00000"):
            with self.assertRaises(codec.CodecError):
                codec.score_f32_bytes(value)

    def test_identity_preimages_start_with_domain_and_schema(self):
        root = _identity()
        root_bytes = codec.canonical_evaluation_identity_bytes(root)
        self.assertTrue(
            root_bytes.startswith(
                codec.pack_string(codec.EVALUATION_IDENTITY_SCHEMA_ID)
                + codec.pack_string(codec.EVALUATION_IDENTITY_SCHEMA_ID)
            )
        )
        contract_bytes = codec.canonical_evaluation_contract_identity_bytes()
        self.assertTrue(
            contract_bytes.startswith(
                codec.pack_string(codec.EVALUATION_CONTRACT_IDENTITY_DOMAIN)
                + codec.pack_string(codec.EVALUATION_CONTRACT_IDENTITY_DOMAIN)
            )
        )
        self.assertEqual(
            codec.evaluation_identity(root),
            codec.evaluation_identity(dataclasses.replace(root)),
        )
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_identity_bytes(
                dataclasses.replace(root, identity_domain="wrong.v1")
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_contract_identity_bytes(
                dataclasses.replace(
                    codec.default_evaluation_contract_identity(),
                    identity_schema="wrong.v1",
                )
            )

    def test_candidate_multiplicity_and_source_order_are_not_normalized(self):
        with self.assertRaises(codec.CodecError):
            codec.ScoreVectorV1(
                public_action_keys=(_key(0), _key(0)),
                score_f32_bits=("00000000", "00000000"),
            ).validate()
        with self.assertRaises(codec.CodecError):
            codec.ScoreVectorV1(
                public_action_keys=(_key(0), _key(1)),
                score_f32_bits=("00000000",),
            ).validate()
        original = _score_vector()
        reordered = codec.ScoreVectorV1(
            public_action_keys=tuple(reversed(original.public_action_keys)),
            score_f32_bits=tuple(reversed(original.score_f32_bits)),
        )
        self.assertNotEqual(
            codec.score_vector_identity(original),
            codec.score_vector_identity(reordered),
        )
        self.assertEqual(original.public_action_keys, (_key(0), _key(1)))

    def test_phase5_domain_precedence_requires_request_kind(self):
        keys = (_key(0), _key(1))
        raw = codec.public_candidate_domain_digest("card_selection", keys)
        fallback = codec.fallback_ordered_candidate_domain_identity(keys)
        self.assertEqual(
            codec.ordered_candidate_domain_identity(keys, "card_selection"),
            raw,
        )
        self.assertEqual(codec.ordered_candidate_domain_identity(keys), fallback)
        codec.validate_ordered_candidate_domain_identity(raw, "card_selection", keys)
        codec.validate_ordered_candidate_domain_identity(fallback, None, keys)
        with self.assertRaises(codec.CodecError):
            codec.validate_ordered_candidate_domain_identity(
                fallback, "card_selection", keys
            )
        with self.assertRaises(codec.CodecError):
            codec.ordered_candidate_domain_identity(keys, "d" * 64)

    def test_decision_kind_vocabulary_is_distinct_from_action_kind_vocabulary(self):
        keys = (_key(0), _key(1))
        for request_kind in (
            "idle_command",
            "battle_command",
            "chain",
            "option",
            "card_selection",
            "tribute",
            "sum",
            "place",
            "counter",
            "ordering",
            "announcement",
            "unselect_card",
            "position",
            "yes_no",
        ):
            digest = codec.public_candidate_domain_digest(request_kind, keys)
            codec.validate_ordered_candidate_domain_identity(
                digest, request_kind, keys
            )
        for invalid_request_kind in ("pick", "finish", "unsupported"):
            with self.assertRaises(codec.CodecError):
                codec.public_candidate_domain_digest(invalid_request_kind, keys)

    def test_score_vector_preserves_source_order_and_exact_capacity_witnesses(self):
        for count in (24, 25, 129):
            vector = _score_vector(count)
            decoded = codec.decode_score_vector_json(
                codec.encode_score_vector_json(vector)
            )
            self.assertEqual(decoded, vector)
            self.assertEqual(
                codec.decode_score_vector_bytes(codec.canonical_score_vector_bytes(vector)),
                vector,
            )
            self.assertEqual(len(decoded.public_action_keys), count)
            self.assertEqual(
                decoded.score_f32_bits[0],
                codec.score_f32_bits(0.0),
            )
            self.assertEqual(
                decoded.score_f32_bits[1],
                codec.score_f32_bits(1.0),
            )

    def test_score_selection_uses_exact_tie_key_without_reordering_vector(self):
        vector = codec.ScoreVectorV1(
            public_action_keys=(_key(1), _key(0)),
            score_f32_bits=("3f800000", "3f800000"),
        )
        self.assertEqual(codec.select_score_vector(vector), 1)
        self.assertEqual(vector.public_action_keys, (_key(1), _key(0)))


class Task5JobTests(unittest.TestCase):
    def test_meaningful_fixed_matchup_schedule_is_exact_and_non_smoke(self):
        checkpoint = "phase6_checkpoint.v1." + "e" * 64
        jobs = codec.meaningful_fixed_matchup_jobs(
            checkpoint_identity=checkpoint,
            evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT,
        )
        self.assertEqual(len(jobs), 16)
        self.assertEqual(
            [job.deterministic_seed for job in jobs],
            [1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2],
        )
        self.assertEqual(
            [job.starting_player for job in jobs],
            [0, 1, 0, 1] * 4,
        )
        self.assertEqual(
            [(job.seat_0_deck_role_id, job.evaluated_policy_seat) for job in jobs],
            [
                (codec.SWORDSOUL_DECK_ID, 0),
                (codec.SWORDSOUL_DECK_ID, 0),
                (codec.SWORDSOUL_DECK_ID, 1),
                (codec.SWORDSOUL_DECK_ID, 1),
                (codec.SALAMANGREAT_DECK_ID, 0),
                (codec.SALAMANGREAT_DECK_ID, 0),
                (codec.SALAMANGREAT_DECK_ID, 1),
                (codec.SALAMANGREAT_DECK_ID, 1),
            ] * 2,
        )
        self.assertEqual(
            {job.corpus_profile_identity for job in jobs},
            {codec.MEANINGFUL_FIXED_MATCHUP_PROFILE},
        )
        self.assertEqual(
            {job.evaluated_policy_checkpoint_identity for job in jobs},
            {checkpoint},
        )
        self.assertEqual(len({codec.evaluation_job_identity(job) for job in jobs}), 16)

    def test_meaningful_schedule_rejects_smoke_checkpoint(self):
        with self.assertRaises(codec.CodecError):
            codec.meaningful_fixed_matchup_jobs(
                checkpoint_identity=codec.SMOKE_CHECKPOINT_ID,
                evaluator_semantic_source_commit=TEST_EVALUATOR_SOURCE_COMMIT,
            )

    def test_meaningful_context_reuses_t5a_identity_graph_and_exact_order(self):
        context = _meaningful_context()
        self.assertEqual(
            context.corpus.corpus_profile_identity,
            codec.MEANINGFUL_FIXED_MATCHUP_PROFILE,
        )
        self.assertEqual(context.corpus.corpus_kind, codec.MEANINGFUL_FIXED_MATCHUP_KIND)
        self.assertEqual(len(context.jobs), 16)
        self.assertEqual(
            context.corpus.evaluation_job_identities,
            tuple(codec.evaluation_job_identity(job) for job in context.jobs),
        )
        self.assertEqual(
            context.job_manifest.evaluation_job_identities,
            context.corpus.evaluation_job_identities,
        )
        self.assertEqual(
            context.jobs[0].evaluated_policy_seat,
            0,
        )
        self.assertEqual(context.jobs[2].evaluated_policy_seat, 1)
        self.assertEqual(context.jobs[4].seat_0_deck_role_id, codec.SALAMANGREAT_DECK_ID)
        self.assertEqual(context.jobs[14].evaluated_policy_seat, 1)

    def test_meaningful_context_rejects_schedule_and_binding_mutations(self):
        context = _meaningful_context()
        with self.assertRaises(codec.CodecError):
            codec.validate_evaluation_context(
                context.root,
                context.manifest,
                context.corpus,
                context.job_manifest,
                tuple(reversed(context.jobs)),
            )
        changed_job = dataclasses.replace(
            context.jobs[2],
            starting_player=1 - context.jobs[2].starting_player,
        )
        changed_jobs = context.jobs[:2] + (changed_job,) + context.jobs[3:]
        with self.assertRaises(codec.CodecError):
            codec.validate_evaluation_context(
                context.root,
                context.manifest,
                context.corpus,
                context.job_manifest,
                changed_jobs,
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_job_bytes(
                dataclasses.replace(
                    context.jobs[0],
                    evaluated_policy_checkpoint_identity=codec.SMOKE_CHECKPOINT_ID,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_job_bytes(
                dataclasses.replace(
                    context.jobs[0],
                    corpus_profile_identity=codec.IMPLEMENTATION_ACCEPTANCE_PROFILE,
                    evaluated_policy_checkpoint_identity="phase6_checkpoint.v1." + "f" * 64,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_job_bytes(
                dataclasses.replace(context.jobs[0], deterministic_seed=3)
            )

    def test_meaningful_schedule_is_fresh_process_deterministic(self):
        script = (
            "from tools.phase6 import task5_codec as c; "
            "jobs=c.meaningful_fixed_matchup_jobs("
            "checkpoint_identity='phase6_checkpoint.v1.'+'e'*64,"
            "evaluator_semantic_source_commit='" + TEST_EVALUATOR_SOURCE_COMMIT + "'); "
            "print('|'.join(c.evaluation_job_identity(job) for job in jobs))"
        )
        env = dict(os.environ)
        env["PYTHONPATH"] = str(Path(__file__).resolve().parents[2])
        outputs = [
            subprocess.check_output(
                [sys.executable, "-c", script],
                cwd=Path(__file__).resolve().parents[2],
                env=env,
                text=True,
            )
            for _ in range(2)
        ]
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(len(outputs[0].strip().split("|")), 16)

    def test_fixed_implementation_acceptance_matrix_has_eight_ordered_jobs(self):
        jobs = _jobs()
        self.assertEqual(len(jobs), 8)
        self.assertEqual([job.deterministic_seed for job in jobs], [1, 1, 1, 1, 2, 2, 2, 2])
        self.assertEqual(
            [job.starting_player for job in jobs],
            [0, 1, 0, 1, 0, 1, 0, 1],
        )
        self.assertEqual(
            [job.seat_0_deck_role_id for job in jobs],
            [
                codec.SWORDSOUl_DECK_ID,
                codec.SWORDSOUl_DECK_ID,
                codec.SALAMANGREAT_DECK_ID,
                codec.SALAMANGREAT_DECK_ID,
            ]
            * 2,
        )
        identities = [codec.evaluation_job_identity(job) for job in jobs]
        self.assertEqual(len(set(identities)), 8)

    def test_job_and_corpus_reencoding_is_deterministic(self):
        job = _job()
        first = codec.evaluation_job_identity(job)
        second = codec.evaluation_job_identity(dataclasses.replace(job))
        self.assertEqual(first, second)
        corpus = _corpus()
        self.assertEqual(
            codec.decode_evaluation_corpus_json(
                codec.encode_evaluation_corpus_json(corpus, context=_context())
            ),
            corpus,
        )
        self.assertEqual(
            codec.evaluation_corpus_identity(corpus),
            codec.evaluation_corpus_identity(dataclasses.replace(corpus)),
        )

    def test_evaluator_source_commit_is_explicit_and_semantic(self):
        self.assertFalse(hasattr(codec, "ACCEPTED_T5A_BASE_COMMIT"))
        with self.assertRaises(TypeError):
            codec.default_evaluation_job()
        with self.assertRaises(TypeError):
            codec.default_evaluation_identity()
        other = codec.default_evaluation_job(
            evaluator_semantic_source_commit="2" * 40
        )
        self.assertNotEqual(
            codec.evaluation_job_identity(_job()),
            codec.evaluation_job_identity(other),
        )

    def test_dataset_and_split_use_their_exact_accepted_grammars(self):
        corpus = _corpus()
        payload = corpus.to_dict(context=_context())
        payload["source_dataset_identity"] = (
            "24b690ae989f9176fecc8931b86d282dbcd3cb0912044317c32a2a0815b8b936"
        )
        payload["dataset_split_identity"] = "phase6_dataset_split.v1." + "2" * 64
        modified_corpus = dataclasses.replace(
            corpus,
            source_dataset_identity=payload["source_dataset_identity"],
            dataset_split_identity=payload["dataset_split_identity"],
        )
        modified_corpus.validate()
        payload["evaluation_corpus_identity"] = codec.evaluation_corpus_identity(
            modified_corpus
        )
        corpus = codec.decode_evaluation_corpus_json(codec.canonical_json_bytes(payload))
        corpus.validate()
        invalid_payload = copy.deepcopy(payload)
        invalid_payload["source_dataset_identity"] = "dataset.v1." + "3" * 64
        with self.assertRaises(codec.CodecError):
            codec.decode_evaluation_corpus_json(codec.canonical_json_bytes(invalid_payload))

    def test_aggregate_context_binds_root_manifest_corpus_jobs_and_source(self):
        context = _context()
        jobs = context.jobs
        corpus = context.corpus
        root = context.root
        job_manifest = context.job_manifest
        manifest = context.manifest
        self.assertIsNone(
            codec.validate_evaluation_context(
                root, manifest, corpus, job_manifest, jobs
            )
        )
        with self.assertRaises(TypeError):
            codec.encode_evaluation_job_manifest_json(job_manifest)
        self.assertEqual(
            codec.decode_evaluation_job_manifest_json(
                codec.encode_evaluation_job_manifest_json(
                    job_manifest,
                    context=context,
                )
            ),
            job_manifest,
        )
        with self.assertRaises(TypeError):
            codec.encode_evaluation_manifest_json(manifest)
        self.assertEqual(
            codec.decode_evaluation_manifest_json(
                codec.encode_evaluation_manifest_json(
                    manifest,
                    context=context,
                )
            ),
            manifest,
        )
        with self.assertRaises(codec.CodecError):
            codec.validate_evaluation_context(
                root,
                manifest,
                corpus,
                dataclasses.replace(
                    job_manifest,
                    evaluation_job_identities=tuple(
                        reversed(job_manifest.evaluation_job_identities)
                    ),
                ),
                jobs,
            )
        with self.assertRaises(codec.CodecError):
            codec.validate_evaluation_context(
                root,
                manifest,
                dataclasses.replace(
                    corpus,
                    evaluation_job_identities=tuple(
                        reversed(corpus.evaluation_job_identities)
                    ),
                ),
                job_manifest,
                jobs,
            )

    def test_wrong_job_binding_and_provenance_are_rejected_or_excluded(self):
        job = _job()
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_job_bytes(
                dataclasses.replace(
                    job,
                    evaluated_policy_checkpoint_identity="phase6_checkpoint.v1." + "0" * 64,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_evaluation_job_bytes(
                dataclasses.replace(
                    job,
                    evaluation_contract_identity="phase6_evaluation_contract.v1." + "0" * 64,
                )
            )
        with self.assertRaises(TypeError):
            dataclasses.replace(job, framework="cuda")


class Task5FirstDivergenceTests(unittest.TestCase):
    def test_t5a_does_not_issue_t5c_or_t5d_artifacts(self):
        self.assertFalse(hasattr(codec, "encode_evaluation_job_json"))
        self.assertFalse(hasattr(codec, "decode_evaluation_job_json"))
        self.assertFalse(hasattr(codec.EvaluationJobV1, "to_dict"))
        self.assertFalse(hasattr(codec.EvaluationJobV1, "from_dict"))
        self.assertFalse(hasattr(codec, "encode_first_divergence_json"))
        self.assertFalse(hasattr(codec, "decode_first_divergence_json"))
        self.assertFalse(hasattr(codec, "first_divergence_identity"))
        self.assertFalse(hasattr(codec.FirstDivergenceV1, "to_dict"))
        self.assertFalse(hasattr(codec.FirstDivergenceV1, "from_dict"))

    def test_divergence_roundtrip_preserves_complete_public_frame(self):
        record = _divergence_record(codec.DIVERGENCE)
        encoded = codec.canonical_first_divergence_field_bytes(record)
        decoded = codec.decode_first_divergence_field_bytes(encoded)
        self.assertEqual(decoded, record)

    def test_no_divergence_terminal_has_only_terminal_payload(self):
        record = _divergence_record(codec.NO_DIVERGENCE_TERMINAL)
        encoded = codec.canonical_first_divergence_field_bytes(record)
        decoded = codec.decode_first_divergence_field_bytes(encoded)
        self.assertEqual(decoded, record)
        self.assertEqual(record.terminal_outcome.win_reason, 7)
        self.assertFalse(hasattr(record, "terminal_outcome_identity"))
        invalid = dataclasses.replace(
            record,
            public_observation_digest="b" * 64,
        )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(invalid)

    def test_failure_before_public_decision_has_no_fabricated_public_fields(self):
        record = _divergence_record(codec.FAILURE_BEFORE_DIVERGENCE)
        encoded = codec.canonical_first_divergence_field_bytes(record)
        decoded = codec.decode_first_divergence_field_bytes(encoded)
        self.assertEqual(decoded, record)
        self.assertEqual(
            decoded.failure_before_divergence.failure_stage,
            "before_public_decision",
        )
        self.assertIsNone(decoded.model_input_identity)
        self.assertIsNone(decoded.candidate_public_action_keys)
        self.assertIsNone(decoded.score_f32_bits)

    def test_union_rejects_wrong_variant_presence_and_partial_bundles(self):
        divergence = _divergence_record(codec.DIVERGENCE)
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    candidate_descriptors=(
                        dataclasses.replace(_descriptor(0), phase=4),
                        _descriptor(1),
                    ),
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    ordered_candidate_domain_identity="d" * 64,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    ordered_candidate_domain_identity=(
                        codec.fallback_ordered_candidate_domain_identity(
                            divergence.candidate_public_action_keys
                        )
                    ),
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(divergence, model_input_identity=None)
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    terminal_outcome=codec.TerminalOutcomeV1(),
                )
            )
        terminal = _divergence_record(codec.NO_DIVERGENCE_TERMINAL)
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    terminal,
                    candidate_count=0,
                )
            )
        failure = _divergence_record(codec.FAILURE_BEFORE_DIVERGENCE)
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    failure,
                    failure_before_divergence=codec.FailureBeforeDivergenceV1(
                        failure_stage="inference",
                        error_code="MODEL_TIMEOUT",
                        failed_decision_ordinal=0,
                    ),
                    semantic_decision_identity="a" * 64,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    candidate_count=2,
                    candidate_public_action_keys=None,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    score_vector_identity="phase6_score_vector.v1." + "e" * 64,
                    score_f32_bits=None,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(
                    divergence,
                    model_selected_public_action_key=_key(0),
                    score_vector_identity=None,
                    score_f32_bits=None,
                )
            )
        with self.assertRaises(codec.CodecError):
            codec.canonical_first_divergence_field_bytes(
                dataclasses.replace(divergence, candidate_count=1)
            )

    def test_all_failure_stage_presence_profiles_are_enforced(self):
        base = _divergence_record(codec.FAILURE_BEFORE_DIVERGENCE)
        frame = {
            "semantic_decision_identity": "a" * 64,
            "public_observation_digest": "b" * 64,
            "ordered_candidate_domain_identity": codec.public_candidate_domain_digest(
                "card_selection", (_key(0), _key(1))
            ),
            "candidate_count": 2,
            "candidate_public_action_keys": (_key(0), _key(1)),
            "candidate_descriptors": (_descriptor(0), _descriptor(1)),
            "decision_request_family": "card_selection",
            "continuation_context": codec.ContinuationContextV1(False, None),
        }
        for stage in (
            "public_frame_validation",
            "model_input_validation",
        ):
            record = dataclasses.replace(
                base,
                **frame,
                failure_before_divergence=dataclasses.replace(
                    base.failure_before_divergence,
                    failure_stage=stage,
                ),
            )
            codec.canonical_first_divergence_field_bytes(record)
        inference = dataclasses.replace(
            base,
            **frame,
            model_input_identity="model_input.v1." + "c" * 64,
            failure_before_divergence=dataclasses.replace(
                base.failure_before_divergence,
                failure_stage="inference",
            ),
            teacher_selected_public_action_key=_key(0),
        )
        codec.canonical_first_divergence_field_bytes(inference)
        selection = dataclasses.replace(
            inference,
            score_vector_identity=codec.score_vector_identity(_score_vector()),
            score_f32_bits=_score_vector().score_f32_bits,
            failure_before_divergence=dataclasses.replace(
                inference.failure_before_divergence,
                failure_stage="selection",
            ),
        )
        codec.canonical_first_divergence_field_bytes(selection)
        for stage in ("environment", "replay", "admission"):
            post_selection = dataclasses.replace(
                selection,
                model_selected_public_action_key=_key(1),
                failure_before_divergence=dataclasses.replace(
                    selection.failure_before_divergence,
                    failure_stage=stage,
                ),
            )
            codec.canonical_first_divergence_field_bytes(post_selection)


class Task5JsonlTests(unittest.TestCase):
    def test_jsonl_is_one_canonical_object_per_lf_line(self):
        records = [{"schema_id": "x.v1", "index": 0}, {"schema_id": "x.v1", "index": 1}]
        encoded = codec.canonical_jsonl_bytes(records)
        self.assertEqual(encoded.count(b"\n"), 2)
        self.assertEqual(codec.parse_canonical_jsonl(encoded), records)
        for invalid in (
            b'{"schema_id":"x.v1","index":0}\r\n',
            b'{"schema_id":"x.v1","index":0}\n\n',
            b'{"schema_id":"x.v1","index":0}\n{"schema_id":"x.v1","index":1}\n ',
        ):
            with self.assertRaises(codec.CodecError):
                codec.parse_canonical_jsonl(invalid)

    def test_jsonl_stream_order_is_not_worker_completion_order(self):
        jobs = _jobs()
        job_ids = tuple(codec.evaluation_job_identity(job) for job in jobs)
        records = [
            job.identity_input_dict() for job in jobs
        ]
        self.assertEqual(
            codec.validate_gameplay_job_order(records, job_ids),
            None,
        )
        with self.assertRaises(codec.CodecError):
            codec.validate_gameplay_job_order(list(reversed(records)), job_ids)
        divergence_records = [
            _divergence_record(codec.FAILURE_BEFORE_DIVERGENCE).to_field_dict()
        ]
        divergence_records[0]["evaluation_job_identity"] = job_ids[3]
        divergence_records.append(
            dataclasses.replace(
                _divergence_record(codec.NO_DIVERGENCE_TERMINAL),
                evaluation_job_identity=job_ids[7],
            ).to_field_dict()
        )
        self.assertIsNone(
            codec.validate_first_divergence_order(
                divergence_records,
                job_ids,
            )
        )
        with self.assertRaises(codec.CodecError):
            codec.validate_first_divergence_order(
                [divergence_records[1], divergence_records[0]],
                job_ids,
            )


if __name__ == "__main__":
    unittest.main()
