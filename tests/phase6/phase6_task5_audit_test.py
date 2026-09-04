import dataclasses
import locale
import subprocess
import sys
import unittest
from pathlib import Path

from tools.phase6 import task5_audit as audit
from tools.phase6 import task5_codec as task5
from tools.phase6 import task5_offline
from tests.phase6 import phase6_task5_offline_test as offline_fixture


SOURCE_COMMIT = "1" * 40


def _context() -> task5.EvaluationContextV1:
    return task5.default_evaluation_context(
        evaluator_semantic_source_commit=SOURCE_COMMIT
    )


def _frame(index: int, *, with_model_input: bool = True) -> audit.PublicDecisionFrameV1:
    descriptors = tuple(
        task5.PublicCandidateDescriptorV1(
            action_kind="card_selection",
            source_reference=task5.PublicReferenceV1(
                reference_kind=0,
                public_locator_token=f"visible:{index}:{candidate}",
                current_entity_ordinal=candidate,
            ),
            source_index=candidate,
        )
        for candidate in range(2)
    )
    keys = tuple(task5.public_action_key(value) for value in descriptors)
    return audit.PublicDecisionFrameV1(
        semantic_decision_identity=f"{index + 1:064x}",
        public_observation_digest=f"{index + 101:064x}",
        model_input_identity=(
            f"model_input.v1.{index + 201:064x}" if with_model_input else None
        ),
        ordered_candidate_domain_identity=task5.public_candidate_domain_digest(
            "card_selection", keys
        ),
        candidate_count=len(keys),
        candidate_public_action_keys=keys,
        candidate_descriptors=descriptors,
        decision_request_family="card_selection",
        continuation_context=task5.ContinuationContextV1(False, None),
        phase=3,
        turn_index=index,
        acting_participant=0,
        locked_deck_role_id=task5.SWORDSOUL_DECK_ID,
        starting_player=0,
        rare_critical_slice="critical" if index == 0 else None,
    )


def _decision(index: int, *, divergent: bool) -> audit.SharedPublicDecisionV1:
    frame = _frame(index)
    if divergent:
        scores = task5.ScoreVectorV1(
            frame.candidate_public_action_keys,
            ("3f000000", "3f800000"),
        )
        teacher_key = frame.candidate_public_action_keys[0]
        model_key = frame.candidate_public_action_keys[1]
    else:
        scores = task5.ScoreVectorV1(
            frame.candidate_public_action_keys,
            ("3f800000", "3f000000"),
        )
        teacher_key = frame.candidate_public_action_keys[0]
        model_key = teacher_key
    return audit.SharedPublicDecisionV1(
        decision_ordinal=index,
        frame=frame,
        teacher_selected_public_action_key=teacher_key,
        model_selected_public_action_key=model_key,
        score_vector=scores,
    )


def _job_evidence(
    job_identity: str,
    *,
    divergent_indices: tuple[int, ...] = (),
    terminal: bool = True,
) -> audit.SharedJobEvidenceV1:
    decisions = tuple(
        _decision(index, divergent=index in divergent_indices)
        for index in range(max(divergent_indices, default=0) + 1)
    )
    outcome = (
        task5.TerminalOutcomeV1(True, None, None) if terminal else None
    )
    return audit.SharedJobEvidenceV1(
        evaluation_job_identity=job_identity,
        started=True,
        decisions=decisions,
        teacher_terminal_outcome=outcome,
        model_terminal_outcome=outcome,
    )


