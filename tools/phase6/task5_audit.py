"""Phase-6 Task5D public audit, comparison, and derived report layer.

This module consumes validated T5B values, public shared-job evidence, and
the canonical JSON emitted by the accepted T5C implementation.  It owns no
engine, policy, inference, replay, or admission behavior.  T5A DTOs and
primitive encoders remain the authority for public candidate fields, score
vectors, and the FirstDivergence tagged-union bytes.
"""

from __future__ import annotations

import dataclasses
import hashlib
from collections import Counter
from typing import Any, Iterable, Optional, Sequence

from . import task5_codec as task5
from . import task5_offline as task5_offline


class AuditCodecError(task5.CodecError):
    """Raised for malformed, non-canonical, or privacy-invalid T5D values."""


class AuditValidationError(AuditCodecError):
    """Raised when authoritative T5B/T5C child evidence is inconsistent."""


FIRST_DIVERGENCE_SCHEMA_ID = task5.FIRST_DIVERGENCE_SCHEMA_ID
FIRST_DIVERGENCE_ID_PREFIX = task5.FIRST_DIVERGENCE_ID_PREFIX
DISTRIBUTION_SHIFT_SCHEMA_ID = "ocgforge.phase6.distribution_shift.v1"
DISTRIBUTION_SHIFT_IDENTITY_DOMAIN = "ocgforge.phase6.distribution_shift_identity.v1"
DISTRIBUTION_SHIFT_ID_PREFIX = "phase6_distribution_shift.v1."
EVALUATION_SUMMARY_SCHEMA_ID = task5.EVALUATION_SUMMARY_SCHEMA_ID
EVALUATION_SUMMARY_IDENTITY_DOMAIN = "ocgforge.phase6.task5.evaluation_summary_identity.v1"
EVALUATION_SUMMARY_ID_PREFIX = "phase6_evaluation_summary.v1."
REPORT_SCHEMA_ID = "ocgforge.phase6.task5.report.v1"
REPORT_IDENTITY_DOMAIN = "ocgforge.phase6.task5.report_identity.v1"
REPORT_ID_PREFIX = "phase6_task5_report.v1."
BC_INDUCED_POPULATION_SCHEMA_ID = "ocgforge.phase6.bc_induced_population.v1"
BC_INDUCED_POPULATION_IDENTITY_DOMAIN = "ocgforge.phase6.bc_induced_population_identity.v1"
BC_INDUCED_POPULATION_ID_PREFIX = "phase6_bc_induced_population.v1."

P6_G14_STATUS = "NOT_RUN/BLOCKED_BY_MEANINGFUL_BASELINE"
P6_G15_STATUS = "PASS"
DIAGNOSTIC_ONLY_TOKENS = (
    "teacher_agreement_is_diagnostic_only",
    "offline_agreement_is_not_online_parity",
)
COMPLIANCE_PASS = "PASS"
COMPLIANCE_FAIL = "FAIL"
COMPLIANCE_NOT_RUN_UNPROVEN = "NOT_RUN/UNPROVEN"
COMPLIANCE_STATUSES = (
    COMPLIANCE_PASS,
    COMPLIANCE_FAIL,
    COMPLIANCE_NOT_RUN_UNPROVEN,
)
SLICE_PRESENT = task5_offline.SLICE_PRESENT
SLICE_NOT_PRESENT = task5_offline.SLICE_NOT_PRESENT

GAMEPLAY_JOB_STATUSES = (
    "TRUSTED_WIN",
    "TRUSTED_LOSS",
    "TRUSTED_DRAW",
    "INTERRUPTED",
    "FAILED",
    "QUARANTINED",
)
REPLAY_STATUSES = ("NOT_RUN", "PASS", "FAIL", "QUARANTINED")
FAILURE_STAGES = task5.FAILURE_STAGES
PUBLIC_FAILURE_CODES = (
    "INFERENCE_FAILURE",
    "INFERENCE_RESPONSE_INVALID",
    "STEP_REJECTED",
    "STEP_REJECTION_RECORDING_FAILURE",
    "STEP_REJECTION_INTERRUPT_FAILURE",
    "POLICY_FAILURE_INTERRUPT_FAILURE",
    "INVALID_DECISION_FRAME",
    "PUBLIC_FRAME_INVALID",
    "MODEL_INPUT_INVALID",
    "SELECTION_INVALID",
    "POLICY_COMMIT_FAILURE",
    "ENVIRONMENT_FACTORY_REJECTED",
    "RESET_REJECTED",
    "RESET_RECORDING_FAILURE",
    "TERMINAL_VIEW_FAILURE",
    "UNKNOWN_STEP_RESULT",
    "STEP_RECORDING_FAILURE",
    "TRAJECTORY_SEAL_FAILURE",
    "REPLAY_FAILURE",
    "ADMISSION_FAILURE",
    "ADMISSION_RECEIPT_FAILURE",
    "FAILED_TRAJECTORY_CLOSURE",
    "QUARANTINED_TRAJECTORY_PASSED_ADMISSION",
    "GAMEPLAY_FINALIZATION_FAILURE",
    "GAMEPLAY_JOB_EXCEPTION",
    "EVALUATOR_ALREADY_RAN",
    "OPPONENT_POLICY_FAILURE",
    "EVALUATOR_INTERNAL_FAILURE",
)


def _identity(prefix: str, payload: bytes) -> str:
    return prefix + hashlib.sha256(payload).hexdigest()


def _strict_object(payload: Any, fields: Sequence[str], label: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise AuditCodecError(f"{label} is not a JSON object")
    expected = set(fields)
    unknown = set(payload) - expected
    missing = expected - set(payload)
    if unknown:
        raise AuditCodecError(f"{label} has unknown fields: {sorted(unknown)}")
    if missing:
        raise AuditCodecError(f"{label} is missing fields: {sorted(missing)}")
    return payload


def _validate_digest(value: Any, field: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
        character not in "0123456789abcdef" for character in value
    ):
        raise AuditCodecError(f"{field} is not a lowercase SHA-256 digest")


def _validate_prefixed(value: Any, prefix: str, field: str) -> None:
    if not isinstance(value, str) or not value.startswith(prefix):
        raise AuditCodecError(f"{field} has the wrong identity prefix")
    _validate_digest(value[len(prefix):], field)


def _validate_text(value: Any, field: str, *, nonempty: bool = True) -> None:
    if not isinstance(value, str) or (nonempty and not value):
        raise AuditCodecError(f"{field} is not accepted text")
    try:
        value.encode("utf-8", "strict")
    except UnicodeError as error:
        raise AuditCodecError(f"{field} is not strict UTF-8") from error


def _validate_u32(value: Any, field: str) -> None:
    try:
        task5.pack_u32(value)
    except task5.CodecError as error:
        raise AuditCodecError(f"{field} is not a u32") from error


def _validate_u64(value: Any, field: str) -> None:
    try:
        task5.pack_u64(value)
    except task5.CodecError as error:
        raise AuditCodecError(f"{field} is not a u64") from error


def _validate_population_identity(value: Any, field: str) -> None:
    if not isinstance(value, str):
        raise AuditCodecError(f"{field} is not a population identity")
    prefixes = (
        task5_offline.TEACHER_STATE_POPULATION_ID_PREFIX,
        BC_INDUCED_POPULATION_ID_PREFIX,
    )
    if not any(value.startswith(prefix) for prefix in prefixes):
        raise AuditCodecError(f"{field} has an unsupported population identity prefix")
    for prefix in prefixes:
        if value.startswith(prefix):
            _validate_prefixed(value, prefix, field)
            return


def _validate_failure(stage: Optional[str], code: Optional[str]) -> None:
    if (stage is None) != (code is None):
        raise AuditCodecError("failure stage and code must be paired")
    if stage is not None:
        if stage not in FAILURE_STAGES:
            raise AuditCodecError("failure stage is not accepted")
        if code not in PUBLIC_FAILURE_CODES:
            raise AuditCodecError("failure code is not an accepted public token")


def _terminal_from_dict(payload: Any) -> Optional[task5.TerminalOutcomeV1]:
    if payload is None:
        return None
    try:
        return task5.TerminalOutcomeV1.from_dict(payload)
    except (task5.CodecError, TypeError, ValueError) as error:
        raise AuditCodecError(str(error)) from error


def _terminal_to_dict(value: Optional[task5.TerminalOutcomeV1]) -> Any:
    return None if value is None else value.to_dict()


# ---------------------------------------------------------------------------
# Public shared-job evidence and FirstDivergence construction


@dataclasses.dataclass(frozen=True)
class PublicDecisionFrameV1:
    semantic_decision_identity: str
    public_observation_digest: str
    model_input_identity: Optional[str]
    ordered_candidate_domain_identity: str
    candidate_count: int
    candidate_public_action_keys: tuple[str, ...]
    candidate_descriptors: tuple[task5.PublicCandidateDescriptorV1, ...]
    decision_request_family: str
    continuation_context: task5.ContinuationContextV1
    phase: Optional[int] = None
    turn_index: Optional[int] = None
    acting_participant: Optional[int] = None
    locked_deck_role_id: Optional[str] = None
    starting_player: Optional[int] = None
    rare_critical_slice: Optional[str] = None

    def validate(self) -> None:
        _validate_digest(self.semantic_decision_identity, "semantic_decision_identity")
        _validate_digest(self.public_observation_digest, "public_observation_digest")
        if self.model_input_identity is not None:
            _validate_prefixed(self.model_input_identity, task5.MODEL_INPUT_ID_PREFIX, "model_input_identity")
        _validate_u32(self.candidate_count, "candidate_count")
        if self.candidate_count == 0:
            raise AuditCodecError("candidate domain cannot be empty")
        if not isinstance(self.candidate_public_action_keys, tuple):
            raise AuditCodecError("candidate public-action keys are not source ordered")
        if not isinstance(self.candidate_descriptors, tuple):
            raise AuditCodecError("candidate descriptors are not source ordered")
        if len(self.candidate_public_action_keys) != self.candidate_count or len(
            self.candidate_descriptors
        ) != self.candidate_count:
            raise AuditCodecError("candidate frame cardinality is not exact")
        _validate_text(self.decision_request_family, "decision_request_family")
        if self.decision_request_family not in task5.DECISION_KIND_TOKENS:
            raise AuditCodecError("decision_request_family is not accepted")
        if self.phase is not None:
            _validate_u32(self.phase, "phase")
        if self.turn_index is not None:
            _validate_u64(self.turn_index, "turn_index")
        if self.acting_participant is not None and self.acting_participant not in (0, 1):
            raise AuditCodecError("acting_participant is not accepted")
        if self.locked_deck_role_id is not None and self.locked_deck_role_id not in (
            task5.SWORDSOUL_DECK_ID,
            task5.SALAMANGREAT_DECK_ID,
        ):
            raise AuditCodecError("locked_deck_role_id is not accepted")
        if self.starting_player is not None and self.starting_player not in (0, 1):
            raise AuditCodecError("starting_player is not accepted")
        if self.rare_critical_slice is not None:
            _validate_text(self.rare_critical_slice, "rare_critical_slice")
            if any(
                character not in "abcdefghijklmnopqrstuvwxyz0123456789_"
                for character in self.rare_critical_slice
            ):
                raise AuditCodecError("rare_critical_slice is not a lower-case token")
        if not isinstance(self.continuation_context, task5.ContinuationContextV1):
            raise AuditCodecError("continuation_context has the wrong DTO type")
        try:
            self.continuation_context.validate()
        except task5.CodecError as error:
            raise AuditCodecError(str(error)) from error
        try:
            task5.validate_ordered_candidate_domain_identity(
                self.ordered_candidate_domain_identity,
                self.decision_request_family,
                self.candidate_public_action_keys,
            )
        except task5.CodecError as error:
            raise AuditCodecError(str(error)) from error
        for index, (key, descriptor) in enumerate(
            zip(self.candidate_public_action_keys, self.candidate_descriptors)
        ):
            try:
                task5.validate_public_action_key(key)
                descriptor.validate()
                if task5.public_action_key(descriptor) != key:
                    raise AuditCodecError(
                        f"candidate descriptor/key pairing differs at ordinal {index}"
                    )
            except task5.CodecError as error:
                raise AuditCodecError(str(error)) from error
        if len(set(self.candidate_public_action_keys)) != self.candidate_count:
            raise AuditCodecError("candidate public-action keys contain duplicates")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "candidate_count": self.candidate_count,
            "candidate_descriptors": [
                descriptor.to_dict() for descriptor in self.candidate_descriptors
            ],
            "candidate_public_action_keys": list(self.candidate_public_action_keys),
            "continuation_context": self.continuation_context.to_dict(),
            "decision_request_family": self.decision_request_family,
            "acting_participant": self.acting_participant,
            "locked_deck_role_id": self.locked_deck_role_id,
            "model_input_identity": self.model_input_identity,
            "ordered_candidate_domain_identity": self.ordered_candidate_domain_identity,
            "phase": self.phase,
            "public_observation_digest": self.public_observation_digest,
            "rare_critical_slice": self.rare_critical_slice,
            "semantic_decision_identity": self.semantic_decision_identity,
            "starting_player": self.starting_player,
            "turn_index": self.turn_index,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "PublicDecisionFrameV1":
        data = _strict_object(
            payload,
            (
                "semantic_decision_identity",
                "public_observation_digest",
                "model_input_identity",
                "ordered_candidate_domain_identity",
                "candidate_count",
                "candidate_public_action_keys",
                "candidate_descriptors",
                "decision_request_family",
                "continuation_context",
                "phase",
                "turn_index",
                "acting_participant",
                "locked_deck_role_id",
                "starting_player",
                "rare_critical_slice",
            ),
            "PublicDecisionFrameV1",
        )
        try:
            value = cls(
                semantic_decision_identity=data["semantic_decision_identity"],
                public_observation_digest=data["public_observation_digest"],
                model_input_identity=data["model_input_identity"],
                ordered_candidate_domain_identity=data[
                    "ordered_candidate_domain_identity"
                ],
                candidate_count=data["candidate_count"],
                candidate_public_action_keys=tuple(data["candidate_public_action_keys"]),
                candidate_descriptors=tuple(
                    task5.PublicCandidateDescriptorV1.from_dict(item)
                    for item in data["candidate_descriptors"]
                ),
                decision_request_family=data["decision_request_family"],
                continuation_context=task5.ContinuationContextV1.from_dict(
                    data["continuation_context"]
                ),
                phase=data["phase"],
                turn_index=data["turn_index"],
                acting_participant=data["acting_participant"],
                locked_deck_role_id=data["locked_deck_role_id"],
                starting_player=data["starting_player"],
                rare_critical_slice=data["rare_critical_slice"],
            )
        except (task5.CodecError, TypeError, ValueError) as error:
            raise AuditCodecError(str(error)) from error
        value.validate()
        return value


@dataclasses.dataclass(frozen=True)
class SharedPublicDecisionV1:
    decision_ordinal: int
    frame: PublicDecisionFrameV1
    teacher_selected_public_action_key: str
    model_selected_public_action_key: str
    score_vector: task5.ScoreVectorV1

    def validate(self) -> None:
        _validate_u64(self.decision_ordinal, "decision_ordinal")
        self.frame.validate()
        if not isinstance(self.score_vector, task5.ScoreVectorV1):
            raise AuditCodecError("shared decision score vector has the wrong DTO type")
        self.score_vector.validate()
        if self.score_vector.public_action_keys != self.frame.candidate_public_action_keys:
            raise AuditCodecError("shared decision score vector changed candidate order")
        for field in (
            "teacher_selected_public_action_key",
            "model_selected_public_action_key",
        ):
            try:
                task5.validate_public_action_key(getattr(self, field))
            except task5.CodecError as error:
                raise AuditCodecError(str(error)) from error
            if getattr(self, field) not in self.frame.candidate_public_action_keys:
                raise AuditCodecError(f"{field} is outside the candidate domain")
        selected = task5.select_score_vector(self.score_vector)
        if (
            self.model_selected_public_action_key
            != self.frame.candidate_public_action_keys[selected]
        ):
            raise AuditCodecError(
                "shared model selection does not match exact score/tie ordering"
            )

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "decision_ordinal": self.decision_ordinal,
            "frame": self.frame.to_dict(),
            "model_selected_public_action_key": self.model_selected_public_action_key,
            "score_vector": self.score_vector.to_dict(),
            "teacher_selected_public_action_key": self.teacher_selected_public_action_key,
        }


