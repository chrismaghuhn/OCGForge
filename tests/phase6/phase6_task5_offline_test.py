import dataclasses
import hashlib
import subprocess
import sys
import unittest
from pathlib import Path

from tools.phase6 import task4_codec as task4
from tools.phase6 import task5_codec as task5
from tools.phase6 import task5_offline


TEST_COMMIT = "1" * 40
VOCABULARY = "model_card_vocabulary.v1." + "3" * 64


def _episode_for(partition: str, marker: int) -> str:
    for index in range(marker, marker + 100000):
        candidate = hashlib.sha256(f"phase6-test-episode-{index}".encode()).hexdigest()
        if task4.partition_for_episode(candidate) == partition:
            return candidate
    raise AssertionError(f"could not find {partition} fixture")


def _descriptor(index: int) -> task5.PublicCandidateDescriptorV1:
    return task5.PublicCandidateDescriptorV1(
        action_kind="card_selection",
        choice=task5.PublicChoiceV1(kind=1, value=index % 2),
        source_reference=task5.PublicReferenceV1(
            reference_kind=0,
            public_locator_token=f"visible:{index}",
        ),
        phase=3,
        source_index=index,
    )


def _keys(count: int) -> tuple[str, ...]:
    return tuple(task5.public_action_key(_descriptor(index)) for index in range(count))


def _sample(
    count: int,
    episode: str,
    index: int,
    *,
    partition: str | None = None,
    selected_index: int | None = None,
) -> task4.CorpusSampleV1:
    keys = _keys(count)
    selected = index % count if selected_index is None else selected_index
    request_kind = "card_selection"
    return task4.CorpusSampleV1(
        bc_sample_identity="",
        trajectory_record_id="trajectory_record.v1." + f"{index + 1:064x}",
        episode_semantic_id=episode,
        public_semantic_decision_id=f"{index + 101:064x}",
        model_input_identity="model_input.v1." + f"{index + 201:064x}",
        selected_public_action_key=keys[selected],
        partition=partition or task4.partition_for_episode(episode),
        candidate_ordinal=selected,
        ordered_candidate_domain_identity=task5.public_candidate_domain_digest(
            request_kind, keys
        ),
        state_rows=((0.0,) * task4.STATE_ROW_WIDTH,),
        candidate_rows=tuple(
            tuple(float(index == column == 0) for column in range(task4.CANDIDATE_ROW_WIDTH))
            for index in range(count)
        ),
        routing_keys=keys,
        public_candidate_domain_digest=task5.public_candidate_domain_digest(
            request_kind, keys
        ),
        perspective_player=0,
        decision_index=index,
    )


def _with_identity(sample: task4.CorpusSampleV1) -> task4.CorpusSampleV1:
    return dataclasses.replace(sample, bc_sample_identity=task4.bc_sample_identity(sample))


def _authority(samples: tuple[task4.CorpusSampleV1, ...], split: tuple[tuple[str, ...], ...]):
    source_dataset = task5_offline.dataset_semantic_id(
        tuple(sorted(sample.trajectory_record_id for sample in samples))
    )
    corpus = task4.DerivedCorpusV1(
        source_dataset_identity=source_dataset,
        split_identity=task4.split_identity(source_dataset, *split),
        derivation_contract_identity=task4.NUMERIC_PROJECTION_CONTRACT_ID,
        card_vocabulary_identity=VOCABULARY,
        episode_ids=tuple(sorted(set(episode for group in split for episode in group))),
        samples=samples,
    )
    artifact_identity = task4.derived_corpus_content_identity(
        task4.canonical_corpus_bytes(corpus)
    )
    authority = task4.CorpusAdmissionAuthorityV1(
        expected_artifact_identity=artifact_identity,
        source_dataset_identity=source_dataset,
        split_identity=corpus.split_identity,
        card_vocabulary_identity=VOCABULARY,
        train_episode_ids=split[0],
        validation_episode_ids=split[1],
        test_episode_ids=split[2],
        source_samples=tuple(
            task4.source_sample_authority(sample)
            for sample in sorted(samples, key=lambda value: value.bc_sample_identity.encode("utf-8"))
        ),
    )
    return corpus, authority