def _failure_job(job_identity: str, stage: str) -> audit.SharedJobEvidenceV1:
    index = 0
    frame = _frame(index, with_model_input=stage not in {
        "public_frame_validation",
        "model_input_validation",
    })
    score = (
        task5.ScoreVectorV1(frame.candidate_public_action_keys, ("3f800000", "3f000000"))
        if stage in {"selection", "environment", "replay", "admission"}
        else None
    )
    teacher_key = frame.candidate_public_action_keys[0] if stage in {
        "inference", "selection", "environment", "replay", "admission"
    } else None
    model_key = frame.candidate_public_action_keys[0] if stage in {
        "environment", "replay", "admission"
    } else None
    codes = {
        "before_public_decision": "INFERENCE_FAILURE",
        "public_frame_validation": "PUBLIC_FRAME_INVALID",
        "model_input_validation": "MODEL_INPUT_INVALID",
        "inference": "INFERENCE_RESPONSE_INVALID",
        "selection": "SELECTION_INVALID",
        "environment": "STEP_REJECTED",
        "replay": "REPLAY_FAILURE",
        "admission": "ADMISSION_FAILURE",
    }
    failure = task5.FailureBeforeDivergenceV1(stage, codes[stage], index)
    return audit.SharedJobEvidenceV1(
        evaluation_job_identity=job_identity,
        started=True,
        decisions=(),
        failure= audit.FailureEventV1(
            failure=failure,
            frame=None if stage == "before_public_decision" else frame,
            score_vector=score,
            teacher_selected_public_action_key=teacher_key,
            model_selected_public_action_key=model_key,
        ),
    )


def _replay(
    context: task5.EvaluationContextV1,
    job_identity: str,
    index: int,
) -> audit.ReplayAdmissionSummaryReadModelV1:
    return audit.ReplayAdmissionSummaryReadModelV1(
        evaluation_identity=task5.evaluation_identity(context.root),
        evaluation_job_identity=job_identity,
        trajectory_record_id=f"trajectory_record.v1.{index + 1:064x}",
        public_gameplay_trajectory_id=f"public_gameplay_trajectory.v1.{index + 101:064x}",
        replay_status="PASS",
        admission_status="PASS",
    )


def _gameplay_result(
    context: task5.EvaluationContextV1,
    job_identity: str,
    replay: audit.ReplayAdmissionSummaryReadModelV1,
    index: int,
) -> audit.GameplayJobResultReadModelV1:
    return audit.GameplayJobResultReadModelV1(
        evaluation_identity=task5.evaluation_identity(context.root),
        evaluation_job_identity=job_identity,
        checkpoint_identity=context.root.checkpoint_identity,
        status="INTERRUPTED",
        started=True,
        terminal_observed=False,
        fallback_assisted=False,
        trajectory_record_id=replay.trajectory_record_id,
        public_gameplay_trajectory_id=replay.public_gameplay_trajectory_id,
        replay_admission_summary_identity=replay.identity,
    )


def _offline_result() -> task5_offline.OfflineEvaluationResultV1:
    population = offline_fixture._build_fixture()
    return task5_offline.evaluate_offline(
        population,
        offline_fixture._config(population),
        lambda sample, width: offline_fixture._scores(sample, width),
    )


def _bc_population(
    context: task5.EvaluationContextV1,
) -> audit.BCInducedPopulationV1:
    job_ids = tuple(task5.evaluation_job_identity(job) for job in context.jobs)
    jobs = tuple(
        _job_evidence(
            job_id,
            divergent_indices=() if index % 2 == 0 else (0,),
        )
        for index, job_id in enumerate(job_ids)
    )
    replays = tuple(_replay(context, job_id, index) for index, job_id in enumerate(job_ids))
    results = tuple(
        _gameplay_result(context, job_id, replays[index], index)
        for index, job_id in enumerate(job_ids)
    )
    gameplay_summary = audit.derive_gameplay_summary(context, results, replays)
    return audit.BCInducedPopulationV1(
        evaluation_corpus_identity=context.root.evaluation_corpus_identity,
        checkpoint_identity=context.root.checkpoint_identity,
        evaluation_contract_identity=context.root.evaluation_contract_identity,
        ordered_job_identities=job_ids,
        shared_jobs=jobs,
        gameplay_job_results=results,
        replay_admission_summaries=replays,
        gameplay_summary=gameplay_summary,
    )