@dataclasses.dataclass(frozen=True)
class FailureEventV1:
    failure: task5.FailureBeforeDivergenceV1
    frame: Optional[PublicDecisionFrameV1] = None
    score_vector: Optional[task5.ScoreVectorV1] = None
    teacher_selected_public_action_key: Optional[str] = None
    model_selected_public_action_key: Optional[str] = None

    def validate(self) -> None:
        if not isinstance(self.failure, task5.FailureBeforeDivergenceV1):
            raise AuditCodecError("failure event has the wrong failure DTO type")
        self.failure.validate()
        stage = self.failure.failure_stage
        _validate_failure(stage, self.failure.error_code)
        if stage == "before_public_decision":
            if any(
                value is not None
                for value in (
                    self.frame,
                    self.score_vector,
                    self.teacher_selected_public_action_key,
                    self.model_selected_public_action_key,
                )
            ):
                raise AuditCodecError("early failure contains fabricated public fields")
            return
        if self.frame is None:
            raise AuditCodecError(f"{stage} failure lacks its public frame")
        self.frame.validate()
        if stage in ("public_frame_validation", "model_input_validation"):
            if self.frame.model_input_identity is not None:
                raise AuditCodecError(f"{stage} failure contains model-input identity")
            if any(
                value is not None
                for value in (
                    self.score_vector,
                    self.teacher_selected_public_action_key,
                    self.model_selected_public_action_key,
                )
            ):
                raise AuditCodecError(f"{stage} failure contains later-stage fields")
            return
        if self.frame.model_input_identity is None:
            raise AuditCodecError(f"{stage} failure lacks model-input identity")
        if stage == "inference":
            if self.score_vector is not None or self.model_selected_public_action_key is not None:
                raise AuditCodecError("inference failure contains score/model selection")
        else:
            if self.score_vector is None:
                raise AuditCodecError(f"{stage} failure lacks score evidence")
            self.score_vector.validate()
            if self.score_vector.public_action_keys != self.frame.candidate_public_action_keys:
                raise AuditCodecError("failure score vector changed candidate order")
        if stage in ("environment", "replay", "admission"):
            if (
                self.teacher_selected_public_action_key is None
                or self.model_selected_public_action_key is None
            ):
                raise AuditCodecError(f"{stage} failure lacks both selected keys")
        for field in (
            "teacher_selected_public_action_key",
            "model_selected_public_action_key",
        ):
            value = getattr(self, field)
            if value is not None:
                try:
                    task5.validate_public_action_key(value)
                except task5.CodecError as error:
                    raise AuditCodecError(str(error)) from error
                if value not in self.frame.candidate_public_action_keys:
                    raise AuditCodecError(f"{field} is outside the failure domain")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "failure": self.failure.to_dict(),
            "frame": None if self.frame is None else self.frame.to_dict(),
            "model_selected_public_action_key": self.model_selected_public_action_key,
            "score_vector": (
                None if self.score_vector is None else self.score_vector.to_dict()
            ),
            "teacher_selected_public_action_key": self.teacher_selected_public_action_key,
        }


@dataclasses.dataclass(frozen=True)
class SharedJobEvidenceV1:
    evaluation_job_identity: str
    started: bool
    decisions: tuple[SharedPublicDecisionV1, ...]
    teacher_terminal_outcome: Optional[task5.TerminalOutcomeV1] = None
    model_terminal_outcome: Optional[task5.TerminalOutcomeV1] = None
    failure: Optional[FailureEventV1] = None

    def validate(self) -> None:
        _validate_prefixed(
            self.evaluation_job_identity,
            task5.EVALUATION_JOB_IDENTITY_PREFIX,
            "evaluation_job_identity",
        )
        if not isinstance(self.started, bool):
            raise AuditCodecError("shared job started is not bool")
        if not isinstance(self.decisions, tuple):
            raise AuditCodecError("shared job decisions are not ordered")
        if not self.started:
            if self.decisions or self.failure is not None or self.teacher_terminal_outcome is not None or self.model_terminal_outcome is not None:
                raise AuditCodecError("unstarted job contains execution evidence")
            return
        for index, decision in enumerate(self.decisions):
            if not isinstance(decision, SharedPublicDecisionV1):
                raise AuditCodecError("shared job contains the wrong decision DTO type")
            decision.validate()
            if decision.decision_ordinal != index:
                raise AuditCodecError("shared decision ordinals are not contiguous")
        if self.failure is not None:
            self.failure.validate()
            if self.failure.failure.failed_decision_ordinal != len(self.decisions):
                raise AuditCodecError("failure ordinal does not follow shared decisions")
            if self.teacher_terminal_outcome is not None or self.model_terminal_outcome is not None:
                raise AuditCodecError("failed shared job contains a terminal outcome")
        elif (self.teacher_terminal_outcome is None) != (self.model_terminal_outcome is None):
            raise AuditCodecError("terminal outcomes are not paired")
        if self.teacher_terminal_outcome is not None:
            self.teacher_terminal_outcome.validate()
            self.model_terminal_outcome.validate()

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "decisions": [decision.to_dict() for decision in self.decisions],
            "evaluation_job_identity": self.evaluation_job_identity,
            "failure": None if self.failure is None else self.failure.to_dict(),
            "model_terminal_outcome": _terminal_to_dict(self.model_terminal_outcome),
            "started": self.started,
            "teacher_terminal_outcome": _terminal_to_dict(self.teacher_terminal_outcome),
        }


def _first_record_from_frame(
    *,
    record_kind: int,
    evaluation_job_identity: str,
    observed_public_decision_count: int,
    frame: Optional[PublicDecisionFrameV1],
    score_vector: Optional[task5.ScoreVectorV1],
    teacher_selected_public_action_key: Optional[str],
    model_selected_public_action_key: Optional[str],
    first_divergence_ordinal: Optional[int],
    terminal_outcome: Optional[task5.TerminalOutcomeV1],
    failure: Optional[task5.FailureBeforeDivergenceV1],
) -> task5.FirstDivergenceV1:
    fields: dict[str, Any] = {
        "record_kind": record_kind,
        "evaluation_job_identity": evaluation_job_identity,
        "observed_public_decision_count": observed_public_decision_count,
        "first_divergence_ordinal": first_divergence_ordinal,
        "terminal_outcome": terminal_outcome,
        "failure_before_divergence": failure,
        "score_vector_identity": None,
        "score_f32_bits": None,
        "semantic_decision_identity": None,
        "public_observation_digest": None,
        "model_input_identity": None,
        "ordered_candidate_domain_identity": None,
        "candidate_count": None,
        "candidate_public_action_keys": None,
        "candidate_descriptors": None,
        "teacher_selected_public_action_key": teacher_selected_public_action_key,
        "model_selected_public_action_key": model_selected_public_action_key,
        "decision_request_family": None,
        "continuation_context": None,
    }
    if frame is not None:
        frame.validate()
        fields.update(
            semantic_decision_identity=frame.semantic_decision_identity,
            public_observation_digest=frame.public_observation_digest,
            model_input_identity=frame.model_input_identity,
            ordered_candidate_domain_identity=frame.ordered_candidate_domain_identity,
            candidate_count=frame.candidate_count,
            candidate_public_action_keys=frame.candidate_public_action_keys,
            candidate_descriptors=frame.candidate_descriptors,
            decision_request_family=frame.decision_request_family,
            continuation_context=frame.continuation_context,
        )
    if score_vector is not None:
        score_vector.validate()
        fields["score_vector_identity"] = task5.score_vector_identity(score_vector)
        fields["score_f32_bits"] = score_vector.score_f32_bits
    record = task5.FirstDivergenceV1(**fields)
    try:
        record.validate()
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    return record


def derive_first_divergence(evidence: SharedJobEvidenceV1) -> task5.FirstDivergenceV1:
    """Return exactly the earliest divergence or explicit terminal/failure record."""

    evidence.validate()
    if not evidence.started:
        raise AuditValidationError("an unstarted job has no FirstDivergence record")
    for index, decision in enumerate(evidence.decisions):
        if (
            decision.teacher_selected_public_action_key
            != decision.model_selected_public_action_key
        ):
            return _first_record_from_frame(
                record_kind=task5.DIVERGENCE,
                evaluation_job_identity=evidence.evaluation_job_identity,
                observed_public_decision_count=index,
                frame=decision.frame,
                score_vector=decision.score_vector,
                teacher_selected_public_action_key=decision.teacher_selected_public_action_key,
                model_selected_public_action_key=decision.model_selected_public_action_key,
                first_divergence_ordinal=index,
                terminal_outcome=None,
                failure=None,
            )
    if evidence.failure is not None:
        failure = evidence.failure
        return _first_record_from_frame(
            record_kind=task5.FAILURE_BEFORE_DIVERGENCE,
            evaluation_job_identity=evidence.evaluation_job_identity,
            observed_public_decision_count=len(evidence.decisions),
            frame=failure.frame,
            score_vector=failure.score_vector,
            teacher_selected_public_action_key=failure.teacher_selected_public_action_key,
            model_selected_public_action_key=failure.model_selected_public_action_key,
            first_divergence_ordinal=None,
            terminal_outcome=None,
            failure=failure.failure,
        )
    if (
        evidence.teacher_terminal_outcome is None
        or evidence.model_terminal_outcome is None
        or evidence.teacher_terminal_outcome != evidence.model_terminal_outcome
    ):
        raise AuditValidationError(
            "shared job ended without a common terminal outcome, divergence, or failure"
        )
    return _first_record_from_frame(
        record_kind=task5.NO_DIVERGENCE_TERMINAL,
        evaluation_job_identity=evidence.evaluation_job_identity,
        observed_public_decision_count=len(evidence.decisions),
        frame=None,
        score_vector=None,
        teacher_selected_public_action_key=None,
        model_selected_public_action_key=None,
        first_divergence_ordinal=None,
        terminal_outcome=evidence.teacher_terminal_outcome,
        failure=None,
    )


def first_divergence_identity(record: task5.FirstDivergenceV1) -> str:
    try:
        return _identity(
            FIRST_DIVERGENCE_ID_PREFIX,
            task5.canonical_first_divergence_field_bytes(record),
        )
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error


def _first_divergence_payload(record: task5.FirstDivergenceV1) -> dict[str, Any]:
    try:
        record.validate()
        payload = record.to_field_dict()
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    payload["first_divergence_identity"] = first_divergence_identity(record)
    return payload


def encode_first_divergence_json(record: task5.FirstDivergenceV1) -> bytes:
    return task5.canonical_json_bytes(_first_divergence_payload(record))