def _binding(sample: task4.CorpusSampleV1, receipt_id: str, **changes):
    return task5_offline.AdmissionBindingV1(
        trajectory_record_id=sample.trajectory_record_id,
        admission_receipt_identity=receipt_id,
        episode_semantic_id=sample.episode_semantic_id,
        behavior_policy_kind="TEACHER",
        behavior_policy_source_identity=task4.TEACHER_SOURCE_IDENTITIES[0],
        teacher_policy_binding_identity=task5.SWORDSOUL_TEACHER_BINDING,
        **changes,
    )


def _population(samples: tuple[task4.CorpusSampleV1, ...], split):
    corpus, authority = _authority(samples, split)
    members = []
    receipts = []
    receipt_ids = {}
    for index, sample in enumerate(samples):
        gameplay_id = "public_gameplay_trajectory.v1." + f"{index + 301:064x}"
        envelope = f"{index + 401:064x}"
        shard = f"{index + 501:064x}"
        receipt = task5_offline.AdmissionReceiptV1(
            admission_contract_id=task5_offline.ADMISSION_RECEIPT_CONTRACT_ID,
            candidate_shard_artifact_sha256=shard,
            restricted_evidence_artifact_sha256=f"{index + 601:064x}",
            entries=(
                task5_offline.AdmissionReceiptEntryV1(
                    trajectory_record_id=sample.trajectory_record_id,
                    public_gameplay_trajectory_id=gameplay_id,
                    environment_semantic_id=f"{index + 701:064x}",
                    episode_semantic_id=sample.episode_semantic_id,
                    episode_envelope_sha256=envelope,
                    closure_kind=1,
                ),
            ),
        )
        verified = task5_offline.issue_verified_admission_receipt(receipt)
        receipts.append(verified)
        receipt_ids[sample.trajectory_record_id] = verified.receipt_identity
        members.append(
            task5_offline.DatasetManifestMemberV1(
                trajectory_record_id=sample.trajectory_record_id,
                public_gameplay_trajectory_id=gameplay_id,
                admission_receipt_id=verified.receipt_identity,
                candidate_shard_artifact_sha256=shard,
                episode_envelope_sha256=envelope,
            )
        )
    members = tuple(sorted(members, key=lambda member: member.trajectory_record_id))
    manifest = task5_offline.DatasetManifestV1(
        dataset_manifest_schema_id=task5_offline.DATASET_MANIFEST_SCHEMA_ID,
        dataset_identity_schema_id=task5_offline.DATASET_IDENTITY_SCHEMA_ID,
        trusted_trajectory_contract_id=task5_offline.TRUSTED_TRAJECTORY_CONTRACT_ID,
        dataset_semantic_id=task5_offline.dataset_semantic_id(
            tuple(member.trajectory_record_id for member in members)
        ),
        members=members,
    )
    source = tuple(
        task5_offline.OfflineSourceSampleV1(
            sample=sample,
            numeric_input_identity=task4.make_numeric_model_input(
                model_input_identity=sample.model_input_identity,
                state_rows=sample.state_rows,
                candidate_rows=sample.candidate_rows,
                routing_keys=sample.routing_keys,
                public_candidate_domain_digest=sample.public_candidate_domain_digest,
                public_semantic_decision_id=sample.public_semantic_decision_id,
                perspective_player=sample.perspective_player,
                decision_index=sample.decision_index,
            ).numeric_input_identity,
            public_context=task5_offline.PublicSampleContextV1(
                decision_request_family="card_selection",
                phase=3,
                turn_index=sample.decision_index,
                acting_participant=0,
                locked_deck_role_id=task5.SWORDSOUL_DECK_ID,
                starting_player=0,
                continuation=False,
            ),
        )
        for sample in samples
    )
    return task5_offline.TrustedOfflinePopulationV1(
        source_dataset_identity=corpus.source_dataset_identity,
        dataset_manifest_identity=corpus.source_dataset_identity,
        dataset_split_identity=corpus.split_identity,
        evaluation_contract_identity=task5.evaluation_contract_identity(),
        admitted_corpus=corpus,
        admission_authority=authority,
        dataset_manifest=manifest,
        verified_admission_receipts=tuple(receipts),
        admission_bindings=tuple(
            _binding(sample, receipt_ids[sample.trajectory_record_id]) for sample in samples
        ),
        source_samples=source,
    )