def _bc_population_with_model_input_failure(
    context: task5.EvaluationContextV1,
) -> audit.BCInducedPopulationV1:
    population = _bc_population(context)
    failure_job = _failure_job(population.ordered_job_identities[0], "model_input_validation")
    failure_replay = audit.ReplayAdmissionSummaryReadModelV1(
        evaluation_identity=task5.evaluation_identity(context.root),
        evaluation_job_identity=population.ordered_job_identities[0],
        replay_status="NOT_RUN",
        admission_status="NOT_RUN",
        failure_stage="model_input_validation",
        failure_code="MODEL_INPUT_INVALID",
    )
    failure_result = audit.GameplayJobResultReadModelV1(
        evaluation_identity=task5.evaluation_identity(context.root),
        evaluation_job_identity=population.ordered_job_identities[0],
        checkpoint_identity=context.root.checkpoint_identity,
        status="FAILED",
        started=True,
        terminal_observed=False,
        fallback_assisted=False,
        replay_admission_summary_identity=failure_replay.identity,
        failure_stage="model_input_validation",
        failure_code="MODEL_INPUT_INVALID",
    )
    results = (failure_result,) + population.gameplay_job_results[1:]
    replays = (failure_replay,) + population.replay_admission_summaries[1:]
    return audit.BCInducedPopulationV1(
        evaluation_corpus_identity=population.evaluation_corpus_identity,
        checkpoint_identity=population.checkpoint_identity,
        evaluation_contract_identity=population.evaluation_contract_identity,
        ordered_job_identities=population.ordered_job_identities,
        shared_jobs=(failure_job,) + population.shared_jobs[1:],
        gameplay_job_results=results,
        replay_admission_summaries=replays,
        gameplay_summary=audit.derive_gameplay_summary(context, results, replays),
    )


def _read_model() -> audit.EvaluationReadModelV1:
    context = _context()
    population = _bc_population(context)
    records = tuple(
        audit.derive_first_divergence(job)
        for job in population.shared_jobs
    )
    return audit.build_evaluation_read_model(
        context,
        _offline_result(),
        population,
        records,
    )