def decode_first_divergence_json(data: bytes) -> task5.FirstDivergenceV1:
    try:
        payload = task5.parse_canonical_json(data)
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    fields = (
        "schema_id", "record_kind", "evaluation_job_identity",
        "observed_public_decision_count", *task5.FIRST_DIVERGENCE_TAIL_FIELD_NAMES,
        "first_divergence_identity",
    )
    values = _strict_object(payload, fields, "FirstDivergenceV1")
    try:
        record = task5.FirstDivergenceV1(
            schema_id=values["schema_id"],
            record_kind=values["record_kind"],
            evaluation_job_identity=values["evaluation_job_identity"],
            observed_public_decision_count=values["observed_public_decision_count"],
            semantic_decision_identity=values["semantic_decision_identity"],
            public_observation_digest=values["public_observation_digest"],
            model_input_identity=values["model_input_identity"],
            ordered_candidate_domain_identity=values["ordered_candidate_domain_identity"],
            candidate_count=values["candidate_count"],
            candidate_public_action_keys=(
                None if values["candidate_public_action_keys"] is None
                else tuple(values["candidate_public_action_keys"])
            ),
            candidate_descriptors=(
                None
                if values["candidate_descriptors"] is None
                else tuple(
                    task5.PublicCandidateDescriptorV1.from_dict(item)
                    for item in values["candidate_descriptors"]
                )
            ),
            score_vector_identity=values["score_vector_identity"],
            score_f32_bits=(
                None if values["score_f32_bits"] is None else tuple(values["score_f32_bits"])
            ),
            teacher_selected_public_action_key=values[
                "teacher_selected_public_action_key"
            ],
            model_selected_public_action_key=values[
                "model_selected_public_action_key"
            ],
            decision_request_family=values["decision_request_family"],
            continuation_context=(
                None
                if values["continuation_context"] is None
                else task5.ContinuationContextV1.from_dict(values["continuation_context"])
            ),
            first_divergence_ordinal=values["first_divergence_ordinal"],
            terminal_outcome=_terminal_from_dict(values["terminal_outcome"]),
            failure_before_divergence=(
                None
                if values["failure_before_divergence"] is None
                else task5.FailureBeforeDivergenceV1.from_dict(
                    values["failure_before_divergence"]
                )
            ),
        )
        record.validate()
    except (task5.CodecError, TypeError, ValueError) as error:
        raise AuditCodecError(str(error)) from error
    if values["first_divergence_identity"] != first_divergence_identity(record):
        raise AuditCodecError("FirstDivergence identity does not match its payload")
    return record


def _require_exact_job_order(
    records: Sequence[task5.FirstDivergenceV1],
    expected_job_identities: Sequence[str],
) -> None:
    expected = tuple(expected_job_identities)
    actual = tuple(record.evaluation_job_identity for record in records)
    for identity in expected:
        _validate_prefixed(identity, task5.EVALUATION_JOB_IDENTITY_PREFIX, "expected job identity")
    if actual != expected:
        raise AuditCodecError("FirstDivergence stream is not exact started-job order")
    if len(set(actual)) != len(actual):
        raise AuditCodecError("FirstDivergence stream contains duplicate jobs")


def encode_first_divergence_jsonl(
    records: Sequence[task5.FirstDivergenceV1],
    expected_job_identities: Sequence[str],
) -> bytes:
    if not isinstance(records, (tuple, list)):
        raise AuditCodecError("FirstDivergence records are not ordered")
    for record in records:
        record.validate()
    _require_exact_job_order(records, expected_job_identities)
    return task5.canonical_jsonl_bytes([_first_divergence_payload(record) for record in records])


def decode_first_divergence_jsonl(
    data: bytes, expected_job_identities: Sequence[str]
) -> tuple[task5.FirstDivergenceV1, ...]:
    try:
        payloads = task5.parse_canonical_jsonl(data)
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    records = tuple(
        decode_first_divergence_json(task5.canonical_json_bytes(payload))
        for payload in payloads
    )
    _require_exact_job_order(records, expected_job_identities)
    return records


# ---------------------------------------------------------------------------
# Strict adapters for the accepted T5C C++ JSON evidence


def _optional_public_id(value: Any, prefix: str, field: str) -> Optional[str]:
    if value is None:
        return None
    _validate_prefixed(value, prefix, field)
    return value


def _status_value(value: Any, values: Sequence[str], field: str) -> str:
    if not isinstance(value, str) or value not in values:
        raise AuditCodecError(f"{field} is not accepted")
    return value


def _stage_value(value: Any) -> Optional[str]:
    if value is None:
        return None
    if value not in FAILURE_STAGES:
        raise AuditCodecError("failure_stage is not accepted")
    return value