def _config(population):
    context = task5.default_evaluation_context(
        evaluator_semantic_source_commit=TEST_COMMIT
    )
    return task5_offline.OfflineEvaluationConfigV1(
        evaluation_context=context,
        source_dataset_identity=population.source_dataset_identity,
        dataset_split_identity=population.dataset_split_identity,
        selected_partitions=("validation", "test"),
        physical_candidate_capacity=None,
        top_k=None,
    )


def _scores(sample: task4.CorpusSampleV1, width: int) -> task5_offline.OfflineScoreBatchV1:
    n = len(sample.routing_keys)
    bits = tuple(task5.score_f32_bits(float(index)) for index in range(n))
    padding = tuple(task5.score_f32_bits(-1000.0) for _ in range(width - n))
    return task5_offline.OfflineScoreBatchV1(
        checkpoint_identity=task5.SMOKE_CHECKPOINT_ID,
        model_input_identity=sample.model_input_identity,
        ordered_candidate_domain_identity=sample.ordered_candidate_domain_identity,
        physical_candidate_width=width,
        score_f32_bits=bits + padding,
        real_candidate_mask=(1,) * n + (0,) * (width - n),
    )


def _inference_response(sample: task4.CorpusSampleV1) -> task4.InferenceResponseV1:
    numeric = task4.make_numeric_model_input(
        model_input_identity=sample.model_input_identity,
        state_rows=sample.state_rows,
        candidate_rows=sample.candidate_rows,
        routing_keys=sample.routing_keys,
        public_candidate_domain_digest=sample.public_candidate_domain_digest,
        public_semantic_decision_id=sample.public_semantic_decision_id,
        perspective_player=sample.perspective_player,
        decision_index=sample.decision_index,
    )
    request = task4.make_inference_request(
        checkpoint_identity=task5.SMOKE_CHECKPOINT_ID,
        model_input=numeric,
    )
    scores = tuple(float(index) for index in range(len(sample.routing_keys)))
    selected = task5.select_score_vector(
        task5.ScoreVectorV1(
            public_action_keys=sample.routing_keys,
            score_f32_bits=tuple(task5.score_f32_bits(value) for value in scores),
        )
    )
    return task4.make_inference_response(request, scores, selected)


def _build_fixture():
    train = _episode_for("train", 0)
    validation = _episode_for("validation", 1)
    test = _episode_for("test", 2)
    samples = (
        _with_identity(_sample(24, validation, 0)),
        _with_identity(_sample(25, test, 1)),
        _with_identity(_sample(129, train, 2)),
    )
    split = ((train,), (validation,), (test,))
    return _population(samples, split)