class Task5DAuditTests(unittest.TestCase):
    def test_earliest_divergence_is_selected_and_round_trips(self):
        job_id = task5.evaluation_job_identity(_context().jobs[0])
        evidence = _job_evidence(job_id, divergent_indices=(0, 1), terminal=False)
        record = audit.derive_first_divergence(evidence)
        self.assertEqual(record.record_kind, task5.DIVERGENCE)
        self.assertEqual(record.first_divergence_ordinal, 0)
        self.assertEqual(record.observed_public_decision_count, 0)
        encoded = audit.encode_first_divergence_json(record)
        decoded = audit.decode_first_divergence_json(encoded)
        self.assertEqual(decoded, record)
        self.assertTrue(audit.first_divergence_identity(record).startswith(
            "phase6_first_divergence.v1."
        ))

    def test_no_divergence_terminal_has_only_terminal_payload(self):
        job_id = task5.evaluation_job_identity(_context().jobs[0])
        record = audit.derive_first_divergence(_job_evidence(job_id))
        self.assertEqual(record.record_kind, task5.NO_DIVERGENCE_TERMINAL)
        self.assertIsNotNone(record.terminal_outcome)
        for field in (
            "semantic_decision_identity", "public_observation_digest",
            "model_input_identity", "ordered_candidate_domain_identity",
            "candidate_count", "candidate_public_action_keys",
            "candidate_descriptors", "score_vector_identity", "score_f32_bits",
            "teacher_selected_public_action_key", "model_selected_public_action_key",
            "decision_request_family", "continuation_context",
            "first_divergence_ordinal", "failure_before_divergence",
        ):
            self.assertIsNone(getattr(record, field), field)
        self.assertEqual(
            audit.decode_first_divergence_json(audit.encode_first_divergence_json(record)),
            record,
        )

    def test_failure_before_public_decision_has_no_fabricated_fields(self):
        job_id = task5.evaluation_job_identity(_context().jobs[0])
        record = audit.derive_first_divergence(
            _failure_job(job_id, "before_public_decision")
        )
        self.assertEqual(record.record_kind, task5.FAILURE_BEFORE_DIVERGENCE)
        self.assertEqual(record.observed_public_decision_count, 0)
        self.assertEqual(record.failure_before_divergence.failed_decision_ordinal, 0)
        self.assertIsNone(record.public_observation_digest)
        self.assertIsNone(record.score_f32_bits)

    def test_failure_stage_profiles_are_preserved(self):
        job_id = task5.evaluation_job_identity(_context().jobs[0])
        for stage in (
            "public_frame_validation", "model_input_validation", "inference",
            "selection", "environment", "replay", "admission",
        ):
            with self.subTest(stage=stage):
                record = audit.derive_first_divergence(_failure_job(job_id, stage))
                self.assertEqual(record.record_kind, task5.FAILURE_BEFORE_DIVERGENCE)
                self.assertEqual(record.failure_before_divergence.failure_stage, stage)
                record.validate()

    def test_first_divergence_stream_is_started_job_order(self):
        context = _context()
        job_ids = tuple(task5.evaluation_job_identity(job) for job in context.jobs)
        records = tuple(
            audit.derive_first_divergence(_job_evidence(job_id))
            for job_id in job_ids[:3]
        )
        encoded = audit.encode_first_divergence_jsonl(records, job_ids[:3])
        self.assertEqual(audit.decode_first_divergence_jsonl(encoded, job_ids[:3]), records)
        with self.assertRaises(audit.AuditCodecError):
            audit.encode_first_divergence_jsonl(records[::-1], job_ids[:3])
        with self.assertRaises(audit.AuditCodecError):
            audit.encode_first_divergence_jsonl(records[:2], job_ids[:3])

    def test_public_privacy_and_t5a_union_validation_fail_closed(self):
        context = _context()
        job_id = task5.evaluation_job_identity(context.jobs[0])
        record = audit.derive_first_divergence(
            _job_evidence(job_id, divergent_indices=(0,), terminal=False)
        )
        payload = record.to_field_dict()
        payload["raw_response_bytes"] = "forbidden"
        with self.assertRaises((audit.AuditCodecError, task5.CodecError)):
            audit.decode_first_divergence_json(task5.canonical_json_bytes(payload))
        partial = dataclasses.replace(record, candidate_descriptors=None)
        with self.assertRaises(task5.CodecError):
            partial.validate()
        no_scores = dataclasses.replace(record, score_f32_bits=None)
        with self.assertRaises(task5.CodecError):
            no_scores.validate()
        no_score_identity = dataclasses.replace(
            record,
            score_vector_identity=None,
            score_f32_bits=None,
            model_selected_public_action_key=record.model_selected_public_action_key,
        )
        with self.assertRaises(task5.CodecError):
            no_score_identity.validate()
        private_reference = dataclasses.replace(
            record.candidate_descriptors[0].source_reference,
            public_locator_token="private:opponent-hand:0",
        )
        private_descriptor = dataclasses.replace(
            record.candidate_descriptors[0], source_reference=private_reference
        )
        with self.assertRaises((audit.AuditCodecError, task5.CodecError)):
            dataclasses.replace(
                record,
                candidate_descriptors=(private_descriptor, record.candidate_descriptors[1]),
            ).validate()
        early_failure = _failure_job(job_id, "before_public_decision").failure
        with self.assertRaises(audit.AuditCodecError):
            dataclasses.replace(
                early_failure,
                teacher_selected_public_action_key=record.teacher_selected_public_action_key,
            ).validate()

    def test_t5c_read_models_reject_noncanonical_json_and_recompute_identity(self):
        context = _context()
        job_id = task5.evaluation_job_identity(context.jobs[0])
        replay = _replay(context, job_id, 0)
        result = _gameplay_result(context, job_id, replay, 0)
        result_bytes = task5.canonical_json_bytes(result.to_dict())
        decoded = audit.decode_gameplay_job_result_json(result_bytes)
        self.assertEqual(decoded, result)
        malformed = result_bytes.replace(b"{\"", b"{ \"", 1)
        with self.assertRaises(audit.AuditCodecError):
            audit.decode_gameplay_job_result_json(malformed)
        unknown = dict(result.to_dict())
        unknown["unknown"] = "value"
        with self.assertRaises(audit.AuditCodecError):
            audit.decode_gameplay_job_result_json(task5.canonical_json_bytes(unknown))

    def test_distribution_shift_keeps_populations_and_denominators_separate(self):
        context = _context()
        population = _bc_population(context)
        shift = audit.derive_distribution_shift(_offline_result(), population, context)
        self.assertNotEqual(
            shift.teacher_state_population_identity,
            shift.bc_induced_population_identity,
        )
        self.assertEqual(
            shift.bc_profile.decision_request_family_counts,
            (("card_selection", 8),),
        )
        self.assertEqual(shift.bc_profile.replay_rate.denominator, 8)
        self.assertEqual(shift.bc_profile.admission_rate.denominator, 8)
        self.assertIsNotNone(shift.bc_profile.teacher_agreement_rate)
        self.assertEqual(
            audit.decode_distribution_shift_json(
                audit.encode_distribution_shift_json(shift)
            ),
            shift,
        )
        payload = shift.to_dict()
        reordered_payload = {
            key: payload[key] for key in reversed(tuple(payload.keys()))
        }
        self.assertEqual(
            audit.encode_distribution_shift_json(shift),
            task5.canonical_json_bytes(reordered_payload),
        )
        with self.assertRaises(audit.AuditCodecError):
            audit.derive_distribution_shift(
                _offline_result(),
                dataclasses.replace(
                    population,
                    evaluation_corpus_identity=population.evaluation_contract_identity,
                ),
                context,
            )
        bad_rate = dataclasses.replace(
            shift.bc_profile.replay_rate,
            denominator=shift.bc_profile.replay_rate.denominator - 1,
        )
        bad_profile = dataclasses.replace(shift.bc_profile, replay_rate=bad_rate)
        bad_shift = dataclasses.replace(shift, bc_profile=bad_profile)
        with self.assertRaises(audit.AuditValidationError):
            dataclasses.replace(_read_model(), distribution_shift=bad_shift).validate()
        missing_rate_field = shift.to_dict()
        del missing_rate_field["bc_profile"]["replay_rate"]["denominator"]
        with self.assertRaises(audit.AuditCodecError):
            audit.decode_distribution_shift_json(task5.canonical_json_bytes(missing_rate_field))

    def test_read_model_and_report_are_derived_without_manual_overrides(self):
        model = _read_model()
        summary_bytes = audit.encode_evaluation_summary_json(model.summary)
        self.assertEqual(audit.decode_evaluation_summary_json(summary_bytes), model.summary)
        report = audit.generate_report(model)
        self.assertEqual(report, audit.generate_report(dataclasses.replace(model)))
        self.assertEqual(report, audit.generate_report(model))
        self.assertTrue(audit.report_identity(report).startswith("phase6_task5_report.v1."))
        with self.assertRaises(TypeError):
            audit.generate_report(model, manual_gate_status="PASS")
        bad_summary = dataclasses.replace(model.summary, p6_g14_status="PASS")
        with self.assertRaises(audit.AuditCodecError):
            dataclasses.replace(model, summary=bad_summary).validate()
        bad_offline = dataclasses.replace(
            model.offline_evaluation,
            evaluation_identity="phase6_evaluation.v1." + "f" * 64,
        )
        with self.assertRaises((audit.AuditValidationError, task5_offline.OfflineCodecError)):
            dataclasses.replace(model, offline_evaluation=bad_offline).validate()

    def test_report_is_independent_of_available_locale(self):
        model = _read_model()
        expected = audit.generate_report(model)
        original = locale.setlocale(locale.LC_ALL)
        try:
            for candidate in ("C", "German_Germany.1252", "de_DE.UTF-8"):
                try:
                    locale.setlocale(locale.LC_ALL, candidate)
                except locale.Error:
                    continue
                self.assertEqual(audit.generate_report(model), expected)
        finally:
            locale.setlocale(locale.LC_ALL, original)

    def test_identical_public_audit_inputs_are_deterministic(self):
        context = _context()
        job_id = task5.evaluation_job_identity(context.jobs[0])
        record_a = audit.derive_first_divergence(
            _job_evidence(job_id, divergent_indices=(0,), terminal=False)
        )
        record_b = audit.derive_first_divergence(
            _job_evidence(job_id, divergent_indices=(0,), terminal=False)
        )
        self.assertEqual(
            audit.encode_first_divergence_json(record_a),
            audit.encode_first_divergence_json(record_b),
        )
        model_a = _read_model()
        model_b = _read_model()
        started_ids = tuple(
            result.evaluation_job_identity
            for result in model_a.bc_population.gameplay_job_results
            if result.started
        )
        self.assertEqual(
            audit.encode_first_divergence_jsonl(model_a.first_divergences, started_ids),
            audit.encode_first_divergence_jsonl(model_b.first_divergences, started_ids),
        )
        self.assertEqual(
            audit.encode_distribution_shift_json(model_a.distribution_shift),
            audit.encode_distribution_shift_json(model_b.distribution_shift),
        )
        self.assertEqual(
            audit.encode_evaluation_summary_json(model_a.summary),
            audit.encode_evaluation_summary_json(model_b.summary),
        )
        self.assertEqual(audit.generate_report(model_a), audit.generate_report(model_b))

    def test_unproven_bc_capacity_and_padding_never_become_pass(self):
        context = _context()
        population = _bc_population_with_model_input_failure(context)
        profile = audit._bc_profile(population)
        self.assertEqual(
            profile.capacity_compliance_rate.status,
            audit.COMPLIANCE_NOT_RUN_UNPROVEN,
        )
        self.assertEqual(
            profile.padding_compliance_rate.status,
            audit.COMPLIANCE_NOT_RUN_UNPROVEN,
        )
        self.assertNotEqual(profile.capacity_compliance_rate.status, audit.COMPLIANCE_PASS)
        self.assertNotEqual(profile.padding_compliance_rate.status, audit.COMPLIANCE_PASS)

    def test_distribution_shift_preserves_not_present_slice_pairs(self):
        context = _context()
        shift = audit.derive_distribution_shift(_offline_result(), _bc_population(context), context)
        witness = next(
            value
            for value in shift.slice_comparisons
            if value.slice_kind == "candidate_domain_witness"
            and value.coordinates[1] == "129"
        )
        self.assertEqual(witness.teacher_status, audit.SLICE_NOT_PRESENT)
        self.assertEqual(witness.teacher_count, 0)
        self.assertEqual(witness.bc_status, audit.SLICE_NOT_PRESENT)
        self.assertEqual(witness.bc_count, 0)
        self.assertEqual(
            audit.decode_distribution_shift_json(
                audit.encode_distribution_shift_json(shift)
            ).slice_comparisons,
            shift.slice_comparisons,
        )
        omitted = dataclasses.replace(
            shift, slice_comparisons=shift.slice_comparisons[1:]
        )
        with self.assertRaises(audit.AuditValidationError):
            dataclasses.replace(_read_model(), distribution_shift=omitted).validate()

    def test_compliance_status_denominator_combinations_fail_closed(self):
        model = _read_model()
        invalid = dataclasses.replace(
            model.distribution_shift.bc_profile.capacity_compliance_rate,
            status=audit.COMPLIANCE_PASS,
            numerator=0,
            denominator=0,
        )
        with self.assertRaises(audit.AuditCodecError):
            invalid.validate()

    def test_jsonl_noncanonical_and_wrong_population_order_fail_closed(self):
        context = _context()
        job_ids = tuple(task5.evaluation_job_identity(job) for job in context.jobs[:2])
        records = tuple(audit.derive_first_divergence(_job_evidence(job)) for job in job_ids)
        encoded = audit.encode_first_divergence_jsonl(records, job_ids)
        with self.assertRaises(audit.AuditCodecError):
            audit.decode_first_divergence_jsonl(encoded.replace(b"\n", b"\r\n", 1), job_ids)
        with self.assertRaises(audit.AuditCodecError):
            audit.encode_first_divergence_jsonl(records, job_ids[::-1])

    def test_fresh_process_derivation_is_byte_identical(self):
        root = Path(__file__).resolve().parents[2]
        command = [sys.executable, "-B", "-m", "tests.phase6.phase6_task5_audit_test", "--probe"]
        first = subprocess.check_output(command, cwd=root)
        second = subprocess.check_output(command, cwd=root)
        self.assertEqual(first, second)


def _probe() -> None:
    model = _read_model()
    values = (
        audit.encode_first_divergence_jsonl(
            model.first_divergences,
            tuple(
                result.evaluation_job_identity
                for result in model.bc_population.gameplay_job_results
                if result.started
            ),
        ),
        audit.encode_distribution_shift_json(model.distribution_shift),
        audit.encode_evaluation_summary_json(model.summary),
        audit.generate_report(model),
    )
    for value in values:
        print(value.hex() if isinstance(value, bytes) else value.encode("utf-8").hex())


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--probe":
        _probe()
    else:
        unittest.main()