@dataclasses.dataclass(frozen=True)
class ReplayAdmissionSummaryReadModelV1:
    evaluation_identity: str
    evaluation_job_identity: str
    trajectory_record_id: Optional[str] = None
    public_gameplay_trajectory_id: Optional[str] = None
    replay_status: str = "NOT_RUN"
    admission_status: str = "NOT_RUN"
    failure_stage: Optional[str] = None
    failure_code: Optional[str] = None
    fallback_assisted: bool = False
    schema_id: str = task5.REPLAY_ADMISSION_SUMMARY_SCHEMA_ID
    declared_identity: str = dataclasses.field(default="", compare=False)

    @property
    def identity(self) -> str:
        return _identity("phase6_replay_admission_summary.v1.", _canonical_replay_bytes(self))

    def validate(self) -> None:
        if self.schema_id != task5.REPLAY_ADMISSION_SUMMARY_SCHEMA_ID:
            raise AuditCodecError("replay/admission summary schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_job_identity, task5.EVALUATION_JOB_IDENTITY_PREFIX, "evaluation_job_identity")
        _status_value(self.replay_status, REPLAY_STATUSES, "replay_status")
        _status_value(self.admission_status, REPLAY_STATUSES, "admission_status")
        _optional_public_id(self.trajectory_record_id, "trajectory_record.v1.", "trajectory_record_id")
        _optional_public_id(self.public_gameplay_trajectory_id, "public_gameplay_trajectory.v1.", "public_gameplay_trajectory_id")
        if not isinstance(self.fallback_assisted, bool):
            raise AuditCodecError("fallback_assisted is not bool")
        _validate_failure(self.failure_stage, self.failure_code)
        if (
            self.replay_status == "FAIL"
            or self.admission_status in ("FAIL", "QUARANTINED")
        ) and (self.failure_stage is None or self.failure_code is None):
            raise AuditCodecError("failed replay/admission result lacks failure reason")
        if self.admission_status == "PASS" and self.replay_status != "PASS":
            raise AuditCodecError("admission cannot pass without replay")
        if self.replay_status == "NOT_RUN" and self.admission_status not in (
            "NOT_RUN", "QUARANTINED"
        ):
            raise AuditCodecError("admission cannot run without replay")
        if self.fallback_assisted and self.admission_status == "PASS":
            raise AuditCodecError("fallback-assisted evidence cannot pass admission")
        if self.declared_identity and self.declared_identity != self.identity:
            raise AuditCodecError("replay/admission summary identity does not recompute")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "admission_status": self.admission_status,
            "evaluation_identity": self.evaluation_identity,
            "evaluation_job_identity": self.evaluation_job_identity,
            "failure_code": self.failure_code,
            "failure_stage": self.failure_stage,
            "fallback_assisted": self.fallback_assisted,
            "public_gameplay_trajectory_id": self.public_gameplay_trajectory_id,
            "replay_admission_summary_identity": self.identity,
            "replay_status": self.replay_status,
            "schema_id": self.schema_id,
            "trajectory_record_id": self.trajectory_record_id,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "ReplayAdmissionSummaryReadModelV1":
        fields = (
            "admission_status", "evaluation_identity", "evaluation_job_identity",
            "failure_code", "failure_stage", "fallback_assisted",
            "public_gameplay_trajectory_id", "replay_admission_summary_identity",
            "replay_status", "schema_id", "trajectory_record_id",
        )
        data = _strict_object(payload, fields, "ReplayAdmissionSummaryV1")
        value = cls(
            evaluation_identity=data["evaluation_identity"],
            evaluation_job_identity=data["evaluation_job_identity"],
            trajectory_record_id=data["trajectory_record_id"],
            public_gameplay_trajectory_id=data["public_gameplay_trajectory_id"],
            replay_status=data["replay_status"],
            admission_status=data["admission_status"],
            failure_stage=data["failure_stage"],
            failure_code=data["failure_code"],
            fallback_assisted=data["fallback_assisted"],
            schema_id=data["schema_id"],
            declared_identity=data["replay_admission_summary_identity"],
        )
        value.validate()
        return value


def _canonical_replay_bytes(value: ReplayAdmissionSummaryReadModelV1) -> bytes:
    return b"".join(
        (
            task5.pack_string("ocgforge.phase6.replay_admission_summary_identity.v1"),
            task5.pack_string("ocgforge.phase6.replay_admission_summary_identity.v1"),
            task5.pack_string(value.schema_id),
            task5.pack_string(value.evaluation_identity),
            task5.pack_string(value.evaluation_job_identity),
            task5.pack_optional(value.trajectory_record_id, task5.pack_string),
            task5.pack_optional(value.public_gameplay_trajectory_id, task5.pack_string),
            task5.pack_u8(REPLAY_STATUSES.index(value.replay_status)),
            task5.pack_u8(REPLAY_STATUSES.index(value.admission_status)),
            task5.pack_optional(
                value.failure_stage,
                lambda stage: task5.pack_u8(FAILURE_STAGES.index(stage)),
            ),
            task5.pack_optional(value.failure_code, task5.pack_string),
            task5.pack_bool(value.fallback_assisted),
        )
    )


@dataclasses.dataclass(frozen=True)
class GameplayJobResultReadModelV1:
    evaluation_identity: str
    evaluation_job_identity: str
    checkpoint_identity: str
    status: str
    started: bool
    terminal_observed: bool
    fallback_assisted: bool
    replay_admission_summary_identity: str
    terminal_outcome: Optional[task5.TerminalOutcomeV1] = None
    trajectory_record_id: Optional[str] = None
    public_gameplay_trajectory_id: Optional[str] = None
    failure_stage: Optional[str] = None
    failure_code: Optional[str] = None
    schema_id: str = task5.GAMEPLAY_JOB_RESULT_SCHEMA_ID
    declared_identity: str = dataclasses.field(default="", compare=False)

    @property
    def identity(self) -> str:
        return _identity("phase6_gameplay_job_result.v1.", _canonical_gameplay_result_bytes(self))

    def validate(self) -> None:
        if self.schema_id != task5.GAMEPLAY_JOB_RESULT_SCHEMA_ID:
            raise AuditCodecError("gameplay job result schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_job_identity, task5.EVALUATION_JOB_IDENTITY_PREFIX, "evaluation_job_identity")
        _validate_prefixed(self.checkpoint_identity, task5.CHECKPOINT_ID_PREFIX, "checkpoint_identity")
        _status_value(self.status, GAMEPLAY_JOB_STATUSES, "status")
        for field, value in (
            ("started", self.started),
            ("terminal_observed", self.terminal_observed),
            ("fallback_assisted", self.fallback_assisted),
        ):
            if not isinstance(value, bool):
                raise AuditCodecError(f"{field} is not bool")
        _optional_public_id(self.trajectory_record_id, "trajectory_record.v1.", "trajectory_record_id")
        _optional_public_id(self.public_gameplay_trajectory_id, "public_gameplay_trajectory.v1.", "public_gameplay_trajectory_id")
        _validate_prefixed(
            self.replay_admission_summary_identity,
            "phase6_replay_admission_summary.v1.",
            "replay_admission_summary_identity",
        )
        _validate_failure(self.failure_stage, self.failure_code)
        if self.terminal_outcome is not None:
            self.terminal_outcome.validate()
        if self.status in ("FAILED", "QUARANTINED") and (
            self.failure_stage is None or self.failure_code is None
        ):
            raise AuditCodecError("failed gameplay result lacks failure reason")
        if self.status == "INTERRUPTED" and (
            self.terminal_observed or self.terminal_outcome is not None
        ):
            raise AuditCodecError("interrupted gameplay result contains terminal outcome")
        trusted = self.status in ("TRUSTED_WIN", "TRUSTED_LOSS", "TRUSTED_DRAW")
        if trusted and (
            not self.started
            or not self.terminal_observed
            or self.terminal_outcome is None
            or self.trajectory_record_id is None
            or self.public_gameplay_trajectory_id is None
            or self.fallback_assisted
            or self.failure_stage is not None
            or self.failure_code is not None
        ):
            raise AuditCodecError("trusted gameplay result lacks clean public admission fields")
        if self.fallback_assisted and trusted:
            raise AuditCodecError("fallback-assisted result cannot be trusted")
        if self.declared_identity and self.declared_identity != self.identity:
            raise AuditCodecError("gameplay job result identity does not recompute")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "checkpoint_identity": self.checkpoint_identity,
            "evaluation_identity": self.evaluation_identity,
            "evaluation_job_identity": self.evaluation_job_identity,
            "failure_code": self.failure_code,
            "failure_stage": self.failure_stage,
            "fallback_assisted": self.fallback_assisted,
            "gameplay_job_result_identity": self.identity,
            "public_gameplay_trajectory_id": self.public_gameplay_trajectory_id,
            "replay_admission_summary_identity": self.replay_admission_summary_identity,
            "schema_id": self.schema_id,
            "started": self.started,
            "status": self.status,
            "terminal_observed": self.terminal_observed,
            "terminal_outcome": _terminal_to_dict(self.terminal_outcome),
            "trajectory_record_id": self.trajectory_record_id,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "GameplayJobResultReadModelV1":
        fields = (
            "checkpoint_identity", "evaluation_identity", "evaluation_job_identity",
            "failure_code", "failure_stage", "fallback_assisted",
            "gameplay_job_result_identity", "public_gameplay_trajectory_id",
            "replay_admission_summary_identity", "schema_id", "started", "status",
            "terminal_observed", "terminal_outcome", "trajectory_record_id",
        )
        data = _strict_object(payload, fields, "GameplayJobResultV1")
        result = cls(
            evaluation_identity=data["evaluation_identity"],
            evaluation_job_identity=data["evaluation_job_identity"],
            checkpoint_identity=data["checkpoint_identity"],
            status=data["status"],
            started=data["started"],
            terminal_observed=data["terminal_observed"],
            fallback_assisted=data["fallback_assisted"],
            terminal_outcome=_terminal_from_dict(data["terminal_outcome"]),
            trajectory_record_id=data["trajectory_record_id"],
            public_gameplay_trajectory_id=data["public_gameplay_trajectory_id"],
            replay_admission_summary_identity=data[
                "replay_admission_summary_identity"
            ],
            failure_stage=data["failure_stage"],
            failure_code=data["failure_code"],
            schema_id=data["schema_id"],
            declared_identity=data["gameplay_job_result_identity"],
        )
        result.validate()
        return result


def _canonical_gameplay_result_bytes(value: GameplayJobResultReadModelV1) -> bytes:
    return b"".join(
        (
            task5.pack_string("ocgforge.phase6.gameplay_job_result_identity.v1"),
            task5.pack_string("ocgforge.phase6.gameplay_job_result_identity.v1"),
            task5.pack_string(value.schema_id),
            task5.pack_string(value.evaluation_identity),
            task5.pack_string(value.evaluation_job_identity),
            task5.pack_string(value.checkpoint_identity),
            task5.pack_u8(GAMEPLAY_JOB_STATUSES.index(value.status)),
            task5.pack_bool(value.started),
            task5.pack_bool(value.terminal_observed),
            task5.pack_bool(value.fallback_assisted),
            task5.pack_optional(value.terminal_outcome, task5.canonical_terminal_outcome_bytes),
            task5.pack_optional(value.trajectory_record_id, task5.pack_string),
            task5.pack_optional(value.public_gameplay_trajectory_id, task5.pack_string),
            task5.pack_string(value.replay_admission_summary_identity),
            task5.pack_optional(
                value.failure_stage,
                lambda stage: task5.pack_u8(FAILURE_STAGES.index(stage)),
            ),
            task5.pack_optional(value.failure_code, task5.pack_string),
        )
    )


@dataclasses.dataclass(frozen=True)
class GameplaySummaryReadModelV1:
    evaluation_identity: str
    evaluation_corpus_identity: str
    evaluation_job_manifest_identity: str
    checkpoint_identity: str
    gameplay_job_result_identities: tuple[str, ...]
    scheduled_job_count: int
    started_job_count: int
    completed_terminal_job_count: int
    trusted_win_count: int
    trusted_loss_count: int
    trusted_draw_count: int
    interrupted_job_count: int
    failed_job_count: int
    quarantined_job_count: int
    fallback_assisted_job_count: int
    replay_failure_count: int
    admission_failure_count: int
    inference_failure_count: int
    wilson_numerator: int
    wilson_denominator: int
    wilson_interval_status: str
    schema_id: str = task5.GAMEPLAY_SUMMARY_SCHEMA_ID
    wilson_metric_identity: str = "ocgforge.phase6.gameplay_metrics.wilson_95.v1"
    declared_identity: str = dataclasses.field(default="", compare=False)

    @property
    def identity(self) -> str:
        return _identity("phase6_gameplay_summary.v1.", _canonical_gameplay_summary_bytes(self))

    def validate(self) -> None:
        if self.schema_id != task5.GAMEPLAY_SUMMARY_SCHEMA_ID:
            raise AuditCodecError("gameplay summary schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_corpus_identity, task5.EVALUATION_CORPUS_IDENTITY_PREFIX, "evaluation_corpus_identity")
        _validate_prefixed(self.evaluation_job_manifest_identity, task5.JOB_MANIFEST_ID_PREFIX, "evaluation_job_manifest_identity")
        _validate_prefixed(self.checkpoint_identity, task5.CHECKPOINT_ID_PREFIX, "checkpoint_identity")
        if self.wilson_metric_identity != "ocgforge.phase6.gameplay_metrics.wilson_95.v1":
            raise AuditCodecError("Wilson metric identity is not accepted")
        if self.wilson_interval_status not in ("AVAILABLE", "NOT_APPLICABLE"):
            raise AuditCodecError("Wilson interval status is not accepted")
        if not isinstance(self.gameplay_job_result_identities, tuple):
            raise AuditCodecError("gameplay result identities are not ordered")
        for identity in self.gameplay_job_result_identities:
            _validate_prefixed(identity, "phase6_gameplay_job_result.v1.", "gameplay_job_result_identity")
        if len(set(self.gameplay_job_result_identities)) != len(self.gameplay_job_result_identities):
            raise AuditCodecError("gameplay result identities contain duplicates")
        counters = (
            self.scheduled_job_count,
            self.started_job_count,
            self.completed_terminal_job_count,
            self.trusted_win_count,
            self.trusted_loss_count,
            self.trusted_draw_count,
            self.interrupted_job_count,
            self.failed_job_count,
            self.quarantined_job_count,
            self.fallback_assisted_job_count,
            self.replay_failure_count,
            self.admission_failure_count,
            self.inference_failure_count,
            self.wilson_numerator,
            self.wilson_denominator,
        )
        for index, value in enumerate(counters):
            _validate_u32(value, f"gameplay summary counter {index}")
        if len(self.gameplay_job_result_identities) != self.scheduled_job_count:
            raise AuditCodecError("gameplay result vector does not match schedule")
        classified = (
            self.trusted_win_count
            + self.trusted_loss_count
            + self.trusted_draw_count
            + self.interrupted_job_count
            + self.failed_job_count
            + self.quarantined_job_count
        )
        if classified != self.scheduled_job_count:
            raise AuditCodecError("gameplay summary status counts do not conserve jobs")
        if self.started_job_count > self.scheduled_job_count:
            raise AuditCodecError("started count exceeds scheduled count")
        if self.completed_terminal_job_count != self.trusted_win_count + self.trusted_loss_count + self.trusted_draw_count:
            raise AuditCodecError("terminal count does not conserve outcomes")
        if self.wilson_numerator != self.trusted_win_count or self.wilson_denominator != self.trusted_win_count + self.trusted_loss_count:
            raise AuditCodecError("Wilson numerator/denominator is not derived")
        if any(
            value > self.scheduled_job_count
            for value in (
                self.fallback_assisted_job_count,
                self.replay_failure_count,
                self.admission_failure_count,
                self.inference_failure_count,
            )
        ):
            raise AuditCodecError("gameplay failure counter exceeds schedule")
        if (self.wilson_denominator == 0) != (self.wilson_interval_status == "NOT_APPLICABLE"):
            raise AuditCodecError("Wilson availability does not match denominator")
        if self.declared_identity and self.declared_identity != self.identity:
            raise AuditCodecError("gameplay summary identity does not recompute")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "admission_failure_count": self.admission_failure_count,
            "checkpoint_identity": self.checkpoint_identity,
            "completed_terminal_job_count": self.completed_terminal_job_count,
            "evaluation_corpus_identity": self.evaluation_corpus_identity,
            "evaluation_identity": self.evaluation_identity,
            "evaluation_job_manifest_identity": self.evaluation_job_manifest_identity,
            "failed_job_count": self.failed_job_count,
            "fallback_assisted_job_count": self.fallback_assisted_job_count,
            "gameplay_job_result_identities": list(self.gameplay_job_result_identities),
            "gameplay_summary_identity": self.identity,
            "inference_failure_count": self.inference_failure_count,
            "interrupted_job_count": self.interrupted_job_count,
            "quarantined_job_count": self.quarantined_job_count,
            "replay_failure_count": self.replay_failure_count,
            "scheduled_job_count": self.scheduled_job_count,
            "started_job_count": self.started_job_count,
            "schema_id": self.schema_id,
            "trusted_draw_count": self.trusted_draw_count,
            "trusted_loss_count": self.trusted_loss_count,
            "trusted_win_count": self.trusted_win_count,
            "wilson_denominator": self.wilson_denominator,
            "wilson_interval_status": self.wilson_interval_status,
            "wilson_metric_identity": self.wilson_metric_identity,
            "wilson_numerator": self.wilson_numerator,
        }


def _canonical_gameplay_summary_bytes(value: GameplaySummaryReadModelV1) -> bytes:
    return b"".join(
        (
            task5.pack_string("ocgforge.phase6.gameplay_summary_identity.v1"),
            task5.pack_string("ocgforge.phase6.gameplay_summary_identity.v1"),
            task5.pack_string(value.schema_id),
            task5.pack_string(value.evaluation_identity),
            task5.pack_string(value.evaluation_corpus_identity),
            task5.pack_string(value.evaluation_job_manifest_identity),
            task5.pack_string(value.checkpoint_identity),
            task5.pack_string_vector(value.gameplay_job_result_identities),
            task5.pack_u32(value.scheduled_job_count),
            task5.pack_u32(value.started_job_count),
            task5.pack_u32(value.completed_terminal_job_count),
            task5.pack_u32(value.trusted_win_count),
            task5.pack_u32(value.trusted_loss_count),
            task5.pack_u32(value.trusted_draw_count),
            task5.pack_u32(value.interrupted_job_count),
            task5.pack_u32(value.failed_job_count),
            task5.pack_u32(value.quarantined_job_count),
            task5.pack_u32(value.fallback_assisted_job_count),
            task5.pack_u32(value.replay_failure_count),
            task5.pack_u32(value.admission_failure_count),
            task5.pack_u32(value.inference_failure_count),
            task5.pack_string(value.wilson_metric_identity),
            task5.pack_u32(value.wilson_numerator),
            task5.pack_u32(value.wilson_denominator),
            task5.pack_string(value.wilson_interval_status),
        )
    )


def decode_replay_admission_summary_json(data: bytes) -> ReplayAdmissionSummaryReadModelV1:
    try:
        return ReplayAdmissionSummaryReadModelV1.from_dict(task5.parse_canonical_json(data))
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error


def encode_replay_admission_summary_json(
    value: ReplayAdmissionSummaryReadModelV1,
) -> bytes:
    return task5.canonical_json_bytes(value.to_dict())


def decode_gameplay_job_result_json(data: bytes) -> GameplayJobResultReadModelV1:
    try:
        return GameplayJobResultReadModelV1.from_dict(task5.parse_canonical_json(data))
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error


def encode_gameplay_job_result_json(value: GameplayJobResultReadModelV1) -> bytes:
    return task5.canonical_json_bytes(value.to_dict())


def decode_gameplay_job_results_jsonl(
    data: bytes, expected_job_identities: Sequence[str]
) -> tuple[GameplayJobResultReadModelV1, ...]:
    try:
        payloads = task5.parse_canonical_jsonl(data)
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    values = tuple(
        GameplayJobResultReadModelV1.from_dict(payload) for payload in payloads
    )
    actual = tuple(value.evaluation_job_identity for value in values)
    if actual != tuple(expected_job_identities):
        raise AuditCodecError("gameplay result JSONL is not in manifest order")
    for value in values:
        value.validate()
    return values


def encode_gameplay_job_results_jsonl(
    values: Sequence[GameplayJobResultReadModelV1],
    expected_job_identities: Sequence[str],
) -> bytes:
    actual = tuple(value.evaluation_job_identity for value in values)
    if actual != tuple(expected_job_identities):
        raise AuditCodecError("gameplay result JSONL is not in manifest order")
    return task5.canonical_jsonl_bytes([value.to_dict() for value in values])


def decode_gameplay_summary_json(data: bytes) -> GameplaySummaryReadModelV1:
    try:
        payload = task5.parse_canonical_json(data)
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    fields = (
        "admission_failure_count", "checkpoint_identity",
        "completed_terminal_job_count", "evaluation_corpus_identity",
        "evaluation_identity", "evaluation_job_manifest_identity",
        "failed_job_count", "fallback_assisted_job_count",
        "gameplay_job_result_identities", "gameplay_summary_identity",
        "inference_failure_count", "interrupted_job_count",
        "quarantined_job_count", "replay_failure_count",
        "scheduled_job_count", "started_job_count", "schema_id",
        "trusted_draw_count", "trusted_loss_count", "trusted_win_count",
        "wilson_denominator", "wilson_interval_status",
        "wilson_metric_identity", "wilson_numerator",
    )
    value = _strict_object(payload, fields, "GameplaySummaryV1")
    result = GameplaySummaryReadModelV1(
        evaluation_identity=value["evaluation_identity"],
        evaluation_corpus_identity=value["evaluation_corpus_identity"],
        evaluation_job_manifest_identity=value["evaluation_job_manifest_identity"],
        checkpoint_identity=value["checkpoint_identity"],
        gameplay_job_result_identities=tuple(value["gameplay_job_result_identities"]),
        scheduled_job_count=value["scheduled_job_count"],
        started_job_count=value["started_job_count"],
        completed_terminal_job_count=value["completed_terminal_job_count"],
        trusted_win_count=value["trusted_win_count"],
        trusted_loss_count=value["trusted_loss_count"],
        trusted_draw_count=value["trusted_draw_count"],
        interrupted_job_count=value["interrupted_job_count"],
        failed_job_count=value["failed_job_count"],
        quarantined_job_count=value["quarantined_job_count"],
        fallback_assisted_job_count=value["fallback_assisted_job_count"],
        replay_failure_count=value["replay_failure_count"],
        admission_failure_count=value["admission_failure_count"],
        inference_failure_count=value["inference_failure_count"],
        wilson_numerator=value["wilson_numerator"],
        wilson_denominator=value["wilson_denominator"],
        wilson_interval_status=value["wilson_interval_status"],
        schema_id=value["schema_id"],
        wilson_metric_identity=value["wilson_metric_identity"],
        declared_identity=value["gameplay_summary_identity"],
    )
    result.validate()
    return result


def encode_gameplay_summary_json(value: GameplaySummaryReadModelV1) -> bytes:
    return task5.canonical_json_bytes(value.to_dict())


def derive_gameplay_summary(
    context: task5.EvaluationContextV1,
    results: Sequence[GameplayJobResultReadModelV1],
    replays: Sequence[ReplayAdmissionSummaryReadModelV1],
) -> GameplaySummaryReadModelV1:
    context.validate()
    job_ids = tuple(task5.evaluation_job_identity(job) for job in context.jobs)
    if tuple(value.evaluation_job_identity for value in results) != job_ids:
        raise AuditValidationError("gameplay result population differs from job manifest")
    if tuple(value.evaluation_job_identity for value in replays) != job_ids:
        raise AuditValidationError("replay population differs from job manifest")
    for result, replay in zip(results, replays):
        result.validate()
        replay.validate()
        if result.evaluation_identity != task5.evaluation_identity(context.root):
            raise AuditValidationError("gameplay result evaluation identity differs")
        if replay.evaluation_identity != result.evaluation_identity:
            raise AuditValidationError("replay evaluation identity differs")
        if result.replay_admission_summary_identity != replay.identity:
            raise AuditValidationError("gameplay result is not bound to replay summary")
        if result.checkpoint_identity != context.root.checkpoint_identity:
            raise AuditValidationError("gameplay result checkpoint differs")
    count = Counter(value.status for value in results)
    trusted_wins = count["TRUSTED_WIN"]
    trusted_losses = count["TRUSTED_LOSS"]
    summary = GameplaySummaryReadModelV1(
        evaluation_identity=task5.evaluation_identity(context.root),
        evaluation_corpus_identity=context.root.evaluation_corpus_identity,
        evaluation_job_manifest_identity=task5.evaluation_job_manifest_identity(
            context.job_manifest
        ),
        checkpoint_identity=context.root.checkpoint_identity,
        gameplay_job_result_identities=tuple(value.identity for value in results),
        scheduled_job_count=len(results),
        started_job_count=sum(value.started for value in results),
        completed_terminal_job_count=count["TRUSTED_WIN"] + count["TRUSTED_LOSS"] + count["TRUSTED_DRAW"],
        trusted_win_count=trusted_wins,
        trusted_loss_count=trusted_losses,
        trusted_draw_count=count["TRUSTED_DRAW"],
        interrupted_job_count=count["INTERRUPTED"],
        failed_job_count=count["FAILED"],
        quarantined_job_count=count["QUARANTINED"],
        fallback_assisted_job_count=sum(value.fallback_assisted for value in results),
        replay_failure_count=sum(value.replay_status == "FAIL" for value in replays),
        admission_failure_count=sum(value.admission_status == "FAIL" for value in replays),
        inference_failure_count=sum(value.failure_stage == "inference" for value in results),
        wilson_numerator=trusted_wins,
        wilson_denominator=trusted_wins + trusted_losses,
        wilson_interval_status=(
            "AVAILABLE" if trusted_wins + trusted_losses else "NOT_APPLICABLE"
        ),
    )
    summary.validate()
    return summary


# ---------------------------------------------------------------------------
# BC-induced population and separate distribution-shift evidence


@dataclasses.dataclass(frozen=True)
class BCInducedPopulationV1:
    evaluation_corpus_identity: str
    checkpoint_identity: str
    evaluation_contract_identity: str
    ordered_job_identities: tuple[str, ...]
    shared_jobs: tuple[SharedJobEvidenceV1, ...]
    gameplay_job_results: tuple[GameplayJobResultReadModelV1, ...]
    replay_admission_summaries: tuple[ReplayAdmissionSummaryReadModelV1, ...]
    gameplay_summary: GameplaySummaryReadModelV1
    schema_id: str = BC_INDUCED_POPULATION_SCHEMA_ID
    declared_identity: str = dataclasses.field(default="", compare=False)

    @property
    def identity(self) -> str:
        return _identity(BC_INDUCED_POPULATION_ID_PREFIX, self._canonical_bytes())

    def validate(self, context: Optional[task5.EvaluationContextV1] = None) -> None:
        if self.schema_id != BC_INDUCED_POPULATION_SCHEMA_ID:
            raise AuditCodecError("BC-induced population schema is not accepted")
        _validate_prefixed(self.evaluation_corpus_identity, task5.EVALUATION_CORPUS_IDENTITY_PREFIX, "evaluation_corpus_identity")
        _validate_prefixed(self.checkpoint_identity, task5.CHECKPOINT_ID_PREFIX, "checkpoint_identity")
        _validate_prefixed(self.evaluation_contract_identity, task5.EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        if not isinstance(self.ordered_job_identities, tuple) or not self.ordered_job_identities:
            raise AuditCodecError("BC-induced job vector is not ordered")
        for identity in self.ordered_job_identities:
            _validate_prefixed(identity, task5.EVALUATION_JOB_IDENTITY_PREFIX, "ordered_job_identity")
        if len(set(self.ordered_job_identities)) != len(self.ordered_job_identities):
            raise AuditCodecError("BC-induced job vector contains duplicates")
        if tuple(job.evaluation_job_identity for job in self.shared_jobs) != self.ordered_job_identities:
            raise AuditCodecError("BC-induced shared jobs changed schedule order")
        if tuple(result.evaluation_job_identity for result in self.gameplay_job_results) != self.ordered_job_identities:
            raise AuditCodecError("BC-induced gameplay results changed schedule order")
        if tuple(replay.evaluation_job_identity for replay in self.replay_admission_summaries) != self.ordered_job_identities:
            raise AuditCodecError("BC-induced replay results changed schedule order")
        for job, result in zip(self.shared_jobs, self.gameplay_job_results):
            job.validate()
            result.validate()
            if job.started != result.started:
                raise AuditValidationError("shared-job started state differs from gameplay result")
        for result, replay in zip(
            self.gameplay_job_results, self.replay_admission_summaries
        ):
            if result.replay_admission_summary_identity != replay.identity:
                raise AuditValidationError("gameplay result is not bound to replay summary")
            if result.evaluation_identity != replay.evaluation_identity:
                raise AuditValidationError("gameplay/replay evaluation identity differs")
            replay.validate()
        if not isinstance(self.gameplay_summary, GameplaySummaryReadModelV1):
            raise AuditCodecError("BC-induced gameplay summary is required")
        self.gameplay_summary.validate()
        if tuple(self.gameplay_summary.gameplay_job_result_identities) != tuple(
            result.identity for result in self.gameplay_job_results
        ):
            raise AuditCodecError("BC-induced gameplay summary is not child-derived")
        if self.gameplay_summary.evaluation_identity != self.gameplay_job_results[0].evaluation_identity:
            raise AuditValidationError("BC-induced gameplay summary evaluation identity differs")
        if (
            self.gameplay_summary.evaluation_corpus_identity != self.evaluation_corpus_identity
            or self.gameplay_summary.checkpoint_identity != self.checkpoint_identity
        ):
            raise AuditValidationError("BC-induced gameplay summary corpus/checkpoint differs")
        if context is not None:
            context.validate()
            expected_jobs = tuple(task5.evaluation_job_identity(job) for job in context.jobs)
            if self.ordered_job_identities != expected_jobs:
                raise AuditValidationError("BC-induced job vector differs from evaluation context")
            if self.evaluation_corpus_identity != context.root.evaluation_corpus_identity or self.checkpoint_identity != context.root.checkpoint_identity or self.evaluation_contract_identity != context.root.evaluation_contract_identity:
                raise AuditValidationError("BC-induced population context differs from evaluation context")
            if self.gameplay_summary.evaluation_job_manifest_identity != task5.evaluation_job_manifest_identity(context.job_manifest):
                raise AuditValidationError("BC-induced gameplay summary job manifest differs")
            derived_summary = derive_gameplay_summary(
                context, self.gameplay_job_results, self.replay_admission_summaries
            )
            if self.gameplay_summary != derived_summary:
                raise AuditValidationError("BC-induced gameplay summary differs from children")
        if self.declared_identity and self.declared_identity != self.identity:
            raise AuditCodecError("BC-induced population identity does not recompute")

    def _payload(self) -> dict[str, Any]:
        return {
            "checkpoint_identity": self.checkpoint_identity,
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_corpus_identity": self.evaluation_corpus_identity,
            "gameplay_job_result_identities": [
                result.identity for result in self.gameplay_job_results
            ],
            "gameplay_summary_identity": self.gameplay_summary.identity,
            "ordered_job_identities": list(self.ordered_job_identities),
            "replay_admission_summary_identities": [
                replay.identity for replay in self.replay_admission_summaries
            ],
            "identity_domain": BC_INDUCED_POPULATION_IDENTITY_DOMAIN,
            "identity_schema": BC_INDUCED_POPULATION_SCHEMA_ID,
            "schema_id": self.schema_id,
            "shared_jobs": [job.to_dict() for job in self.shared_jobs],
        }

    def _canonical_bytes(self) -> bytes:
        return task5.canonical_json_bytes(self._payload())

    def to_dict(self) -> dict[str, Any]:
        payload = self._payload()
        payload["bc_induced_population_identity"] = self.identity
        return payload


def encode_bc_induced_population_json(value: BCInducedPopulationV1) -> bytes:
    return task5.canonical_json_bytes(value.to_dict())


@dataclasses.dataclass(frozen=True)
class RateV1:
    population_identity: str
    numerator: int
    denominator: int

    def validate(self) -> None:
        _validate_population_identity(self.population_identity, "population_identity")
        _validate_u64(self.numerator, "rate.numerator")
        _validate_u64(self.denominator, "rate.denominator")
        if self.numerator > self.denominator:
            raise AuditCodecError("rate numerator exceeds denominator")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "denominator": self.denominator,
            "numerator": self.numerator,
            "population_identity": self.population_identity,
        }


@dataclasses.dataclass(frozen=True)
class ComplianceEvidenceV1:
    population_identity: str
    status: str
    numerator: int
    denominator: int

    def validate(self) -> None:
        _validate_population_identity(self.population_identity, "population_identity")
        if self.status not in COMPLIANCE_STATUSES:
            raise AuditCodecError("compliance status is not accepted")
        _validate_u64(self.numerator, "compliance.numerator")
        _validate_u64(self.denominator, "compliance.denominator")
        if self.numerator > self.denominator:
            raise AuditCodecError("compliance numerator exceeds denominator")
        if self.status == COMPLIANCE_PASS and (
            self.denominator == 0 or self.numerator != self.denominator
        ):
            raise AuditCodecError("PASS compliance requires a complete proven denominator")
        if self.status == COMPLIANCE_FAIL and (
            self.denominator == 0 or self.numerator == self.denominator
        ):
            raise AuditCodecError("FAIL compliance requires a proven failing member")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "denominator": self.denominator,
            "numerator": self.numerator,
            "population_identity": self.population_identity,
            "status": self.status,
        }


@dataclasses.dataclass(frozen=True)
class SliceComparisonV1:
    slice_kind: str
    coordinates: tuple[str, ...]
    teacher_status: str
    teacher_count: int
    bc_status: str
    bc_count: int

    def validate(self) -> None:
        if self.slice_kind not in task5_offline.SLICE_KIND_ORDER:
            raise AuditCodecError("slice comparison kind is not accepted")
        if not isinstance(self.coordinates, tuple) or len(self.coordinates) != len(
            task5_offline.SLICE_DIMENSION_ORDER
        ):
            raise AuditCodecError("slice comparison coordinates are not complete")
        for coordinate in self.coordinates:
            _validate_text(coordinate, "slice comparison coordinate")
        active_dimensions = {
            "decision_request_family": {"decision_request_family"},
            "candidate_domain_size": {"candidate_domain_size"},
            "candidate_domain_witness": {"candidate_domain_size"},
            "phase_decision_context": {"phase", "turn_index"},
            "acting_participant_deck_role": {"acting_participant", "locked_deck_role"},
            "starting_player": {"starting_player"},
            "continuation": {"continuation"},
            "rare_critical": {"rare_critical"},
        }[self.slice_kind]
        coordinate_by_dimension = dict(
            zip(task5_offline.SLICE_DIMENSION_ORDER, self.coordinates)
        )
        for dimension, coordinate in coordinate_by_dimension.items():
            if dimension not in active_dimensions and coordinate != task5_offline.SLICE_COORDINATE_ABSENT:
                raise AuditCodecError("slice comparison contains an inactive coordinate")
        if self.slice_kind == "candidate_domain_witness" and coordinate_by_dimension["candidate_domain_size"] not in {"24", "25", "129"}:
            raise AuditCodecError("slice comparison witness is not accepted")
        if self.teacher_status not in (SLICE_PRESENT, SLICE_NOT_PRESENT):
            raise AuditCodecError("Teacher slice status is not accepted")
        if self.bc_status not in (SLICE_PRESENT, SLICE_NOT_PRESENT):
            raise AuditCodecError("BC slice status is not accepted")
        _validate_u64(self.teacher_count, "teacher slice count")
        _validate_u64(self.bc_count, "BC slice count")
        if self.teacher_status == SLICE_NOT_PRESENT and self.teacher_count != 0:
            raise AuditCodecError("NOT_PRESENT Teacher slice has members")
        if self.bc_status == SLICE_NOT_PRESENT and self.bc_count != 0:
            raise AuditCodecError("NOT_PRESENT BC slice has members")
        if self.teacher_status == SLICE_PRESENT and self.teacher_count == 0:
            raise AuditCodecError("PRESENT Teacher slice has no members")
        if self.bc_status == SLICE_PRESENT and self.bc_count == 0:
            raise AuditCodecError("PRESENT BC slice has no members")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "bc_count": self.bc_count,
            "bc_status": self.bc_status,
            "coordinates": list(self.coordinates),
            "slice_kind": self.slice_kind,
            "teacher_count": self.teacher_count,
            "teacher_status": self.teacher_status,
        }