class Task5BOfflineTests(unittest.TestCase):
    def test_teacher_population_identity_and_exact_evaluation_order(self):
        population = _build_fixture()
        result = task5_offline.evaluate_offline(
            population,
            _config(population),
            lambda sample, width: _scores(sample, width),
        )
        self.assertEqual(
            [item.partition for item in result.sample_results], ["validation", "test"]
        )
        self.assertEqual(
            [item.candidate_count for item in result.sample_results], [24, 25]
        )
        self.assertEqual(result.metrics.total_count, 2)
        self.assertEqual(result.metrics.scored_count, 2)
        self.assertEqual(result.metrics.rejected_count, 0)
        self.assertEqual(result.metrics.unscored_count, 0)
        self.assertTrue(
            result.teacher_state_population_identity.startswith(
                task5_offline.TEACHER_STATE_POPULATION_ID_PREFIX
            )
        )

    def test_capacity_witnesses_and_padding_are_semantic_noops(self):
        for count in (24, 25, 129):
            episode = _episode_for("validation", 1000 + count)
            sample = _with_identity(_sample(count, episode, count))
            train_episode = _episode_for("train", 2000 + count)
            train_sample = _with_identity(_sample(1, train_episode, 200 + count))
            population = _population(
                (sample, train_sample), ((train_episode,), (episode,), ())
            )
            config = _config(population)
            first = task5_offline.evaluate_offline(
                population,
                dataclasses.replace(config, physical_candidate_capacity=count + 3),
                lambda value, width: _scores(value, width),
            )
            second = task5_offline.evaluate_offline(
                population,
                dataclasses.replace(config, physical_candidate_capacity=count + 3),
                lambda value, width: dataclasses.replace(
                    _scores(value, width),
                    score_f32_bits=_scores(value, width).score_f32_bits[:-3]
                    + tuple(task5.score_f32_bits(10**30) for _ in range(3)),
                ),
            )
            self.assertEqual(first.sample_results[0].model_selected_public_action_key,
                             second.sample_results[0].model_selected_public_action_key)
            self.assertEqual(first.sample_results[0].loss_f64_bits,
                             second.sample_results[0].loss_f64_bits)

    def test_membership_split_teacher_label_and_failure_accounting_fail_closed(self):
        population = _build_fixture()
        config = _config(population)
        original = population.source_samples[0].sample
        reordered_keys = (original.routing_keys[1], original.routing_keys[0]) + original.routing_keys[2:]
        mutated_sample = dataclasses.replace(
            original,
            routing_keys=reordered_keys,
            ordered_candidate_domain_identity=task5.public_candidate_domain_digest(
                "card_selection", reordered_keys
            ),
            public_candidate_domain_digest=task5.public_candidate_domain_digest(
                "card_selection", reordered_keys
            ),
        )
        mutated_numeric_identity = task4.make_numeric_model_input(
            model_input_identity=mutated_sample.model_input_identity,
            state_rows=mutated_sample.state_rows,
            candidate_rows=mutated_sample.candidate_rows,
            routing_keys=mutated_sample.routing_keys,
            public_candidate_domain_digest=mutated_sample.public_candidate_domain_digest,
            public_semantic_decision_id=mutated_sample.public_semantic_decision_id,
            perspective_player=mutated_sample.perspective_player,
            decision_index=mutated_sample.decision_index,
        ).numeric_input_identity
        mutated = dataclasses.replace(
            population.source_samples[0],
            sample=mutated_sample,
            numeric_input_identity=mutated_numeric_identity,
        )
        changed = dataclasses.replace(population, source_samples=(mutated,) + population.source_samples[1:])
        result = task5_offline.evaluate_offline(
            changed, config, lambda sample, width: _scores(sample, width)
        )
        self.assertEqual(result.metrics.rejected_count, 1)
        self.assertEqual(result.metrics.scored_count, 1)
        self.assertEqual(
            result.sample_results[0].failure_reason,
            task5_offline.FailureReason.LABEL_MISMATCH,
        )

        train = [item for item in population.source_samples if item.sample.partition == "train"][0]
        with self.assertRaises(task5_offline.OfflineEvaluationError):
            task5_offline.evaluate_offline(
                dataclasses.replace(population, source_samples=(train,)),
                config,
                lambda sample, width: _scores(sample, width),
            )

    def test_wrong_receipt_quarantine_randomlegal_and_width_fail_without_scoring(self):
        population = _build_fixture()
        config = _config(population)
        calls = []

        def scorer(sample, width):
            calls.append(sample.bc_sample_identity)
            return _scores(sample, width)

        result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                verified_admission_receipts=population.verified_admission_receipts[1:],
            ),
            config,
            scorer,
        )
        self.assertEqual(result.sample_results[0].status, "REJECTED")
        self.assertEqual(result.sample_results[0].failure_reason,
                         task5_offline.FailureReason.MISSING_ADMISSION_RECEIPT)
        self.assertEqual(calls, [population.source_samples[1].sample.bc_sample_identity])

        too_small = dataclasses.replace(config, physical_candidate_capacity=23)
        result = task5_offline.evaluate_offline(population, too_small, scorer)
        self.assertEqual(result.metrics.unscored_count, 2)
        self.assertTrue(all(item.status == "UNSCORED" for item in result.sample_results))

        quarantined = dataclasses.replace(
            population.admission_bindings[0], quarantined=True
        )
        result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                admission_bindings=(quarantined,) + population.admission_bindings[1:],
            ),
            config,
            scorer,
        )
        self.assertEqual(
            result.sample_results[0].failure_reason,
            task5_offline.FailureReason.QUARANTINED_TRAJECTORY,
        )

        random_legal = dataclasses.replace(
            population.admission_bindings[0], behavior_policy_kind="RandomLegal"
        )
        result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                admission_bindings=(random_legal,) + population.admission_bindings[1:],
            ),
            config,
            scorer,
        )
        self.assertEqual(
            result.sample_results[0].failure_reason,
            task5_offline.FailureReason.INELIGIBLE_TEACHER_POLICY,
        )

    def test_receipt_and_manifest_commitments_are_recomputed(self):
        population = _build_fixture()
        population.dataset_manifest.validate()
        for receipt in population.verified_admission_receipts:
            receipt.validate()
        task5_offline.validate_dataset_manifest_membership(
            population.dataset_manifest, population.verified_admission_receipts
        )
        member = population.dataset_manifest.members[0]
        bad_member = dataclasses.replace(
            member, candidate_shard_artifact_sha256="f" * 64
        )
        bad_manifest = dataclasses.replace(
            population.dataset_manifest,
            members=(bad_member,) + population.dataset_manifest.members[1:],
        )
        with self.assertRaises(task5_offline.OfflineCodecError):
            task5_offline.validate_dataset_manifest_membership(
                bad_manifest, population.verified_admission_receipts
            )

        bad_receipt = dataclasses.replace(
            population.verified_admission_receipts[0].receipt,
            candidate_shard_artifact_sha256="e" * 64,
        )
        bad_receipt_result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                verified_admission_receipts=(
                    dataclasses.replace(
                        population.verified_admission_receipts[0],
                        receipt=bad_receipt,
                    ),
                ) + population.verified_admission_receipts[1:],
            ),
            _config(population),
            lambda sample, width: _scores(sample, width),
        ).sample_results[0]
        self.assertEqual(
            bad_receipt_result.failure_reason,
            task5_offline.FailureReason.INVALID_ADMISSION_RECEIPT,
        )

    def test_invalid_public_domain_inputs_are_rejected_without_echoing_private_or_invalid_identity(self):
        population = _build_fixture()
        config = _config(population)
        original = population.source_samples[0]
        bad_key_sample = dataclasses.replace(
            original.sample,
            routing_keys=("public_action.v1.00",) + original.sample.routing_keys[1:],
        )
        bad_key_result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                source_samples=(dataclasses.replace(original, sample=bad_key_sample),)
                + population.source_samples[1:],
            ),
            config,
            lambda sample, width: _scores(sample, width),
        ).sample_results[0]
        self.assertEqual(
            bad_key_result.failure_reason,
            task5_offline.FailureReason.CANDIDATE_DOMAIN_FAILURE,
        )
        self.assertIsNone(bad_key_result.candidate_public_action_keys)
        self.assertIsNone(bad_key_result.ordered_candidate_domain_identity)

        duplicate_sample = dataclasses.replace(
            original.sample,
            routing_keys=(original.sample.routing_keys[0],) + original.sample.routing_keys,
        )
        duplicate_result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                source_samples=(dataclasses.replace(original, sample=duplicate_sample),)
                + population.source_samples[1:],
            ),
            config,
            lambda sample, width: _scores(sample, width),
        ).sample_results[0]
        self.assertEqual(
            duplicate_result.failure_reason,
            task5_offline.FailureReason.CANDIDATE_DOMAIN_FAILURE,
        )
        self.assertIsNone(duplicate_result.ordered_candidate_domain_identity)

        changed_identity_sample = dataclasses.replace(
            original.sample,
            model_input_identity="model_input.v1." + "f" * 64,
        )
        changed_identity_result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                source_samples=(dataclasses.replace(original, sample=changed_identity_sample),)
                + population.source_samples[1:],
            ),
            config,
            lambda sample, width: _scores(sample, width),
        ).sample_results[0]
        self.assertEqual(
            changed_identity_result.failure_reason,
            task5_offline.FailureReason.SAMPLE_IDENTITY_MISMATCH,
        )

    def test_padding_mask_and_checkpoint_binding_fail_closed(self):
        population = _build_fixture()
        config = dataclasses.replace(_config(population), physical_candidate_capacity=26)

        def bad_mask(sample, width):
            batch = _scores(sample, width)
            return dataclasses.replace(
                batch,
                real_candidate_mask=(1,) * width,
            )

        result = task5_offline.evaluate_offline(population, config, bad_mask)
        self.assertTrue(
            all(item.failure_reason == task5_offline.FailureReason.PADDING_MASK_VIOLATION
                for item in result.sample_results if item.candidate_count == 24 or item.candidate_count == 25)
        )

        def wrong_checkpoint(sample, width):
            return dataclasses.replace(
                _scores(sample, width),
                checkpoint_identity="phase6_checkpoint.v1." + "f" * 64,
            )

        result = task5_offline.evaluate_offline(population, _config(population), wrong_checkpoint)
        self.assertTrue(all(item.failure_reason == task5_offline.FailureReason.MODEL_BINDING_FAILURE
                            for item in result.sample_results))

    def test_accepted_task4_inference_response_is_bound_before_score_persistence(self):
        population = _build_fixture()
        responses = {
            source.sample.bc_sample_identity: _inference_response(source.sample)
            for source in population.source_samples
            if source.sample.partition in ("validation", "test")
        }
        result = task5_offline.evaluate_offline(
            population,
            _config(population),
            lambda sample, width: responses[sample.bc_sample_identity],
        )
        self.assertTrue(all(item.status == "SCORED" for item in result.sample_results))
        self.assertTrue(all(item.score_vector_identity for item in result.sample_results))

        module_source = Path("tools/phase6/task5_offline.py").read_text(encoding="utf-8")
        self.assertNotIn("import torch", module_source)
        self.assertNotIn("import jax", module_source)

    def test_reversed_source_construction_has_identical_machine_evidence(self):
        population = _build_fixture()
        config = _config(population)
        first = task5_offline.evaluate_offline(
            population, config, lambda sample, width: _scores(sample, width)
        )
        reversed_population = dataclasses.replace(
            population,
            source_samples=tuple(reversed(population.source_samples)),
            admission_bindings=tuple(reversed(population.admission_bindings)),
            verified_admission_receipts=tuple(reversed(population.verified_admission_receipts)),
        )
        second = task5_offline.evaluate_offline(
            reversed_population, config, lambda sample, width: _scores(sample, width)
        )
        self.assertEqual(first.teacher_state_population_identity,
                         second.teacher_state_population_identity)
        self.assertEqual(
            task5_offline.encode_offline_sample_jsonl(first.sample_results),
            task5_offline.encode_offline_sample_jsonl(second.sample_results),
        )
        self.assertEqual(first.metrics, second.metrics)
        self.assertEqual(first.slice_results, second.slice_results)

    def test_score_and_top_k_validation_rejects_nonfinite_wrong_length_and_invalid_k(self):
        population = _build_fixture()
        config = _config(population)
        with self.assertRaises(task5_offline.OfflineEvaluationError):
            task5_offline.evaluate_offline(
                population,
                dataclasses.replace(config, top_k=0),
                lambda sample, width: _scores(sample, width),
            )
        with self.assertRaises(task5_offline.OfflineEvaluationError):
            task5_offline.evaluate_offline(
                population,
                dataclasses.replace(config, top_k=26),
                lambda sample, width: _scores(sample, width),
            )

        def wrong_length(sample, width):
            return dataclasses.replace(_scores(sample, width), score_f32_bits=())

        result = task5_offline.evaluate_offline(population, config, wrong_length)
        self.assertTrue(all(item.status == "UNSCORED" for item in result.sample_results))

        def nonfinite(sample, width):
            bits = list(_scores(sample, width).score_f32_bits)
            bits[0] = "7f800000"
            return dataclasses.replace(_scores(sample, width), score_f32_bits=tuple(bits))

        result = task5_offline.evaluate_offline(population, config, nonfinite)
        self.assertTrue(all(item.failure_reason == task5_offline.FailureReason.NONFINITE_SCORE
                            for item in result.sample_results))

    def test_canonical_sample_stream_and_metrics_round_trip_reject_bad_order_and_fields(self):
        population = _build_fixture()
        result = task5_offline.evaluate_offline(
            population, _config(population), lambda sample, width: _scores(sample, width)
        )
        encoded = task5_offline.encode_offline_sample_jsonl(result.sample_results)
        decoded = task5_offline.decode_offline_sample_jsonl(encoded)
        self.assertEqual(decoded, result.sample_results)
        with self.assertRaises(task5_offline.OfflineCodecError):
            task5_offline.decode_offline_sample_jsonl(encoded.replace(b"\n", b"\r\n", 1))
        with self.assertRaises(task5_offline.OfflineCodecError):
            task5_offline.decode_offline_sample_jsonl(
                task5.canonical_jsonl_bytes(
                    [
                        result.sample_results[1].to_dict(),
                        result.sample_results[0].to_dict(),
                    ]
                )
            )
        payload = result.sample_results[0].to_dict()
        payload["score_vector"]["score_f32_bits"] = [1.0]
        with self.assertRaises(task5_offline.OfflineCodecError):
            task5_offline.OfflineSampleResultV1.from_dict(payload)
        self.assertEqual(
            task5_offline.decode_offline_metrics_json(
                task5_offline.encode_offline_metrics_json(result.metrics)
            ),
            result.metrics,
        )
        self.assertEqual(
            task5_offline.decode_offline_slice_jsonl(
                task5_offline.encode_offline_slice_jsonl(result.slice_results)
            ),
            result.slice_results,
        )

    def test_loss_f64_bits_and_repeated_fresh_process_are_exact(self):
        self.assertEqual(task5_offline.loss_f64_bits(0.0), "0000000000000000")
        self.assertEqual(task5_offline.loss_f64_bits(-0.0), "8000000000000000")
        for value in ("7ff0000000000000", "fff0000000000000", "7ff8000000000000", "3F00000000000000"):
            with self.assertRaises(task5_offline.OfflineCodecError):
                task5_offline.loss_f64_value(value)

        outputs = [
            subprocess.check_output(
                [
                    sys.executable,
                    "-c",
                    "from tools.phase6 import task5_offline; print(task5_offline.loss_f64_bits(1.25))",
                ],
                text=True,
                cwd="C:\\yogiohML",
            )
            for _ in range(2)
        ]
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(outputs[0].strip(), "3ff4000000000000")

    def test_private_public_context_and_candidate_domain_mutations_fail_closed(self):
        population = _build_fixture()
        config = _config(population)
        source = population.source_samples[0]
        bad_context = dataclasses.replace(
            source.public_context,
            decision_request_family="pick",
        )
        result = task5_offline.evaluate_offline(
            dataclasses.replace(
                population,
                source_samples=(dataclasses.replace(source, public_context=bad_context),)
                + population.source_samples[1:],
            ),
            config,
            lambda sample, width: _scores(sample, width),
        )
        self.assertEqual(result.sample_results[0].failure_reason,
                         task5_offline.FailureReason.CANDIDATE_DOMAIN_FAILURE)

        with self.assertRaises(task5_offline.OfflineCodecError):
            task5_offline.PublicSampleContextV1.from_dict({
                "decision_request_family": "card_selection",
                "phase": 3,
                "turn_index": 0,
                "acting_participant": 0,
                "locked_deck_role_id": task5.SWORDSOUL_DECK_ID,
                "starting_player": 0,
                "continuation": False,
                "CoreHost": "private",
            })


if __name__ == "__main__":
    unittest.main()