@dataclasses.dataclass(frozen=True)
class PopulationProfileV1:
    population_identity: str
    population_label: str
    decision_count: int
    decision_request_family_counts: tuple[tuple[str, int], ...]
    candidate_domain_size_counts: tuple[tuple[str, int], ...]
    capacity_compliance_rate: ComplianceEvidenceV1
    padding_compliance_rate: ComplianceEvidenceV1
    inference_failure_rate: RateV1
    quarantine_rate: RateV1
    replay_rate: RateV1
    admission_rate: RateV1
    teacher_agreement_rate: Optional[RateV1]
    terminal_outcome_counts: tuple[tuple[str, int], ...]
    interruption_count: int
    failure_count: int
    slice_counts: tuple[tuple[str, int], ...]

    def validate(self) -> None:
        _validate_population_identity(self.population_identity, "population_identity")
        if self.population_label not in ("TEACHER_STATE", "BC_INDUCED"):
            raise AuditCodecError("population label is not accepted")
        _validate_u64(self.decision_count, "decision_count")
        for name, values in (
            ("decision_request_family_counts", self.decision_request_family_counts),
            ("candidate_domain_size_counts", self.candidate_domain_size_counts),
            ("terminal_outcome_counts", self.terminal_outcome_counts),
            ("slice_counts", self.slice_counts),
        ):
            if not isinstance(values, tuple):
                raise AuditCodecError(f"{name} is not an ordered vector")
            previous: Optional[bytes] = None
            for key, count in values:
                _validate_text(key, f"{name}.key")
                _validate_u64(count, f"{name}.count")
                encoded = key.encode("utf-8")
                if previous is not None and encoded <= previous:
                    raise AuditCodecError(f"{name} is not canonically ordered")
                previous = encoded
        if sum(count for _, count in self.decision_request_family_counts) != self.decision_count:
            raise AuditCodecError("decision-family counts do not conserve population decisions")
        if sum(count for _, count in self.candidate_domain_size_counts) != self.decision_count:
            raise AuditCodecError("domain-size counts do not conserve population decisions")
        for compliance in (
            self.capacity_compliance_rate,
            self.padding_compliance_rate,
        ):
            compliance.validate()
            if compliance.population_identity != self.population_identity:
                raise AuditCodecError("compliance population identity differs")
        for rate in (
            self.inference_failure_rate,
            self.quarantine_rate,
            self.replay_rate,
            self.admission_rate,
        ):
            rate.validate()
            if rate.population_identity != self.population_identity:
                raise AuditCodecError("profile rate population identity differs")
        if self.teacher_agreement_rate is not None:
            self.teacher_agreement_rate.validate()
            if self.teacher_agreement_rate.population_identity != self.population_identity:
                raise AuditCodecError("agreement rate population identity differs")
        _validate_u64(self.interruption_count, "interruption_count")
        _validate_u64(self.failure_count, "failure_count")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "admission_rate": self.admission_rate.to_dict(),
            "candidate_domain_size_counts": [
                {"count": count, "value": value}
                for value, count in self.candidate_domain_size_counts
            ],
            "capacity_compliance_rate": self.capacity_compliance_rate.to_dict(),
            "decision_count": self.decision_count,
            "decision_request_family_counts": [
                {"count": count, "value": value}
                for value, count in self.decision_request_family_counts
            ],
            "failure_count": self.failure_count,
            "inference_failure_rate": self.inference_failure_rate.to_dict(),
            "interruption_count": self.interruption_count,
            "padding_compliance_rate": self.padding_compliance_rate.to_dict(),
            "population_identity": self.population_identity,
            "population_label": self.population_label,
            "quarantine_rate": self.quarantine_rate.to_dict(),
            "replay_rate": self.replay_rate.to_dict(),
            "slice_counts": [
                {"count": count, "value": value} for value, count in self.slice_counts
            ],
            "teacher_agreement_rate": (
                None
                if self.teacher_agreement_rate is None
                else self.teacher_agreement_rate.to_dict()
            ),
            "terminal_outcome_counts": [
                {"count": count, "value": value}
                for value, count in self.terminal_outcome_counts
            ],
        }


@dataclasses.dataclass(frozen=True)
class DistributionShiftV1:
    evaluation_identity: str
    evaluation_contract_identity: str
    teacher_state_population_identity: str
    bc_induced_population_identity: str
    teacher_profile: PopulationProfileV1
    bc_profile: PopulationProfileV1
    slice_comparisons: tuple[SliceComparisonV1, ...]
    schema_id: str = DISTRIBUTION_SHIFT_SCHEMA_ID
    diagnostic_only_tokens: tuple[str, ...] = DIAGNOSTIC_ONLY_TOKENS
    declared_identity: str = dataclasses.field(default="", compare=False)

    @property
    def identity(self) -> str:
        return _identity(DISTRIBUTION_SHIFT_ID_PREFIX, self._canonical_bytes())

    def validate(self) -> None:
        if self.schema_id != DISTRIBUTION_SHIFT_SCHEMA_ID:
            raise AuditCodecError("distribution-shift schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_contract_identity, task5.EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        if self.evaluation_contract_identity != task5.evaluation_contract_identity():
            raise AuditCodecError("distribution-shift contract identity is not accepted")
        _validate_prefixed(self.teacher_state_population_identity, task5_offline.TEACHER_STATE_POPULATION_ID_PREFIX, "teacher_state_population_identity")
        _validate_prefixed(self.bc_induced_population_identity, BC_INDUCED_POPULATION_ID_PREFIX, "bc_induced_population_identity")
        if self.teacher_state_population_identity == self.bc_induced_population_identity:
            raise AuditCodecError("Teacher and BC populations must remain separate")
        if self.diagnostic_only_tokens != DIAGNOSTIC_ONLY_TOKENS:
            raise AuditCodecError("distribution-shift diagnostic token order differs")
        self.teacher_profile.validate()
        self.bc_profile.validate()
        if self.teacher_profile.population_identity != self.teacher_state_population_identity or self.teacher_profile.population_label != "TEACHER_STATE":
            raise AuditCodecError("Teacher distribution profile is not bound")
        if self.bc_profile.population_identity != self.bc_induced_population_identity or self.bc_profile.population_label != "BC_INDUCED":
            raise AuditCodecError("BC distribution profile is not bound")
        if not isinstance(self.slice_comparisons, tuple) or not self.slice_comparisons:
            raise AuditCodecError("slice comparisons are required")
        if {value.slice_kind for value in self.slice_comparisons} != set(
            task5_offline.SLICE_KIND_ORDER
        ):
            raise AuditCodecError("slice comparisons do not cover every required slice kind")
        previous: Optional[tuple[int, tuple[bytes, ...]]] = None
        seen: set[tuple[str, tuple[str, ...]]] = set()
        for comparison in self.slice_comparisons:
            comparison.validate()
            key = (comparison.slice_kind, comparison.coordinates)
            if key in seen:
                raise AuditCodecError("slice comparisons contain duplicates")
            seen.add(key)
            order = (
                task5_offline.SLICE_KIND_ORDER.index(comparison.slice_kind),
                tuple(value.encode("utf-8") for value in comparison.coordinates),
            )
            if previous is not None and order < previous:
                raise AuditCodecError("slice comparisons are not in fixed order")
            previous = order
        if self.declared_identity and self.declared_identity != self.identity:
            raise AuditCodecError("distribution-shift identity does not recompute")

    def _payload(self) -> dict[str, Any]:
        return {
            "bc_induced_population_identity": self.bc_induced_population_identity,
            "bc_profile": self.bc_profile.to_dict(),
            "diagnostic_only_tokens": list(self.diagnostic_only_tokens),
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_identity": self.evaluation_identity,
            "identity_domain": DISTRIBUTION_SHIFT_IDENTITY_DOMAIN,
            "identity_schema": DISTRIBUTION_SHIFT_SCHEMA_ID,
            "schema_id": self.schema_id,
            "slice_comparisons": [
                value.to_dict() for value in self.slice_comparisons
            ],
            "teacher_profile": self.teacher_profile.to_dict(),
            "teacher_state_population_identity": self.teacher_state_population_identity,
        }

    def _canonical_bytes(self) -> bytes:
        return task5.canonical_json_bytes(self._payload())

    def to_dict(self) -> dict[str, Any]:
        payload = self._payload()
        payload["distribution_shift_identity"] = self.identity
        return payload


def _sorted_counts(values: Iterable[str]) -> tuple[tuple[str, int], ...]:
    counts = Counter(values)
    return tuple(sorted(counts.items(), key=lambda item: item[0].encode("utf-8")))


def _frame_slice_items(
    frame: PublicDecisionFrameV1,
) -> tuple[tuple[str, tuple[str, ...]], ...]:
    absent = task5_offline.SLICE_COORDINATE_ABSENT
    coordinates = (
        frame.decision_request_family,
        str(frame.candidate_count),
        absent if frame.phase is None else str(frame.phase),
        absent if frame.turn_index is None else str(frame.turn_index),
        absent if frame.acting_participant is None else str(frame.acting_participant),
        frame.locked_deck_role_id or absent,
        absent if frame.starting_player is None else str(frame.starting_player),
        "continuation"
        if frame.continuation_context.is_continuation
        else "non_continuation",
        frame.rare_critical_slice or absent,
    )
    active = {
        "decision_request_family": {0},
        "candidate_domain_size": {1},
        "candidate_domain_witness": {1},
        "phase_decision_context": {2, 3},
        "acting_participant_deck_role": {4, 5},
        "starting_player": {6},
        "continuation": {7},
        "rare_critical": {8},
    }
    labels = []
    for kind, indices in active.items():
        if kind == "candidate_domain_witness" and frame.candidate_count not in (24, 25, 129):
            continue
        projected = tuple(
            value if index in indices else absent
            for index, value in enumerate(coordinates)
        )
        labels.append((kind, projected))
    return tuple(labels)


def _frame_slice_labels(frame: PublicDecisionFrameV1) -> tuple[str, ...]:
    return tuple(
        f"{kind}|{'/'.join(coordinates)}"
        for kind, coordinates in _frame_slice_items(frame)
    )


def _rate(population_identity: str, numerator: int, denominator: int) -> RateV1:
    rate = RateV1(population_identity, numerator, denominator)
    rate.validate()
    return rate


def _compliance(
    population_identity: str,
    proven_pass_count: int,
    proven_fail_count: int,
    unproven_count: int,
) -> ComplianceEvidenceV1:
    denominator = proven_pass_count + proven_fail_count
    if proven_fail_count:
        status = COMPLIANCE_FAIL
    elif unproven_count:
        status = COMPLIANCE_NOT_RUN_UNPROVEN
    elif proven_pass_count:
        status = COMPLIANCE_PASS
    else:
        status = COMPLIANCE_NOT_RUN_UNPROVEN
    result = ComplianceEvidenceV1(
        population_identity=population_identity,
        status=status,
        numerator=proven_pass_count,
        denominator=denominator,
    )
    result.validate()
    return result


def _offline_profile(
    result: task5_offline.OfflineEvaluationResultV1,
) -> PopulationProfileV1:
    result.validate()
    population_identity = result.teacher_state_population_identity
    samples = tuple(result.sample_results)
    family = _sorted_counts(
        sample.decision_request_family or task5_offline.SLICE_COORDINATE_ABSENT
        for sample in samples
    )
    sizes = _sorted_counts(
        str(sample.candidate_count)
        if sample.candidate_count is not None
        else task5_offline.SLICE_COORDINATE_ABSENT
        for sample in samples
    )
    capacity_proven = sum(
        sample.candidate_count is not None
        and sample.failure_reason != task5_offline.FailureReason.CANDIDATE_CAPACITY_FAILURE
        for sample in samples
    )
    capacity_failures = sum(
        sample.failure_reason == task5_offline.FailureReason.CANDIDATE_CAPACITY_FAILURE
        for sample in samples
    )
    padding_failures = sum(
        sample.failure_reason == task5_offline.FailureReason.PADDING_MASK_VIOLATION
        for sample in samples
    )
    agreement_values = [sample.top1_agreement for sample in samples if sample.top1_agreement is not None]
    slice_values: list[str] = []
    for slice_result in result.slice_results:
        slice_values.extend(
            f"{slice_result.slice_kind}|{'/'.join(slice_result.coordinates)}"
            for _ in range(slice_result.total_count)
        )
    slice_counts = _sorted_counts(slice_values)
    profile = PopulationProfileV1(
        population_identity=population_identity,
        population_label="TEACHER_STATE",
        decision_count=len(samples),
        decision_request_family_counts=family,
        candidate_domain_size_counts=sizes,
        capacity_compliance_rate=_compliance(
            population_identity,
            capacity_proven,
            capacity_failures,
            sum(sample.candidate_count is None for sample in samples),
        ),
        padding_compliance_rate=_compliance(
            population_identity,
            0,
            padding_failures,
            len(samples) - padding_failures,
        ),
        inference_failure_rate=_rate(
            population_identity,
            sum(sample.failure_reason == task5_offline.FailureReason.INFERENCE_FAILURE for sample in samples),
            len(samples),
        ),
        quarantine_rate=_rate(population_identity, 0, len(samples)),
        replay_rate=_rate(population_identity, 0, 0),
        admission_rate=_rate(population_identity, 0, 0),
        teacher_agreement_rate=(
            _rate(population_identity, sum(bool(value) for value in agreement_values), len(agreement_values))
            if agreement_values
            else None
        ),
        terminal_outcome_counts=(),
        interruption_count=0,
        failure_count=sum(sample.status != task5_offline.STATUS_SCORED for sample in samples),
        slice_counts=slice_counts,
    )
    profile.validate()
    return profile


def _bc_profile(population: BCInducedPopulationV1) -> PopulationProfileV1:
    population.validate()
    identity = population.identity
    decisions = tuple(
        decision
        for job in population.shared_jobs
        for decision in job.decisions
    )
    frames = [decision.frame for decision in decisions]
    for job in population.shared_jobs:
        if job.failure is not None and job.failure.frame is not None:
            frames.append(job.failure.frame)
    family = _sorted_counts(frame.decision_request_family for frame in frames)
    sizes = _sorted_counts(str(frame.candidate_count) for frame in frames)
    job_count = len(population.gameplay_job_results)
    agreement_values = [
        decision.teacher_selected_public_action_key == decision.model_selected_public_action_key
        for decision in decisions
        if decision.teacher_selected_public_action_key is not None
        and decision.model_selected_public_action_key is not None
    ]
    terminal_counts = _sorted_counts(
        result.status
        for result in population.gameplay_job_results
        if result.status in ("TRUSTED_WIN", "TRUSTED_LOSS", "TRUSTED_DRAW")
    )
    slice_values = [label for frame in frames for label in _frame_slice_labels(frame)]
    profile = PopulationProfileV1(
        population_identity=identity,
        population_label="BC_INDUCED",
        decision_count=len(frames),
        decision_request_family_counts=family,
        candidate_domain_size_counts=sizes,
        capacity_compliance_rate=_compliance(
            identity,
            len(decisions),
            0,
            len(frames) - len(decisions),
        ),
        padding_compliance_rate=_compliance(identity, 0, 0, len(frames)),
        inference_failure_rate=_rate(
            identity,
            sum(result.failure_stage == "inference" for result in population.gameplay_job_results),
            job_count,
        ),
        quarantine_rate=_rate(
            identity,
            sum(result.status == "QUARANTINED" for result in population.gameplay_job_results),
            job_count,
        ),
        replay_rate=_rate(
            identity,
            sum(replay.replay_status == "PASS" for replay in population.replay_admission_summaries),
            job_count,
        ),
        admission_rate=_rate(
            identity,
            sum(replay.admission_status == "PASS" for replay in population.replay_admission_summaries),
            job_count,
        ),
        teacher_agreement_rate=(
            _rate(identity, sum(agreement_values), len(agreement_values))
            if agreement_values
            else None
        ),
        terminal_outcome_counts=terminal_counts,
        interruption_count=sum(result.status == "INTERRUPTED" for result in population.gameplay_job_results),
        failure_count=sum(result.status == "FAILED" for result in population.gameplay_job_results),
        slice_counts=_sorted_counts(slice_values),
    )
    profile.validate()
    return profile


def _derive_slice_comparisons(
    offline_result: task5_offline.OfflineEvaluationResultV1,
    bc_population: BCInducedPopulationV1,
) -> tuple[SliceComparisonV1, ...]:
    teacher: dict[tuple[str, tuple[str, ...]], tuple[str, int]] = {}
    for slice_result in offline_result.slice_results:
        key = (slice_result.slice_kind, slice_result.coordinates)
        if key in teacher:
            raise AuditValidationError("offline evidence contains duplicate slice definitions")
        teacher[key] = (slice_result.presence, slice_result.total_count)
    missing_kinds = set(task5_offline.SLICE_KIND_ORDER) - {
        kind for kind, _ in teacher
    }
    if missing_kinds:
        raise AuditValidationError(
            "offline evidence is missing required slice kinds: " + ",".join(sorted(missing_kinds))
        )

    bc_counts: Counter[tuple[str, tuple[str, ...]]] = Counter()
    for job in bc_population.shared_jobs:
        frames = [decision.frame for decision in job.decisions]
        if job.failure is not None and job.failure.frame is not None:
            frames.append(job.failure.frame)
        for frame in frames:
            bc_counts.update(_frame_slice_items(frame))

    keys = set(teacher) | set(bc_counts)
    ordered_keys = sorted(
        keys,
        key=lambda key: (
            task5_offline.SLICE_KIND_ORDER.index(key[0]),
            tuple(value.encode("utf-8") for value in key[1]),
        ),
    )
    comparisons = []
    for key in ordered_keys:
        teacher_status, teacher_count = teacher.get(key, (SLICE_NOT_PRESENT, 0))
        bc_count = bc_counts.get(key, 0)
        bc_status = SLICE_PRESENT if bc_count else SLICE_NOT_PRESENT
        comparisons.append(
            SliceComparisonV1(
                slice_kind=key[0],
                coordinates=key[1],
                teacher_status=teacher_status,
                teacher_count=teacher_count,
                bc_status=bc_status,
                bc_count=bc_count,
            )
        )
    for comparison in comparisons:
        comparison.validate()
    return tuple(comparisons)


def derive_distribution_shift(
    offline_result: task5_offline.OfflineEvaluationResultV1,
    bc_population: BCInducedPopulationV1,
    context: task5.EvaluationContextV1,
) -> DistributionShiftV1:
    context.validate()
    offline_result.validate()
    bc_population.validate(context)
    evaluation_identity = task5.evaluation_identity(context.root)
    if offline_result.evaluation_identity != evaluation_identity:
        raise AuditValidationError("offline evidence evaluation identity differs")
    teacher_profile = _offline_profile(offline_result)
    bc_profile = _bc_profile(bc_population)
    slice_comparisons = _derive_slice_comparisons(offline_result, bc_population)
    result = DistributionShiftV1(
        evaluation_identity=evaluation_identity,
        evaluation_contract_identity=context.root.evaluation_contract_identity,
        teacher_state_population_identity=offline_result.teacher_state_population_identity,
        bc_induced_population_identity=bc_population.identity,
        teacher_profile=teacher_profile,
        bc_profile=bc_profile,
        slice_comparisons=slice_comparisons,
    )
    result.validate()
    return result


def _rate_from_dict(payload: Any, label: str) -> RateV1:
    data = _strict_object(payload, ("denominator", "numerator", "population_identity"), label)
    result = RateV1(data["population_identity"], data["numerator"], data["denominator"])
    result.validate()
    return result


def _compliance_from_dict(payload: Any, label: str) -> ComplianceEvidenceV1:
    data = _strict_object(
        payload,
        ("denominator", "numerator", "population_identity", "status"),
        label,
    )
    result = ComplianceEvidenceV1(
        population_identity=data["population_identity"],
        status=data["status"],
        numerator=data["numerator"],
        denominator=data["denominator"],
    )
    result.validate()
    return result


def _count_vector_from_dict(payload: Any, label: str) -> tuple[tuple[str, int], ...]:
    if not isinstance(payload, list):
        raise AuditCodecError(f"{label} is not a vector")
    values = []
    for item in payload:
        data = _strict_object(item, ("count", "value"), label + " entry")
        values.append((data["value"], data["count"]))
    return tuple(values)


def _profile_from_dict(payload: Any) -> PopulationProfileV1:
    fields = (
        "admission_rate", "candidate_domain_size_counts", "capacity_compliance_rate",
        "decision_count", "decision_request_family_counts", "failure_count",
        "inference_failure_rate", "interruption_count", "padding_compliance_rate",
        "population_identity", "population_label", "quarantine_rate", "replay_rate",
        "slice_counts", "teacher_agreement_rate", "terminal_outcome_counts",
    )
    data = _strict_object(payload, fields, "PopulationProfileV1")
    result = PopulationProfileV1(
        population_identity=data["population_identity"],
        population_label=data["population_label"],
        decision_count=data["decision_count"],
        decision_request_family_counts=_count_vector_from_dict(data["decision_request_family_counts"], "decision families"),
        candidate_domain_size_counts=_count_vector_from_dict(data["candidate_domain_size_counts"], "domain sizes"),
        capacity_compliance_rate=_compliance_from_dict(data["capacity_compliance_rate"], "capacity rate"),
        padding_compliance_rate=_compliance_from_dict(data["padding_compliance_rate"], "padding rate"),
        inference_failure_rate=_rate_from_dict(data["inference_failure_rate"], "inference rate"),
        quarantine_rate=_rate_from_dict(data["quarantine_rate"], "quarantine rate"),
        replay_rate=_rate_from_dict(data["replay_rate"], "replay rate"),
        admission_rate=_rate_from_dict(data["admission_rate"], "admission rate"),
        teacher_agreement_rate=(
            None if data["teacher_agreement_rate"] is None
            else _rate_from_dict(data["teacher_agreement_rate"], "agreement rate")
        ),
        terminal_outcome_counts=_count_vector_from_dict(data["terminal_outcome_counts"], "terminal outcomes"),
        interruption_count=data["interruption_count"],
        failure_count=data["failure_count"],
        slice_counts=_count_vector_from_dict(data["slice_counts"], "slice counts"),
    )
    result.validate()
    return result


def _slice_comparison_from_dict(payload: Any) -> SliceComparisonV1:
    data = _strict_object(
        payload,
        ("bc_count", "bc_status", "coordinates", "slice_kind", "teacher_count", "teacher_status"),
        "SliceComparisonV1",
    )
    result = SliceComparisonV1(
        slice_kind=data["slice_kind"],
        coordinates=tuple(data["coordinates"]),
        teacher_status=data["teacher_status"],
        teacher_count=data["teacher_count"],
        bc_status=data["bc_status"],
        bc_count=data["bc_count"],
    )
    result.validate()
    return result


def encode_distribution_shift_json(value: DistributionShiftV1) -> bytes:
    return task5.canonical_json_bytes(value.to_dict())


def decode_distribution_shift_json(data: bytes) -> DistributionShiftV1:
    try:
        payload = task5.parse_canonical_json(data)
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    fields = (
        "bc_induced_population_identity", "bc_profile", "diagnostic_only_tokens",
        "distribution_shift_identity", "evaluation_contract_identity",
        "evaluation_identity", "identity_domain", "identity_schema", "schema_id", "teacher_profile",
        "teacher_state_population_identity", "slice_comparisons",
    )
    values = _strict_object(payload, fields, "DistributionShiftV1")
    result = DistributionShiftV1(
        evaluation_identity=values["evaluation_identity"],
        evaluation_contract_identity=values["evaluation_contract_identity"],
        teacher_state_population_identity=values["teacher_state_population_identity"],
        bc_induced_population_identity=values["bc_induced_population_identity"],
        teacher_profile=_profile_from_dict(values["teacher_profile"]),
        bc_profile=_profile_from_dict(values["bc_profile"]),
        slice_comparisons=tuple(
            _slice_comparison_from_dict(item) for item in values["slice_comparisons"]
        ),
        schema_id=values["schema_id"],
        diagnostic_only_tokens=tuple(values["diagnostic_only_tokens"]),
        declared_identity=values["distribution_shift_identity"],
    )
    if values["identity_domain"] != DISTRIBUTION_SHIFT_IDENTITY_DOMAIN or values["identity_schema"] != DISTRIBUTION_SHIFT_SCHEMA_ID:
        raise AuditCodecError("distribution-shift identity domain/schema is not accepted")
    result.validate()
    return result


# ---------------------------------------------------------------------------
# Aggregate read model, evaluation summary, and deterministic report


@dataclasses.dataclass(frozen=True)
class EvaluationSummaryV1:
    evaluation_identity: str
    evaluation_contract_identity: str
    evaluation_corpus_identity: str
    evaluation_job_manifest_identity: str
    teacher_state_population_identity: str
    bc_induced_population_identity: str
    offline_metrics_identity: str
    offline_slice_identities: tuple[str, ...]
    offline_sample_count: int
    gameplay_summary_identity: str
    gameplay_job_result_identities: tuple[str, ...]
    replay_admission_summary_identities: tuple[str, ...]
    first_divergence_identities: tuple[str, ...]
    distribution_shift_identity: str
    p6_g15_status: str = P6_G15_STATUS
    p6_g14_status: str = P6_G14_STATUS
    schema_id: str = EVALUATION_SUMMARY_SCHEMA_ID
    declared_identity: str = dataclasses.field(default="", compare=False)

    @property
    def identity(self) -> str:
        return _identity(EVALUATION_SUMMARY_ID_PREFIX, self._canonical_bytes())

    def validate(self) -> None:
        if self.schema_id != EVALUATION_SUMMARY_SCHEMA_ID:
            raise AuditCodecError("evaluation summary schema is not accepted")
        for value, prefix, field in (
            (self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity"),
            (self.evaluation_contract_identity, task5.EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity"),
            (self.evaluation_corpus_identity, task5.EVALUATION_CORPUS_IDENTITY_PREFIX, "evaluation_corpus_identity"),
            (self.evaluation_job_manifest_identity, task5.JOB_MANIFEST_ID_PREFIX, "evaluation_job_manifest_identity"),
            (self.teacher_state_population_identity, task5_offline.TEACHER_STATE_POPULATION_ID_PREFIX, "teacher_state_population_identity"),
            (self.bc_induced_population_identity, BC_INDUCED_POPULATION_ID_PREFIX, "bc_induced_population_identity"),
            (self.offline_metrics_identity, task5_offline.OFFLINE_METRICS_ID_PREFIX, "offline_metrics_identity"),
            (self.gameplay_summary_identity, "phase6_gameplay_summary.v1.", "gameplay_summary_identity"),
            (self.distribution_shift_identity, DISTRIBUTION_SHIFT_ID_PREFIX, "distribution_shift_identity"),
        ):
            _validate_prefixed(value, prefix, field)
        if self.evaluation_contract_identity != task5.evaluation_contract_identity():
            raise AuditCodecError("evaluation-summary contract identity is not accepted")
        if self.teacher_state_population_identity == self.bc_induced_population_identity:
            raise AuditCodecError("summary populations are merged")
        for values, prefix, field in (
            (self.offline_slice_identities, task5_offline.OFFLINE_SLICE_ID_PREFIX, "offline_slice_identity"),
            (self.gameplay_job_result_identities, "phase6_gameplay_job_result.v1.", "gameplay_job_result_identity"),
            (self.replay_admission_summary_identities, "phase6_replay_admission_summary.v1.", "replay_admission_summary_identity"),
            (self.first_divergence_identities, FIRST_DIVERGENCE_ID_PREFIX, "first_divergence_identity"),
        ):
            if not isinstance(values, tuple):
                raise AuditCodecError(f"{field} vector is not ordered")
            for value in values:
                _validate_prefixed(value, prefix, field)
            if len(set(values)) != len(values):
                raise AuditCodecError(f"{field} vector contains duplicates")
        _validate_u64(self.offline_sample_count, "offline_sample_count")
        if self.p6_g14_status != P6_G14_STATUS or self.p6_g15_status != P6_G15_STATUS:
            raise AuditCodecError("evaluation summary gate status is not derived/accepted")
        if self.declared_identity and self.declared_identity != self.identity:
            raise AuditCodecError("evaluation summary identity does not recompute")

    def _payload(self) -> dict[str, Any]:
        return {
            "bc_induced_population_identity": self.bc_induced_population_identity,
            "distribution_shift_identity": self.distribution_shift_identity,
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_corpus_identity": self.evaluation_corpus_identity,
            "evaluation_identity": self.evaluation_identity,
            "evaluation_job_manifest_identity": self.evaluation_job_manifest_identity,
            "first_divergence_identities": list(self.first_divergence_identities),
            "gameplay_job_result_identities": list(self.gameplay_job_result_identities),
            "gameplay_summary_identity": self.gameplay_summary_identity,
            "identity_domain": EVALUATION_SUMMARY_IDENTITY_DOMAIN,
            "identity_schema": EVALUATION_SUMMARY_SCHEMA_ID,
            "offline_metrics_identity": self.offline_metrics_identity,
            "offline_sample_count": self.offline_sample_count,
            "offline_slice_identities": list(self.offline_slice_identities),
            "p6_g14_status": self.p6_g14_status,
            "p6_g15_status": self.p6_g15_status,
            "replay_admission_summary_identities": list(self.replay_admission_summary_identities),
            "schema_id": self.schema_id,
            "teacher_state_population_identity": self.teacher_state_population_identity,
        }

    def _canonical_bytes(self) -> bytes:
        return task5.canonical_json_bytes(self._payload())

    def to_dict(self) -> dict[str, Any]:
        payload = self._payload()
        payload["evaluation_summary_identity"] = self.identity
        return payload


def encode_evaluation_summary_json(value: EvaluationSummaryV1) -> bytes:
    return task5.canonical_json_bytes(value.to_dict())


def decode_evaluation_summary_json(data: bytes) -> EvaluationSummaryV1:
    try:
        payload = task5.parse_canonical_json(data)
    except task5.CodecError as error:
        raise AuditCodecError(str(error)) from error
    fields = (
        "bc_induced_population_identity", "distribution_shift_identity",
        "evaluation_contract_identity", "evaluation_corpus_identity",
        "evaluation_identity", "evaluation_job_manifest_identity",
        "evaluation_summary_identity", "first_divergence_identities",
        "gameplay_job_result_identities", "gameplay_summary_identity",
        "identity_domain", "identity_schema",
        "offline_metrics_identity", "offline_sample_count",
        "offline_slice_identities", "p6_g14_status", "p6_g15_status",
        "replay_admission_summary_identities", "schema_id",
        "teacher_state_population_identity",
    )
    values = _strict_object(payload, fields, "EvaluationSummaryV1")
    result = EvaluationSummaryV1(
        evaluation_identity=values["evaluation_identity"],
        evaluation_contract_identity=values["evaluation_contract_identity"],
        evaluation_corpus_identity=values["evaluation_corpus_identity"],
        evaluation_job_manifest_identity=values["evaluation_job_manifest_identity"],
        teacher_state_population_identity=values["teacher_state_population_identity"],
        bc_induced_population_identity=values["bc_induced_population_identity"],
        offline_metrics_identity=values["offline_metrics_identity"],
        offline_slice_identities=tuple(values["offline_slice_identities"]),
        offline_sample_count=values["offline_sample_count"],
        gameplay_summary_identity=values["gameplay_summary_identity"],
        gameplay_job_result_identities=tuple(values["gameplay_job_result_identities"]),
        replay_admission_summary_identities=tuple(values["replay_admission_summary_identities"]),
        first_divergence_identities=tuple(values["first_divergence_identities"]),
        distribution_shift_identity=values["distribution_shift_identity"],
        p6_g15_status=values["p6_g15_status"],
        p6_g14_status=values["p6_g14_status"],
        schema_id=values["schema_id"],
        declared_identity=values["evaluation_summary_identity"],
    )
    if values["identity_domain"] != EVALUATION_SUMMARY_IDENTITY_DOMAIN or values["identity_schema"] != EVALUATION_SUMMARY_SCHEMA_ID:
        raise AuditCodecError("evaluation-summary identity domain/schema is not accepted")
    result.validate()
    return result


@dataclasses.dataclass(frozen=True)
class EvaluationReadModelV1:
    context: task5.EvaluationContextV1
    offline_evaluation: task5_offline.OfflineEvaluationResultV1
    bc_population: BCInducedPopulationV1
    first_divergences: tuple[task5.FirstDivergenceV1, ...]
    distribution_shift: DistributionShiftV1
    summary: EvaluationSummaryV1

    def validate(self) -> None:
        self.context.validate()
        self.offline_evaluation.validate()
        self.bc_population.validate(self.context)
        expected_identity = task5.evaluation_identity(self.context.root)
        if self.offline_evaluation.evaluation_identity != expected_identity:
            raise AuditValidationError("offline evidence is not bound to evaluation root")
        expected_started = tuple(
            result.evaluation_job_identity
            for result in self.bc_population.gameplay_job_results
            if result.started
        )
        if tuple(record.evaluation_job_identity for record in self.first_divergences) != expected_started:
            raise AuditValidationError("FirstDivergence vector is not the exact started-job vector")
        for job, record in zip(
            (job for job in self.bc_population.shared_jobs if job.started),
            self.first_divergences,
        ):
            if record != derive_first_divergence(job):
                raise AuditValidationError("FirstDivergence record is not derived from shared evidence")
        expected_shift = derive_distribution_shift(
            self.offline_evaluation, self.bc_population, self.context
        )
        if self.distribution_shift != expected_shift:
            raise AuditValidationError("distribution shift is not derived from child evidence")
        expected_gameplay = derive_gameplay_summary(
            self.context,
            self.bc_population.gameplay_job_results,
            self.bc_population.replay_admission_summaries,
        )
        expected_summary = EvaluationSummaryV1(
            evaluation_identity=expected_identity,
            evaluation_contract_identity=self.context.root.evaluation_contract_identity,
            evaluation_corpus_identity=self.context.root.evaluation_corpus_identity,
            evaluation_job_manifest_identity=task5.evaluation_job_manifest_identity(
                self.context.job_manifest
            ),
            teacher_state_population_identity=self.offline_evaluation.teacher_state_population_identity,
            bc_induced_population_identity=self.bc_population.identity,
            offline_metrics_identity=self.offline_evaluation.metrics.offline_metrics_identity,
            offline_slice_identities=tuple(
                value.offline_slice_identity for value in self.offline_evaluation.slice_results
            ),
            offline_sample_count=len(self.offline_evaluation.sample_results),
            gameplay_summary_identity=expected_gameplay.identity,
            gameplay_job_result_identities=tuple(
                value.identity for value in self.bc_population.gameplay_job_results
            ),
            replay_admission_summary_identities=tuple(
                value.identity for value in self.bc_population.replay_admission_summaries
            ),
            first_divergence_identities=tuple(
                first_divergence_identity(value) for value in self.first_divergences
            ),
            distribution_shift_identity=self.distribution_shift.identity,
        )
        expected_summary.validate()
        if self.summary != expected_summary:
            raise AuditValidationError("evaluation summary is not derived from child artifacts")


def build_evaluation_read_model(
    context: task5.EvaluationContextV1,
    offline_evaluation: task5_offline.OfflineEvaluationResultV1,
    bc_population: BCInducedPopulationV1,
    first_divergences: Sequence[task5.FirstDivergenceV1],
) -> EvaluationReadModelV1:
    context.validate()
    offline_evaluation.validate()
    bc_population.validate(context)
    started_jobs = tuple(job for job in bc_population.shared_jobs if job.started)
    expected_records = tuple(derive_first_divergence(job) for job in started_jobs)
    if tuple(first_divergences) != expected_records:
        raise AuditValidationError("supplied FirstDivergence records are not child-derived")
    shift = derive_distribution_shift(offline_evaluation, bc_population, context)
    gameplay = derive_gameplay_summary(
        context,
        bc_population.gameplay_job_results,
        bc_population.replay_admission_summaries,
    )
    summary = EvaluationSummaryV1(
        evaluation_identity=task5.evaluation_identity(context.root),
        evaluation_contract_identity=context.root.evaluation_contract_identity,
        evaluation_corpus_identity=context.root.evaluation_corpus_identity,
        evaluation_job_manifest_identity=task5.evaluation_job_manifest_identity(
            context.job_manifest
        ),
        teacher_state_population_identity=offline_evaluation.teacher_state_population_identity,
        bc_induced_population_identity=bc_population.identity,
        offline_metrics_identity=offline_evaluation.metrics.offline_metrics_identity,
        offline_slice_identities=tuple(
            value.offline_slice_identity for value in offline_evaluation.slice_results
        ),
        offline_sample_count=len(offline_evaluation.sample_results),
        gameplay_summary_identity=gameplay.identity,
        gameplay_job_result_identities=tuple(
            value.identity for value in bc_population.gameplay_job_results
        ),
        replay_admission_summary_identities=tuple(
            value.identity for value in bc_population.replay_admission_summaries
        ),
        first_divergence_identities=tuple(first_divergence_identity(value) for value in expected_records),
        distribution_shift_identity=shift.identity,
    )
    model = EvaluationReadModelV1(
        context=context,
        offline_evaluation=offline_evaluation,
        bc_population=bc_population,
        first_divergences=expected_records,
        distribution_shift=shift,
        summary=summary,
    )
    model.validate()
    return model


def generate_report(model: EvaluationReadModelV1) -> str:
    """Derive one deterministic Markdown report from one validated read model."""

    model.validate()
    offline_metrics = model.offline_evaluation.metrics
    gameplay = derive_gameplay_summary(
        model.context,
        model.bc_population.gameplay_job_results,
        model.bc_population.replay_admission_summaries,
    )
    lines = [
        "# OCGForge Phase 6 Task5 Evaluation Report",
        "",
        f"- Report schema: `{REPORT_SCHEMA_ID}`",
        f"- Evaluation identity: `{model.summary.evaluation_identity}`",
        f"- Evaluation contract: `{model.summary.evaluation_contract_identity}`",
        f"- Evaluation corpus: `{model.summary.evaluation_corpus_identity}`",
        f"- Checkpoint: `{model.context.root.checkpoint_identity}`",
        "",
        "## Offline Teacher-state validation",
        "",
        f"- Population: `{model.summary.teacher_state_population_identity}`",
        f"- Total samples: {offline_metrics.total_count}",
        f"- Scored samples: {offline_metrics.scored_count}",
        f"- Rejected samples: {offline_metrics.rejected_count}",
        f"- Unscored samples: {offline_metrics.unscored_count}",
        f"- Loss denominator: {offline_metrics.loss_denominator}",
        f"- Top-1 denominator: {offline_metrics.top1_denominator}",
        "- Teacher agreement is diagnostic behavior-imitation evidence, not online parity.",
        "",
        "## Gameplay mechanics",
        "",
        f"- BC-induced population: `{model.summary.bc_induced_population_identity}`",
        f"- Scheduled jobs: {gameplay.scheduled_job_count}",
        f"- Started jobs: {gameplay.started_job_count}",
        f"- Trusted wins/losses/draws: {gameplay.trusted_win_count}/{gameplay.trusted_loss_count}/{gameplay.trusted_draw_count}",
        f"- Interrupted/failed/quarantined: {gameplay.interrupted_job_count}/{gameplay.failed_job_count}/{gameplay.quarantined_job_count}",
        f"- Replay/admission failures: {gameplay.replay_failure_count}/{gameplay.admission_failure_count}",
        f"- Inference failures: {gameplay.inference_failure_count}",
        f"- Fallback-assisted jobs: {gameplay.fallback_assisted_job_count}",
        "",
        "## First divergence",
        "",
        f"- Started-job records: {len(model.first_divergences)}",
        f"- FirstDivergence stream identity inputs: `{','.join(model.summary.first_divergence_identities)}`",
        "- Only the earliest divergence or explicit pre-divergence failure is retained per started job.",
        "",
        "## Distribution shift",
        "",
        f"- Teacher-state population: `{model.distribution_shift.teacher_state_population_identity}`",
        f"- BC-induced population: `{model.distribution_shift.bc_induced_population_identity}`",
        f"- Teacher decision families: {model.distribution_shift.teacher_profile.decision_request_family_counts}",
        f"- BC decision families: {model.distribution_shift.bc_profile.decision_request_family_counts}",
        f"- Teacher domain sizes: {model.distribution_shift.teacher_profile.candidate_domain_size_counts}",
        f"- BC domain sizes: {model.distribution_shift.bc_profile.candidate_domain_size_counts}",
        "- Teacher agreement on BC-induced states is diagnostic only; it is not strategic equality or safe generalization.",
        "",
        "## Phase-6 status",
        "",
        f"- P6-G15: `{model.summary.p6_g15_status}`",
        f"- P6-G14: `{model.summary.p6_g14_status}`",
        "- The smoke checkpoint does not establish strategic playability, convergence, or meaningful win-rate strength.",
        "",
    ]
    return "\n".join(lines)


def report_identity(report: str) -> str:
    if not isinstance(report, str):
        raise AuditCodecError("report is not text")
    if not report.endswith("\n") or "\r" in report:
        raise AuditCodecError("report is not LF-terminated canonical text")
    try:
        payload = report.encode("utf-8", "strict")
    except UnicodeError as error:
        raise AuditCodecError("report is not strict UTF-8") from error
    return _identity(
        REPORT_ID_PREFIX,
        task5.pack_string(REPORT_IDENTITY_DOMAIN)
        + task5.pack_string(REPORT_SCHEMA_ID)
        + payload,
    )


__all__ = [
    "AuditCodecError",
    "AuditValidationError",
    "PublicDecisionFrameV1",
    "SharedPublicDecisionV1",
    "FailureEventV1",
    "SharedJobEvidenceV1",
    "derive_first_divergence",
    "first_divergence_identity",
    "encode_first_divergence_json",
    "decode_first_divergence_json",
    "encode_first_divergence_jsonl",
    "decode_first_divergence_jsonl",
    "ReplayAdmissionSummaryReadModelV1",
    "GameplayJobResultReadModelV1",
    "GameplaySummaryReadModelV1",
    "encode_replay_admission_summary_json",
    "decode_replay_admission_summary_json",
    "encode_gameplay_job_result_json",
    "decode_gameplay_job_result_json",
    "encode_gameplay_job_results_jsonl",
    "decode_gameplay_job_results_jsonl",
    "encode_gameplay_summary_json",
    "decode_gameplay_summary_json",
    "derive_gameplay_summary",
    "BCInducedPopulationV1",
    "encode_bc_induced_population_json",
    "RateV1",
    "ComplianceEvidenceV1",
    "SliceComparisonV1",
    "PopulationProfileV1",
    "DistributionShiftV1",
    "derive_distribution_shift",
    "encode_distribution_shift_json",
    "decode_distribution_shift_json",
    "EvaluationSummaryV1",
    "encode_evaluation_summary_json",
    "decode_evaluation_summary_json",
    "EvaluationReadModelV1",
    "build_evaluation_read_model",
    "generate_report",
    "report_identity",
    "FIRST_DIVERGENCE_SCHEMA_ID",
    "DISTRIBUTION_SHIFT_SCHEMA_ID",
    "DISTRIBUTION_SHIFT_IDENTITY_DOMAIN",
    "EVALUATION_SUMMARY_SCHEMA_ID",
    "EVALUATION_SUMMARY_IDENTITY_DOMAIN",
    "REPORT_SCHEMA_ID",
    "REPORT_IDENTITY_DOMAIN",
    "BC_INDUCED_POPULATION_SCHEMA_ID",
    "BC_INDUCED_POPULATION_IDENTITY_DOMAIN",
    "P6_G14_STATUS",
    "P6_G15_STATUS",
]
