"""Offline Phase-6 Task-5B evaluation.

This module is deliberately above the admitted Task-4 corpus boundary.  It
does not discover files, inspect an engine, load a framework checkpoint, or
submit an action.  A caller supplies an already admitted public corpus and a
checkpoint-bound score provider.  The module then validates membership,
scores the complete current candidate domain, and derives public machine
evidence in deterministic order.

The score-provider boundary is intentionally small so that the accepted
Task-4 inference path can be adapted without importing PyTorch or JAX here::

    score_provider(sample, physical_candidate_width) ->
        OfflineScoreBatchV1 | task4_codec.InferenceResponseV1

The provider owns model execution.  This module owns no fallback and never
turns an inference failure into a Teacher or RandomLegal decision.
"""

from __future__ import annotations

import dataclasses
import hashlib
import math
import re
import struct
from collections import Counter, defaultdict
from typing import Any, Callable, Iterable, Optional, Sequence

from . import task4_codec as task4
from . import task5_codec as task5


class OfflineCodecError(task5.CodecError):
    """Raised for malformed, non-canonical, or privacy-invalid T5B data."""


class OfflineEvaluationError(OfflineCodecError):
    """Raised when the trusted population or evaluation configuration is invalid."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


# T5B-owned schemas and content-identity prefixes.
OFFLINE_SAMPLE_SCHEMA_ID = "ocgforge.phase6.offline_sample.v1"
OFFLINE_SLICE_SCHEMA_ID = "ocgforge.phase6.offline_slice.v1"
OFFLINE_METRICS_SCHEMA_ID = "ocgforge.phase6.offline_metrics.v1"
OFFLINE_SCORE_BATCH_SCHEMA_ID = "ocgforge.phase6.offline_score_batch.v1"
TEACHER_STATE_POPULATION_SCHEMA_ID = "ocgforge.phase6.teacher_state_population.v1"
TEACHER_STATE_POPULATION_IDENTITY_DOMAIN = (
    "ocgforge.phase6.teacher_state_population_identity.v1"
)
TEACHER_STATE_POPULATION_ID_PREFIX = "phase6_teacher_state_population.v1."
TEACHER_STATE_POPULATION_IDENTITY_PREFIX = TEACHER_STATE_POPULATION_ID_PREFIX
OFFLINE_SAMPLE_ID_PREFIX = "phase6_offline_sample.v1."
OFFLINE_SLICE_ID_PREFIX = "phase6_offline_slice.v1."
OFFLINE_METRICS_ID_PREFIX = "phase6_offline_metrics.v1."
LOSS_F64_CODEC_ID = "ocgforge.phase6.numeric.f64_ieee754_be.v1"

SLICE_DIMENSION_CONTRACT_ID = "ocgforge.phase6.offline_slice_dimensions.v1"
SLICE_COORDINATE_ABSENT = "ABSENT"
SLICE_DIMENSION_ORDER = (
    "decision_request_family",
    "candidate_domain_size",
    "phase",
    "turn_index",
    "acting_participant",
    "locked_deck_role",
    "starting_player",
    "continuation",
    "rare_critical",
)
SLICE_KIND_ORDER = (
    "decision_request_family",
    "candidate_domain_size",
    "candidate_domain_witness",
    "phase_decision_context",
    "acting_participant_deck_role",
    "starting_player",
    "continuation",
    "rare_critical",
)
PARTITION_ORDER = ("validation", "test")
STATUS_SCORED = "SCORED"
STATUS_REJECTED = "REJECTED"
STATUS_UNSCORED = "UNSCORED"
SLICE_PRESENT = "PRESENT"
SLICE_NOT_PRESENT = "NOT_PRESENT"
LABEL_PASS = "PASS"
LABEL_FAIL = "FAIL"
LABEL_NOT_RUN = "NOT_RUN"


class FailureReason:
    """Closed public failure taxonomy used by result and aggregate evidence."""

    MISSING_ADMISSION_RECEIPT = "MISSING_ADMISSION_RECEIPT"
    INVALID_ADMISSION_RECEIPT = "INVALID_ADMISSION_RECEIPT"
    QUARANTINED_TRAJECTORY = "QUARANTINED_TRAJECTORY"
    INELIGIBLE_TEACHER_POLICY = "INELIGIBLE_TEACHER_POLICY"
    SPLIT_LEAKAGE = "SPLIT_LEAKAGE"
    SAMPLE_IDENTITY_MISMATCH = "SAMPLE_IDENTITY_MISMATCH"
    PUBLIC_INPUT_REJECTION = "PUBLIC_INPUT_REJECTION"
    CANDIDATE_DOMAIN_FAILURE = "CANDIDATE_DOMAIN_FAILURE"
    LABEL_MISMATCH = "LABEL_MISMATCH"
    CANDIDATE_CAPACITY_FAILURE = "CANDIDATE_CAPACITY_FAILURE"
    PADDING_MASK_VIOLATION = "PADDING_MASK_VIOLATION"
    SCORE_COUNT_MISMATCH = "SCORE_COUNT_MISMATCH"
    NONFINITE_SCORE = "NONFINITE_SCORE"
    MODEL_BINDING_FAILURE = "MODEL_BINDING_FAILURE"
    INFERENCE_FAILURE = "INFERENCE_FAILURE"


FAILURE_REASONS = (
    FailureReason.MISSING_ADMISSION_RECEIPT,
    FailureReason.INVALID_ADMISSION_RECEIPT,
    FailureReason.QUARANTINED_TRAJECTORY,
    FailureReason.INELIGIBLE_TEACHER_POLICY,
    FailureReason.SPLIT_LEAKAGE,
    FailureReason.SAMPLE_IDENTITY_MISMATCH,
    FailureReason.PUBLIC_INPUT_REJECTION,
    FailureReason.CANDIDATE_DOMAIN_FAILURE,
    FailureReason.LABEL_MISMATCH,
    FailureReason.CANDIDATE_CAPACITY_FAILURE,
    FailureReason.PADDING_MASK_VIOLATION,
    FailureReason.SCORE_COUNT_MISMATCH,
    FailureReason.NONFINITE_SCORE,
    FailureReason.MODEL_BINDING_FAILURE,
    FailureReason.INFERENCE_FAILURE,
)


def _identity(prefix: str, body: bytes) -> str:
    return prefix + hashlib.sha256(body).hexdigest()


def _is_hex(value: Any, length: int = 64) -> bool:
    return isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{length}}}", value) is not None


def _validate_digest(value: Any, field: str) -> None:
    if not _is_hex(value):
        raise OfflineCodecError(f"{field} is not a lowercase SHA-256 digest")


def _validate_prefixed(value: Any, prefix: str, field: str) -> None:
    if not isinstance(value, str) or not value.startswith(prefix) or not _is_hex(value[len(prefix):]):
        raise OfflineCodecError(f"{field} is not a canonical {prefix} identity")


def _validate_string(value: Any, field: str, *, nonempty: bool = True) -> None:
    if not isinstance(value, str) or (nonempty and not value):
        raise OfflineCodecError(f"{field} is not an accepted string")
    try:
        value.encode("utf-8", "strict")
    except UnicodeError as error:
        raise OfflineCodecError(f"{field} is not strict UTF-8") from error


def _validate_u32(value: Any, field: str) -> None:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise OfflineCodecError(f"{field} is not a u32")


def _validate_u64(value: Any, field: str) -> None:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise OfflineCodecError(f"{field} is not a u64")


def _validate_bool(value: Any, field: str) -> None:
    if not isinstance(value, bool):
        raise OfflineCodecError(f"{field} is not a bool")


def _strict_object(payload: Any, fields: Sequence[str], label: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise OfflineCodecError(f"{label} is not an object")
    expected = set(fields)
    unknown = set(payload) - expected
    missing = expected - set(payload)
    if unknown:
        raise OfflineCodecError(f"{label} has unknown fields: {sorted(unknown)}")
    if missing:
        raise OfflineCodecError(f"{label} is missing fields: {sorted(missing)}")
    return payload


def _validate_partitions(partitions: Sequence[str], field: str = "selected_partitions") -> tuple[str, ...]:
    if not isinstance(partitions, (tuple, list)):
        raise OfflineCodecError(f"{field} is not an ordered vector")
    value = tuple(partitions)
    if not value or any(partition not in PARTITION_ORDER for partition in value):
        raise OfflineCodecError(f"{field} contains an unsupported partition")
    if value != tuple(partition for partition in PARTITION_ORDER if partition in value):
        raise OfflineCodecError(f"{field} is not in validation-then-test order")
    if len(set(value)) != len(value):
        raise OfflineCodecError(f"{field} contains duplicates")
    return value


def _validate_lower_token(value: Optional[str], field: str, *, allow_empty: bool = False) -> None:
    if value is None:
        return
    if not isinstance(value, str) or (not allow_empty and not value):
        raise OfflineCodecError(f"{field} is not a token")
    if allow_empty and value == "":
        return
    if re.fullmatch(r"[a-z][a-z0-9_]*", value) is None:
        raise OfflineCodecError(f"{field} is not a lowercase token")


@dataclasses.dataclass(frozen=True)
class OfflineSourceSampleV1:
    """An exact public sample read from the trusted Task-4 output."""

    sample: task4.CorpusSampleV1
    numeric_input_identity: str = ""


@dataclasses.dataclass(frozen=True)
class TrustedOfflinePopulationV1:
    """The only population input accepted by the offline evaluator.

    ``admitted_corpus`` and ``admission_authority`` are the existing Task-4
    authority sidecar pair.  ``source_samples`` must be an exact value-owned
    read model of that pair; T5B never issues a receipt, adds Teacher
    provenance, or reads a trajectory path from disk.
    """

    source_dataset_identity: str
    dataset_manifest_identity: str
    dataset_split_identity: str
    evaluation_contract_identity: str
    admitted_corpus: task4.DerivedCorpusV1
    admission_authority: task4.CorpusAdmissionAuthorityV1
    source_samples: tuple[OfflineSourceSampleV1, ...]

    def validate(self) -> None:
        _validate_digest(self.source_dataset_identity, "source_dataset_identity")
        _validate_digest(self.dataset_manifest_identity, "dataset_manifest_identity")
        if self.dataset_manifest_identity != self.source_dataset_identity:
            raise OfflineEvaluationError(
                "INVALID_DATASET_MANIFEST",
                "dataset manifest identity differs from source DatasetManifest identity",
            )
        _validate_prefixed(self.dataset_split_identity, "phase6_dataset_split.v1.", "dataset_split_identity")
        if self.evaluation_contract_identity != task5.evaluation_contract_identity():
            raise OfflineEvaluationError(
                "WRONG_EVALUATION_CONTRACT",
                "offline population uses an unaccepted evaluation contract",
            )
        if not isinstance(self.admitted_corpus, task4.DerivedCorpusV1):
            raise OfflineEvaluationError("INVALID_CORPUS", "admitted corpus has the wrong DTO type")
        if not isinstance(self.admission_authority, task4.CorpusAdmissionAuthorityV1):
            raise OfflineEvaluationError("INVALID_AUTHORITY", "admission authority has the wrong DTO type")
        if self.admitted_corpus.source_dataset_identity != self.source_dataset_identity:
            raise OfflineEvaluationError("INVALID_DATASET_MANIFEST", "corpus dataset identity is not admitted")
        if self.admitted_corpus.split_identity != self.dataset_split_identity:
            raise OfflineEvaluationError("INVALID_SPLIT", "corpus split identity differs from requested split")
        if self.admission_authority.source_dataset_identity != self.source_dataset_identity:
            raise OfflineEvaluationError("INVALID_DATASET_MANIFEST", "authority dataset identity differs")
        if self.admission_authority.split_identity != self.dataset_split_identity:
            raise OfflineEvaluationError("INVALID_SPLIT", "authority split identity differs")
        try:
            # This is an in-memory re-check of the accepted trusted sidecar;
            # no arbitrary file or path is consulted.
            artifact = task4.encode_corpus_artifact(self.admitted_corpus)
            admitted = task4.admit_corpus_artifact(artifact, self.admission_authority)
        except (task4.CodecError, TypeError, ValueError) as error:
            raise OfflineEvaluationError("ADMISSION_BINDING_FAILURE", str(error)) from error
        if admitted != self.admitted_corpus:
            raise OfflineEvaluationError("ADMISSION_BINDING_FAILURE", "admitted corpus changed during validation")

        expected_by_id = {
            authority_sample.bc_sample_identity: authority_sample
            for authority_sample in self.admission_authority.source_samples
        }
        corpus_ids = {sample.bc_sample_identity for sample in self.admitted_corpus.samples}
        if corpus_ids != set(expected_by_id):
            raise OfflineEvaluationError("ADMISSION_BINDING_FAILURE", "corpus and authority memberships differ")
        if not isinstance(self.source_samples, tuple):
            raise OfflineEvaluationError("INVALID_SOURCE_POPULATION", "source samples are not an ordered tuple")
        source_ids: list[str] = []
        for source in self.source_samples:
            if not isinstance(source, OfflineSourceSampleV1) or not isinstance(source.sample, task4.CorpusSampleV1):
                raise OfflineEvaluationError("INVALID_SOURCE_POPULATION", "source sample has the wrong DTO type")
            _validate_prefixed(source.sample.bc_sample_identity, "bc_sample.v1.", "source sample identity")
            source_ids.append(source.sample.bc_sample_identity)
        if len(set(source_ids)) != len(source_ids):
            raise OfflineEvaluationError("DUPLICATE_SAMPLE_IDENTITY", "source sample identity is duplicated")
        if set(source_ids) != set(expected_by_id):
            raise OfflineEvaluationError("ADMISSION_BINDING_FAILURE", "source sample membership is incomplete or unadmitted")

        admitted_samples = {
            sample.bc_sample_identity: sample
            for sample in self.admitted_corpus.samples
        }
        for source in self.source_samples:
            admitted = admitted_samples.get(source.sample.bc_sample_identity)
            if admitted is None or source.sample != admitted:
                raise OfflineEvaluationError(
                    "ADMISSION_BINDING_FAILURE",
                    "source sample is not the exact admitted Task-4 materialization",
                )
            try:
                numeric_input = task4.make_numeric_model_input(
                    model_input_identity=source.sample.model_input_identity,
                    state_rows=source.sample.state_rows,
                    candidate_rows=source.sample.candidate_rows,
                    routing_keys=source.sample.routing_keys,
                    public_candidate_domain_digest=source.sample.public_candidate_domain_digest,
                    public_semantic_decision_id=source.sample.public_semantic_decision_id,
                    perspective_player=source.sample.perspective_player,
                    decision_index=source.sample.decision_index,
                )
                _validate_prefixed(
                    source.numeric_input_identity,
                    task4.NUMERIC_MODEL_INPUT_ID_PREFIX,
                    "numeric_input_identity",
                )
            except (task4.CodecError, TypeError, ValueError) as error:
                raise OfflineEvaluationError("PUBLIC_INPUT_REJECTION", str(error)) from error
            if source.numeric_input_identity != numeric_input.numeric_input_identity:
                raise OfflineEvaluationError(
                    "PUBLIC_INPUT_REJECTION",
                    "source numeric-input identity does not match admitted rows",
                )


@dataclasses.dataclass(frozen=True)
class OfflineEvaluationConfigV1:
    evaluation_context: task5.EvaluationContextV1
    source_dataset_identity: str
    dataset_split_identity: str
    selected_partitions: tuple[str, ...] = PARTITION_ORDER
    physical_candidate_capacity: Optional[int] = None
    top_k: Optional[int] = None

    def validate(self) -> None:
        if not isinstance(self.evaluation_context, task5.EvaluationContextV1):
            raise OfflineEvaluationError("INVALID_EVALUATION_CONTEXT", "evaluation context has the wrong DTO type")
        try:
            self.evaluation_context.validate()
        except task5.CodecError as error:
            raise OfflineEvaluationError("INVALID_EVALUATION_CONTEXT", str(error)) from error
        _validate_digest(self.source_dataset_identity, "source_dataset_identity")
        _validate_prefixed(self.dataset_split_identity, "phase6_dataset_split.v1.", "dataset_split_identity")
        _validate_partitions(self.selected_partitions)
        if self.physical_candidate_capacity is not None:
            if isinstance(self.physical_candidate_capacity, bool) or not isinstance(self.physical_candidate_capacity, int) or self.physical_candidate_capacity < 0:
                raise OfflineEvaluationError("INVALID_CAPACITY", "physical candidate capacity is invalid")
        if self.top_k is not None:
            if isinstance(self.top_k, bool) or not isinstance(self.top_k, int) or self.top_k < 1:
                raise OfflineEvaluationError("INVALID_TOP_K", "top-K must be an integer greater than zero")


@dataclasses.dataclass(frozen=True)
class OfflineScoreBatchV1:
    """Physical batch wrapper around an already accepted Task-4 response.

    This is not a second inference response.  The embedded response must
    validate first; the additional rows and mask only describe the physical
    batch used by the accepted inference adapter.
    """

    inference_response: task4.InferenceResponseV1
    physical_candidate_width: int
    score_f32_bits: tuple[str, ...]
    real_candidate_mask: tuple[int, ...]
    schema_id: str = OFFLINE_SCORE_BATCH_SCHEMA_ID

    def validate(self) -> None:
        if self.schema_id != OFFLINE_SCORE_BATCH_SCHEMA_ID:
            raise OfflineCodecError("score batch schema is not accepted")
        if not isinstance(self.inference_response, task4.InferenceResponseV1):
            raise OfflineCodecError("score batch does not contain a Task-4 inference response")
        if isinstance(self.physical_candidate_width, bool) or not isinstance(self.physical_candidate_width, int) or self.physical_candidate_width < 0:
            raise OfflineCodecError("score physical width is invalid")
        if not isinstance(self.score_f32_bits, tuple) or len(self.score_f32_bits) != self.physical_candidate_width:
            raise OfflineCodecError("score count does not equal physical width")
        if not isinstance(self.real_candidate_mask, tuple) or len(self.real_candidate_mask) != self.physical_candidate_width:
            raise OfflineCodecError("score mask count does not equal physical width")
        for bits in self.score_f32_bits:
            try:
                task5.score_f32_bytes(bits)
            except task5.CodecError as error:
                raise OfflineCodecError(str(error)) from error
        for value in self.real_candidate_mask:
            if isinstance(value, bool) or value not in (0, 1):
                raise OfflineCodecError("score mask is not binary")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "inference_response": {
                "request_identity": self.inference_response.request_identity,
                "checkpoint_identity": self.inference_response.checkpoint_identity,
                "model_input_identity": self.inference_response.model_input_identity,
                "ordered_candidate_domain_identity": self.inference_response.ordered_candidate_domain_identity,
                "score_f32_bits": [
                    task5.score_f32_bits(value)
                    for value in self.inference_response.scores
                ],
                "selected_candidate_ordinal": self.inference_response.selected_candidate_ordinal,
                "selected_public_action_key": self.inference_response.selected_public_action_key,
                "response_identity": self.inference_response.response_identity,
                "schema_id": self.inference_response.schema_id,
            },
            "physical_candidate_width": self.physical_candidate_width,
            "real_candidate_mask": list(self.real_candidate_mask),
            "schema_id": self.schema_id,
            "score_f32_bits": list(self.score_f32_bits),
        }


def _score_batch_from_provider_response(
    response: Any,
    sample: task4.CorpusSampleV1,
    expected_checkpoint_identity: str,
    physical_width: int,
) -> OfflineScoreBatchV1:
    n = len(sample.routing_keys)

    def validate_response(response_value: task4.InferenceResponseV1) -> tuple[str, ...]:
        if response_value.schema_id != task4.INFERENCE_RESPONSE_SCHEMA_ID:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "inference response schema is not accepted")
        if not isinstance(response_value.request_identity, str) or not response_value.request_identity.startswith("phase6_inference_request.v1.") or not _is_hex(response_value.request_identity[len("phase6_inference_request.v1."):]):
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "inference response request identity is invalid")
        try:
            domain_digest = sample.public_candidate_domain_digest
            if domain_digest is None and _is_hex(sample.ordered_candidate_domain_identity):
                domain_digest = sample.ordered_candidate_domain_identity
            numeric_input = task4.make_numeric_model_input(
                model_input_identity=sample.model_input_identity,
                state_rows=sample.state_rows,
                candidate_rows=sample.candidate_rows,
                routing_keys=sample.routing_keys,
                public_candidate_domain_digest=domain_digest,
                public_semantic_decision_id=sample.public_semantic_decision_id,
                perspective_player=sample.perspective_player,
                decision_index=sample.decision_index,
            )
            expected_request = task4.make_inference_request(
                checkpoint_identity=expected_checkpoint_identity,
                model_input=numeric_input,
            )
        except (task4.CodecError, TypeError, ValueError) as error:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "sample cannot reconstruct its accepted inference request") from error
        if response_value.request_identity != expected_request.request_identity:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "inference response request does not bind the exact sample input")
        if response_value.checkpoint_identity != expected_checkpoint_identity or response_value.model_input_identity != sample.model_input_identity or response_value.ordered_candidate_domain_identity != sample.ordered_candidate_domain_identity:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "inference response semantic binding differs from sample context")
        try:
            score_bits = tuple(task5.score_f32_bits(value) for value in response_value.scores)
        except task5.CodecError as error:
            raise OfflineEvaluationError(FailureReason.NONFINITE_SCORE, str(error)) from error
        if len(score_bits) != n:
            raise OfflineEvaluationError(FailureReason.SCORE_COUNT_MISMATCH, "inference response score count is not the exact domain size")
        try:
            expected_response_identity = task4.inference_response_selection_identity(response_value)
        except (task4.CodecError, TypeError, ValueError) as error:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "inference response identity is invalid") from error
        if response_value.response_identity != expected_response_identity:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "inference response identity does not match its envelope")
        score_vector = task5.ScoreVectorV1(
            public_action_keys=tuple(sample.routing_keys),
            score_f32_bits=score_bits,
        )
        selected = task5.select_score_vector(score_vector)
        if (
            response_value.selected_candidate_ordinal != selected
            or response_value.selected_public_action_key != sample.routing_keys[selected]
        ):
            raise OfflineEvaluationError(
                FailureReason.MODEL_BINDING_FAILURE,
                "inference response selection does not use the accepted source-order tie rule",
            )
        return score_bits

    if isinstance(response, OfflineScoreBatchV1):
        try:
            response.validate()
        except OfflineCodecError as error:
            message = str(error)
            if "non-finite" in message:
                raise OfflineEvaluationError(FailureReason.NONFINITE_SCORE, message) from error
            if "score count" in message or "physical width" in message:
                raise OfflineEvaluationError(FailureReason.SCORE_COUNT_MISMATCH, message) from error
            raise OfflineEvaluationError(FailureReason.INFERENCE_FAILURE, message) from error
        if not isinstance(response.inference_response, task4.InferenceResponseV1):
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "score batch has no accepted inference response")
        response_bits = validate_response(response.inference_response)
        if tuple(response.score_f32_bits[:n]) != response_bits:
            raise OfflineEvaluationError(FailureReason.MODEL_BINDING_FAILURE, "physical batch changed accepted real score bits")
        batch = response
    elif isinstance(response, task4.InferenceResponseV1):
        response_bits = validate_response(response)
        batch = OfflineScoreBatchV1(
            inference_response=response,
            physical_candidate_width=n,
            score_f32_bits=response_bits,
            real_candidate_mask=(1,) * n,
        )
    else:
        raise OfflineEvaluationError(FailureReason.INFERENCE_FAILURE, "score provider returned an unsupported response DTO")

    if batch.physical_candidate_width < n:
        raise OfflineEvaluationError(FailureReason.CANDIDATE_CAPACITY_FAILURE, "physical width is smaller than semantic candidate count")
    if batch.physical_candidate_width != physical_width:
        raise OfflineEvaluationError(FailureReason.SCORE_COUNT_MISMATCH, "score physical width differs from requested width")
    expected_mask = (1,) * n + (0,) * (batch.physical_candidate_width - n)
    if batch.real_candidate_mask != expected_mask:
        raise OfflineEvaluationError(FailureReason.PADDING_MASK_VIOLATION, "real/padding mask does not match exact semantic domain")
    if len(batch.score_f32_bits) != batch.physical_candidate_width:
        raise OfflineEvaluationError(FailureReason.SCORE_COUNT_MISMATCH, "score vector width is not exact")
    return batch


def loss_f64_bits(value: float) -> str:
    if isinstance(value, bool):
        raise OfflineCodecError("bool is not a binary64 loss")
    try:
        converted = float(value)
        if not math.isfinite(converted):
            raise OfflineCodecError("loss is not finite")
        raw = struct.pack(">d", converted)
    except (OverflowError, struct.error, TypeError, ValueError) as error:
        raise OfflineCodecError("loss is not representable as binary64") from error
    if not math.isfinite(struct.unpack(">d", raw)[0]):
        raise OfflineCodecError("loss is not finite")
    return raw.hex()


def loss_f64_bytes(value: Any) -> bytes:
    if not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{16}", value) is None:
        raise OfflineCodecError("loss_f64_bits is not sixteen lowercase hex characters")
    raw = bytes.fromhex(value)
    if not math.isfinite(struct.unpack(">d", raw)[0]):
        raise OfflineCodecError("loss bytes are non-finite")
    return raw


def loss_f64_value(bits: str) -> float:
    return struct.unpack(">d", loss_f64_bytes(bits))[0]


def exact_domain_loss(score_f32_bits: Sequence[str], teacher_ordinal: int) -> tuple[float, str]:
    if not isinstance(score_f32_bits, (tuple, list)) or not score_f32_bits:
        raise OfflineCodecError("exact-domain score vector is empty")
    if not isinstance(teacher_ordinal, int) or isinstance(teacher_ordinal, bool) or not 0 <= teacher_ordinal < len(score_f32_bits):
        raise OfflineCodecError("Teacher ordinal is outside exact score domain")
    scores = tuple(task5.score_f32_value(bits) for bits in score_f32_bits)
    maximum = max(scores)
    log_sum_exp = maximum + math.log(math.fsum(math.exp(score - maximum) for score in scores))
    loss = log_sum_exp - scores[teacher_ordinal]
    return loss, loss_f64_bits(loss)


def canonical_teacher_state_population_identity_bytes(
    *,
    source_dataset_identity: str,
    dataset_split_identity: str,
    selected_partitions: Sequence[str],
    evaluation_contract_identity: str,
    ordered_bc_sample_identities: Sequence[str],
    partition_sample_counts: Sequence[int],
) -> bytes:
    _validate_digest(source_dataset_identity, "source_dataset_identity")
    _validate_prefixed(dataset_split_identity, "phase6_dataset_split.v1.", "dataset_split_identity")
    partitions = _validate_partitions(selected_partitions)
    if evaluation_contract_identity != task5.evaluation_contract_identity():
        raise OfflineCodecError("Teacher-state population uses an unaccepted contract identity")
    identities = tuple(ordered_bc_sample_identities)
    if not identities:
        raise OfflineCodecError("Teacher-state population cannot be empty")
    for identity in identities:
        _validate_prefixed(identity, "bc_sample.v1.", "bc_sample_identity")
    if len(set(identities)) != len(identities):
        raise OfflineCodecError("Teacher-state population sample identities are duplicated")
    if not isinstance(partition_sample_counts, (tuple, list)) or len(partition_sample_counts) != len(partitions):
        raise OfflineCodecError("Teacher-state population partition counts are incomplete")
    if any(
        isinstance(count, bool) or not isinstance(count, int) or count < 0
        for count in partition_sample_counts
    ) or sum(partition_sample_counts) != len(identities):
        raise OfflineCodecError("Teacher-state population partition counts do not conserve samples")
    # The evaluator supplies a validation-then-test vector, with each
    # partition already sorted by unsigned UTF-8 identity.  Counts and the
    # partition labels are encoded below so the preimage cannot conflate the
    # same identifiers assigned to different selected partitions.  A
    # reordered vector produces a different digest rather than being silently
    # normalized.
    offset = 0
    for count in partition_sample_counts:
        partition_ids = identities[offset:offset + count]
        offset += count
        previous: Optional[bytes] = None
        for identity in partition_ids:
            _validate_string(identity, "bc_sample_identity")
            encoded = identity.encode("utf-8")
            if previous is not None and encoded <= previous:
                raise OfflineCodecError("Teacher-state population partition is not unsigned-UTF-8 ascending")
            previous = encoded
    out = [
        task5.pack_string(TEACHER_STATE_POPULATION_IDENTITY_DOMAIN),
        task5.pack_string(TEACHER_STATE_POPULATION_SCHEMA_ID),
        task5.pack_string(source_dataset_identity),
        task5.pack_string(dataset_split_identity),
        task5.pack_string(evaluation_contract_identity),
        task5.pack_string_vector(partitions),
    ]
    offset = 0
    for partition, count in zip(partitions, partition_sample_counts):
        partition_ids = identities[offset:offset + count]
        offset += count
        out.extend((task5.pack_string(partition), task5.pack_string_vector(partition_ids)))
    return b"".join(out)


def teacher_state_population_identity(
    *,
    source_dataset_identity: str,
    dataset_split_identity: str,
    selected_partitions: Sequence[str],
    evaluation_contract_identity: str,
    ordered_bc_sample_identities: Sequence[str],
    partition_sample_counts: Sequence[int],
) -> str:
    return _identity(
        TEACHER_STATE_POPULATION_ID_PREFIX,
        canonical_teacher_state_population_identity_bytes(
            source_dataset_identity=source_dataset_identity,
            dataset_split_identity=dataset_split_identity,
            selected_partitions=selected_partitions,
            evaluation_contract_identity=evaluation_contract_identity,
            ordered_bc_sample_identities=ordered_bc_sample_identities,
            partition_sample_counts=partition_sample_counts,
        ),
    )


def _candidate_domain_validation(
    source: OfflineSourceSampleV1,
) -> tuple[int, tuple[str, ...], str]:
    sample = source.sample
    if not isinstance(sample.routing_keys, tuple) or not isinstance(sample.candidate_rows, tuple):
        raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, "candidate domain is not tuple-owned")
    keys = tuple(sample.routing_keys)
    rows = tuple(sample.candidate_rows)
    if not keys or len(keys) != len(rows):
        raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, "candidate/key cardinality is not exact")
    if len(set(keys)) != len(keys):
        raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, "candidate domain contains duplicate keys")
    for index, key in enumerate(keys):
        try:
            task5.validate_public_action_key(key)
        except task5.CodecError as error:
            raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, f"candidate key {index} is invalid") from error
    for row in sample.state_rows:
        if len(row) != task4.STATE_ROW_WIDTH:
            raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "state row width is invalid")
        for value in row:
            try:
                task4.f32_bytes(value)
            except task4.CodecError as error:
                raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "state row contains a non-finite value") from error
    for row in rows:
        if len(row) != task4.CANDIDATE_ROW_WIDTH:
            raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "candidate row width is invalid")
        for value in row:
            try:
                task4.f32_bytes(value)
            except task4.CodecError as error:
                raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "candidate row contains a non-finite value") from error
    # The accepted Task-4 corpus already carries the validated Phase-5
    # candidate-domain digest.  T5B has no DecisionRecord annotation input,
    # so it never invents a request kind.  A fallback identity is validated
    # by T5A only when the accepted source did not carry a Phase-5 digest.
    if sample.public_candidate_domain_digest is not None:
        if not _is_hex(sample.public_candidate_domain_digest):
            raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, "Phase-5 domain digest is invalid")
        if sample.ordered_candidate_domain_identity != sample.public_candidate_domain_digest:
            raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, "Phase-5 domain identity is not the accepted source digest")
    else:
        try:
            task5.validate_ordered_candidate_domain_identity(
                sample.ordered_candidate_domain_identity,
                None,
                keys,
            )
        except task5.CodecError as error:
            raise OfflineEvaluationError(FailureReason.CANDIDATE_DOMAIN_FAILURE, str(error)) from error
    if not isinstance(sample.model_input_identity, str) or not sample.model_input_identity.startswith("model_input.v1."):
        raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "model-input identity is invalid")
    if not _is_hex(sample.public_semantic_decision_id):
        raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "public decision identity is invalid")
    if not isinstance(sample.trajectory_record_id, str) or not sample.trajectory_record_id.startswith("trajectory_record.v1."):
        raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "trajectory identity is invalid")
    try:
        expected_sample_identity = task4.bc_sample_identity(sample)
    except (task4.CodecError, TypeError, ValueError) as error:
        raise OfflineEvaluationError(FailureReason.SAMPLE_IDENTITY_MISMATCH, "BC sample identity fields are not canonical") from error
    if sample.bc_sample_identity != expected_sample_identity:
        raise OfflineEvaluationError(FailureReason.SAMPLE_IDENTITY_MISMATCH, "BC sample identity does not match source fields")
    try:
        numeric_model_input = task4.make_numeric_model_input(
            model_input_identity=sample.model_input_identity,
            state_rows=sample.state_rows,
            candidate_rows=sample.candidate_rows,
            routing_keys=sample.routing_keys,
            public_candidate_domain_digest=sample.public_candidate_domain_digest,
            public_semantic_decision_id=sample.public_semantic_decision_id,
            perspective_player=sample.perspective_player,
            decision_index=sample.decision_index,
        )
        task4.validate_numeric_model_input(numeric_model_input)
        _validate_prefixed(
            source.numeric_input_identity,
            task4.NUMERIC_MODEL_INPUT_ID_PREFIX,
            "numeric_input_identity",
        )
        if source.numeric_input_identity != numeric_model_input.numeric_input_identity:
            raise OfflineEvaluationError(
                FailureReason.SAMPLE_IDENTITY_MISMATCH,
                "numeric model-input identity does not match public rows",
            )
    except (task4.CodecError, TypeError, ValueError) as error:
        raise OfflineEvaluationError(FailureReason.PUBLIC_INPUT_REJECTION, "accepted numeric model-input representation could not be reconstructed") from error
    return len(keys), keys, sample.ordered_candidate_domain_identity


def _label_validation(sample: task4.CorpusSampleV1, keys: Sequence[str]) -> int:
    try:
        task5.validate_public_action_key(sample.selected_public_action_key)
    except task5.CodecError as error:
        raise OfflineEvaluationError(FailureReason.LABEL_MISMATCH, "selected Teacher key is not canonical") from error
    occurrences = tuple(index for index, key in enumerate(keys) if key == sample.selected_public_action_key)
    if len(occurrences) != 1:
        raise OfflineEvaluationError(FailureReason.LABEL_MISMATCH, "Teacher key is absent or duplicated in the exact domain")
    if isinstance(sample.candidate_ordinal, bool) or not isinstance(sample.candidate_ordinal, int):
        raise OfflineEvaluationError(FailureReason.LABEL_MISMATCH, "Teacher ordinal is not an integer")
    if sample.candidate_ordinal != occurrences[0]:
        raise OfflineEvaluationError(FailureReason.LABEL_MISMATCH, "Teacher key and ordinal disagree")
    return occurrences[0]


def _accepted_context_fields(sample: task4.CorpusSampleV1) -> dict[str, Any]:
    """Project only coordinates present in the accepted Task-4 sample.

    Task-4's Python corpus DTO does not expose phase, start-seat, deck-role,
    continuation, or request-family fields.  T5B therefore records those
    dimensions as explicit absence rather than accepting caller annotations.
    Decision index and perspective player are carried by the accepted sample.
    """

    return {
        "decision_request_family": None,
        "phase": None,
        "turn_index": None,
        "acting_participant": sample.perspective_player,
        "locked_deck_role_id": None,
        "starting_player": None,
        "continuation": None,
        "rare_critical_slice": None,
    }


@dataclasses.dataclass(frozen=True)
class OfflineSampleResultV1:
    schema_id: str
    evaluation_identity: str
    evaluation_contract_identity: str
    teacher_state_population_identity: str
    bc_sample_identity: str
    partition: str
    trajectory_record_id: str
    episode_semantic_id: str
    public_semantic_decision_id: str
    model_input_identity: str
    candidate_count: Optional[int]
    ordered_candidate_domain_identity: Optional[str]
    candidate_public_action_keys: Optional[tuple[str, ...]]
    teacher_selected_public_action_key: Optional[str]
    teacher_candidate_ordinal: Optional[int]
    decision_request_family: Optional[str]
    phase: Optional[int]
    turn_index: Optional[int]
    acting_participant: Optional[int]
    locked_deck_role_id: Optional[str]
    starting_player: Optional[int]
    continuation: Optional[bool]
    rare_critical_slice: Optional[str]
    status: str
    failure_reason: Optional[str]
    score_vector: Optional[task5.ScoreVectorV1]
    score_vector_identity: Optional[str]
    loss_f64_bits: Optional[str]
    model_selected_public_action_key: Optional[str]
    model_candidate_ordinal: Optional[int]
    top1_agreement: Optional[bool]
    top_k: Optional[int]
    top_k_agreement: Optional[bool]
    label_consistency: str
    teacher_key_consistency: Optional[bool]
    teacher_ordinal_consistency: Optional[bool]

    @property
    def offline_sample_identity(self) -> str:
        return _identity(OFFLINE_SAMPLE_ID_PREFIX, self._canonical_payload_bytes())

    def _payload(self, *, include_identity: bool) -> dict[str, Any]:
        result: dict[str, Any] = {
            "acting_participant": self.acting_participant,
            "bc_sample_identity": self.bc_sample_identity,
            "candidate_count": self.candidate_count,
            "candidate_public_action_keys": (
                list(self.candidate_public_action_keys)
                if self.candidate_public_action_keys is not None else None
            ),
            "continuation": self.continuation,
            "decision_request_family": self.decision_request_family,
            "episode_semantic_id": self.episode_semantic_id,
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_identity": self.evaluation_identity,
            "failure_reason": self.failure_reason,
            "label_consistency": self.label_consistency,
            "locked_deck_role_id": self.locked_deck_role_id,
            "loss_f64_bits": self.loss_f64_bits,
            "model_input_identity": self.model_input_identity,
            "model_selected_public_action_key": self.model_selected_public_action_key,
            "model_candidate_ordinal": self.model_candidate_ordinal,
            "ordered_candidate_domain_identity": self.ordered_candidate_domain_identity,
            "partition": self.partition,
            "phase": self.phase,
            "public_semantic_decision_id": self.public_semantic_decision_id,
            "rare_critical_slice": self.rare_critical_slice,
            "schema_id": self.schema_id,
            "score_vector": self.score_vector.to_dict() if self.score_vector is not None else None,
            "score_vector_identity": self.score_vector_identity,
            "starting_player": self.starting_player,
            "status": self.status,
            "teacher_candidate_ordinal": self.teacher_candidate_ordinal,
            "teacher_key_consistency": self.teacher_key_consistency,
            "teacher_ordinal_consistency": self.teacher_ordinal_consistency,
            "teacher_selected_public_action_key": self.teacher_selected_public_action_key,
            "teacher_state_population_identity": self.teacher_state_population_identity,
            "top1_agreement": self.top1_agreement,
            "top_k": self.top_k,
            "top_k_agreement": self.top_k_agreement,
            "trajectory_record_id": self.trajectory_record_id,
            "turn_index": self.turn_index,
        }
        if include_identity:
            result["offline_sample_identity"] = self.offline_sample_identity
        return result

    def _canonical_payload_bytes(self) -> bytes:
        return task5.canonical_json_bytes(self._payload(include_identity=False))

    def validate(self) -> None:
        if self.schema_id != OFFLINE_SAMPLE_SCHEMA_ID:
            raise OfflineCodecError("offline sample schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_contract_identity, task5.EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        if self.evaluation_contract_identity != task5.evaluation_contract_identity():
            raise OfflineCodecError("offline sample contract identity is not accepted")
        _validate_prefixed(self.teacher_state_population_identity, TEACHER_STATE_POPULATION_ID_PREFIX, "teacher_state_population_identity")
        _validate_prefixed(self.bc_sample_identity, "bc_sample.v1.", "bc_sample_identity")
        if self.partition not in PARTITION_ORDER:
            raise OfflineCodecError("offline sample partition is not validation/test")
        _validate_prefixed(self.trajectory_record_id, "trajectory_record.v1.", "trajectory_record_id")
        _validate_digest(self.episode_semantic_id, "episode_semantic_id")
        _validate_digest(self.public_semantic_decision_id, "public_semantic_decision_id")
        _validate_prefixed(self.model_input_identity, "model_input.v1.", "model_input_identity")
        if self.candidate_count is not None:
            _validate_u32(self.candidate_count, "candidate_count")
        if self.candidate_public_action_keys is not None:
            if not isinstance(self.candidate_public_action_keys, tuple) or not self.candidate_public_action_keys:
                raise OfflineCodecError("candidate_public_action_keys is not an ordered vector")
            if self.candidate_count != len(self.candidate_public_action_keys):
                raise OfflineCodecError("candidate count and key vector differ")
            for key in self.candidate_public_action_keys:
                task5.validate_public_action_key(key)
        elif self.candidate_count is not None:
            raise OfflineCodecError("candidate count exists without candidate keys")
        if self.ordered_candidate_domain_identity is not None:
            if not isinstance(self.ordered_candidate_domain_identity, str):
                raise OfflineCodecError("ordered candidate domain identity is not text")
            if self.candidate_public_action_keys is not None:
                if self.ordered_candidate_domain_identity.startswith(
                    task5.ORDERED_CANDIDATE_DOMAIN_ID_PREFIX
                ):
                    task5.validate_ordered_candidate_domain_identity(
                        self.ordered_candidate_domain_identity,
                        None,
                        self.candidate_public_action_keys,
                    )
                else:
                    _validate_digest(
                        self.ordered_candidate_domain_identity,
                        "ordered_candidate_domain_identity",
                    )
            if self.candidate_public_action_keys is None:
                raise OfflineCodecError("ordered candidate domain exists without candidate keys")
        if self.teacher_selected_public_action_key is not None:
            task5.validate_public_action_key(self.teacher_selected_public_action_key)
        if self.teacher_candidate_ordinal is not None:
            _validate_u32(self.teacher_candidate_ordinal, "teacher_candidate_ordinal")
        if (self.teacher_selected_public_action_key is None) != (self.teacher_candidate_ordinal is None):
            raise OfflineCodecError("Teacher key and ordinal presence differ")
        if self.candidate_public_action_keys is not None and self.teacher_selected_public_action_key is not None:
            occurrences = tuple(
                index for index, key in enumerate(self.candidate_public_action_keys)
                if key == self.teacher_selected_public_action_key
            )
            if len(occurrences) == 1 and self.teacher_candidate_ordinal != occurrences[0]:
                if self.label_consistency != LABEL_FAIL:
                    raise OfflineCodecError("Teacher key and ordinal disagree without label failure")
        if self.decision_request_family is not None and self.decision_request_family not in task5.DECISION_KIND_TOKENS:
            raise OfflineCodecError("decision_request_family is not accepted")
        if self.phase is not None:
            _validate_u32(self.phase, "phase")
        if self.turn_index is not None:
            _validate_u64(self.turn_index, "turn_index")
        if self.acting_participant is not None and self.acting_participant not in (0, 1):
            raise OfflineCodecError("acting_participant is invalid")
        if self.locked_deck_role_id is not None and self.locked_deck_role_id not in (
            task5.SWORDSOUL_DECK_ID, task5.SALAMANGREAT_DECK_ID
        ):
            raise OfflineCodecError("locked_deck_role_id is invalid")
        if self.starting_player is not None and self.starting_player not in (0, 1):
            raise OfflineCodecError("starting_player is invalid")
        if self.continuation is not None:
            _validate_bool(self.continuation, "continuation")
        if self.rare_critical_slice is not None:
            _validate_lower_token(self.rare_critical_slice, "rare_critical_slice")
        if self.status not in (STATUS_SCORED, STATUS_REJECTED, STATUS_UNSCORED):
            raise OfflineCodecError("offline sample status is not accepted")
        if self.failure_reason is not None and self.failure_reason not in FAILURE_REASONS:
            raise OfflineCodecError("offline sample failure reason is not accepted")
        if self.label_consistency not in (LABEL_PASS, LABEL_FAIL, LABEL_NOT_RUN):
            raise OfflineCodecError("offline sample label-consistency status is not accepted")
        if self.teacher_key_consistency is not None:
            _validate_bool(self.teacher_key_consistency, "teacher_key_consistency")
        if self.teacher_ordinal_consistency is not None:
            _validate_bool(self.teacher_ordinal_consistency, "teacher_ordinal_consistency")
        if self.label_consistency == LABEL_PASS and (
            self.teacher_key_consistency is not True
            or self.teacher_ordinal_consistency is not True
        ):
            raise OfflineCodecError("label PASS is not paired with exact key/ordinal consistency")
        if self.top1_agreement is not None:
            _validate_bool(self.top1_agreement, "top1_agreement")
        if self.top_k_agreement is not None:
            _validate_bool(self.top_k_agreement, "top_k_agreement")
        if self.top_k is not None:
            if isinstance(self.top_k, bool) or not isinstance(self.top_k, int) or self.top_k < 1:
                raise OfflineCodecError("offline sample top-K is invalid")
            if self.candidate_count is not None and self.top_k > self.candidate_count:
                raise OfflineCodecError("offline sample top-K exceeds exact domain")
        if self.status == STATUS_SCORED:
            if self.failure_reason is not None or self.score_vector is None or self.score_vector_identity is None:
                raise OfflineCodecError("scored sample has incomplete score bundle")
            if self.candidate_public_action_keys is None or self.candidate_count is None:
                raise OfflineCodecError("scored sample has no exact candidate domain")
            if self.score_vector.public_action_keys != self.candidate_public_action_keys:
                raise OfflineCodecError("score vector changed candidate source order")
            self.score_vector.validate()
            if self.score_vector_identity != task5.score_vector_identity(self.score_vector):
                raise OfflineCodecError("score vector identity does not match payload")
            if self.loss_f64_bits is None:
                raise OfflineCodecError("scored sample has no exact loss bits")
            loss_f64_bytes(self.loss_f64_bits)
            if self.model_selected_public_action_key is None or self.model_candidate_ordinal is None:
                raise OfflineCodecError("scored sample has no model selection")
            selected = task5.select_score_vector(self.score_vector)
            if self.model_candidate_ordinal != selected or self.model_selected_public_action_key != self.candidate_public_action_keys[selected]:
                raise OfflineCodecError("model selection does not use the accepted tie rule")
            if self.top1_agreement is None or self.top1_agreement != (
                self.model_selected_public_action_key == self.teacher_selected_public_action_key
            ):
                raise OfflineCodecError("top-1 agreement is not derived from selected keys")
            if self.top_k is not None and self.top_k_agreement is None:
                raise OfflineCodecError("declared top-K has no result")
            if self.top_k is None and self.top_k_agreement is not None:
                raise OfflineCodecError("top-K result exists without declaration")
            if self.label_consistency != LABEL_PASS:
                raise OfflineCodecError("scored sample does not have a consistent Teacher label")
            if self.teacher_key_consistency is not True or self.teacher_ordinal_consistency is not True:
                raise OfflineCodecError("scored sample does not preserve exact Teacher key/ordinal consistency")
        else:
            if self.score_vector is not None or self.score_vector_identity is not None or self.loss_f64_bits is not None:
                raise OfflineCodecError("non-scored sample carries an authoritative score")
            if self.model_selected_public_action_key is not None or self.model_candidate_ordinal is not None or self.top1_agreement is not None:
                raise OfflineCodecError("non-scored sample carries model selection")
            if self.failure_reason is None:
                raise OfflineCodecError("non-scored sample has no typed failure reason")
            if self.top_k_agreement is not None:
                raise OfflineCodecError("non-scored sample carries a top-K result")
            if self.status == STATUS_REJECTED and self.label_consistency not in (LABEL_FAIL, LABEL_NOT_RUN):
                raise OfflineCodecError("rejected sample has invalid label status")
        if self.offline_sample_identity != _identity(OFFLINE_SAMPLE_ID_PREFIX, self._canonical_payload_bytes()):
            raise OfflineCodecError("offline sample identity is not canonical")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return self._payload(include_identity=True)

    @classmethod
    def from_dict(cls, payload: Any) -> "OfflineSampleResultV1":
        fields = (
            "schema_id", "evaluation_identity", "evaluation_contract_identity",
            "teacher_state_population_identity", "bc_sample_identity", "partition",
            "trajectory_record_id", "episode_semantic_id", "public_semantic_decision_id",
            "model_input_identity", "candidate_count", "ordered_candidate_domain_identity",
            "candidate_public_action_keys", "teacher_selected_public_action_key",
            "teacher_candidate_ordinal", "decision_request_family", "phase", "turn_index",
            "acting_participant", "locked_deck_role_id", "starting_player", "continuation",
            "rare_critical_slice", "status", "failure_reason", "score_vector",
            "score_vector_identity", "loss_f64_bits", "model_selected_public_action_key",
            "model_candidate_ordinal", "top1_agreement", "top_k", "top_k_agreement",
            "label_consistency", "teacher_key_consistency",
            "teacher_ordinal_consistency", "offline_sample_identity",
        )
        data = _strict_object(payload, fields, "OfflineSampleResultV1")
        try:
            score_vector = (
                None
                if data["score_vector"] is None
                else task5.ScoreVectorV1.from_dict(data["score_vector"])
            )
        except (task5.CodecError, TypeError, ValueError) as error:
            raise OfflineCodecError(str(error)) from error
        value = cls(
            schema_id=data["schema_id"],
            evaluation_identity=data["evaluation_identity"],
            evaluation_contract_identity=data["evaluation_contract_identity"],
            teacher_state_population_identity=data["teacher_state_population_identity"],
            bc_sample_identity=data["bc_sample_identity"],
            partition=data["partition"],
            trajectory_record_id=data["trajectory_record_id"],
            episode_semantic_id=data["episode_semantic_id"],
            public_semantic_decision_id=data["public_semantic_decision_id"],
            model_input_identity=data["model_input_identity"],
            candidate_count=data["candidate_count"],
            ordered_candidate_domain_identity=data["ordered_candidate_domain_identity"],
            candidate_public_action_keys=(
                tuple(data["candidate_public_action_keys"])
                if data["candidate_public_action_keys"] is not None else None
            ),
            teacher_selected_public_action_key=data["teacher_selected_public_action_key"],
            teacher_candidate_ordinal=data["teacher_candidate_ordinal"],
            decision_request_family=data["decision_request_family"],
            phase=data["phase"],
            turn_index=data["turn_index"],
            acting_participant=data["acting_participant"],
            locked_deck_role_id=data["locked_deck_role_id"],
            starting_player=data["starting_player"],
            continuation=data["continuation"],
            rare_critical_slice=data["rare_critical_slice"],
            status=data["status"],
            failure_reason=data["failure_reason"],
            score_vector=score_vector,
            score_vector_identity=data["score_vector_identity"],
            loss_f64_bits=data["loss_f64_bits"],
            model_selected_public_action_key=data["model_selected_public_action_key"],
            model_candidate_ordinal=data["model_candidate_ordinal"],
            top1_agreement=data["top1_agreement"],
            top_k=data["top_k"],
            top_k_agreement=data["top_k_agreement"],
            label_consistency=data["label_consistency"],
            teacher_key_consistency=data["teacher_key_consistency"],
            teacher_ordinal_consistency=data["teacher_ordinal_consistency"],
        )
        try:
            value.validate()
        except (task5.CodecError, TypeError, ValueError) as error:
            if isinstance(error, OfflineCodecError):
                raise
            raise OfflineCodecError(str(error)) from error
        if data["offline_sample_identity"] != value.offline_sample_identity:
            raise OfflineCodecError("offline sample identity does not match payload")
        return value


def _validate_result_order(results: Sequence[OfflineSampleResultV1]) -> None:
    if not isinstance(results, (tuple, list)):
        raise OfflineCodecError("offline sample results are not an ordered sequence")
    previous_partition = -1
    previous_identity: Optional[bytes] = None
    seen: set[str] = set()
    for result in results:
        result.validate()
        partition_index = PARTITION_ORDER.index(result.partition)
        if partition_index < previous_partition:
            raise OfflineCodecError("offline sample results are not validation-then-test")
        if partition_index != previous_partition:
            previous_identity = None
        if result.bc_sample_identity in seen:
            raise OfflineCodecError("offline sample results contain a duplicate sample")
        seen.add(result.bc_sample_identity)
        encoded = result.bc_sample_identity.encode("utf-8")
        if previous_identity is not None and encoded <= previous_identity:
            raise OfflineCodecError("offline sample results are not unsigned-UTF-8 ascending")
        previous_partition = partition_index
        previous_identity = encoded


def encode_offline_sample_jsonl(results: Sequence[OfflineSampleResultV1]) -> bytes:
    _validate_result_order(results)
    if not results:
        raise OfflineCodecError("offline sample stream cannot be empty")
    return task5.canonical_jsonl_bytes([result.to_dict() for result in results])


def decode_offline_sample_jsonl(data: bytes) -> tuple[OfflineSampleResultV1, ...]:
    try:
        payloads = task5.parse_canonical_jsonl(data)
    except task5.CodecError as error:
        raise OfflineCodecError(str(error)) from error
    values = tuple(OfflineSampleResultV1.from_dict(payload) for payload in payloads)
    if not values:
        raise OfflineCodecError("offline sample stream cannot be empty")
    _validate_result_order(values)
    return values


def encode_offline_sample_json(value: OfflineSampleResultV1) -> bytes:
    if not isinstance(value, OfflineSampleResultV1):
        raise OfflineCodecError("offline sample has the wrong DTO type")
    return task5.canonical_json_bytes(value.to_dict())


def decode_offline_sample_json(data: bytes) -> OfflineSampleResultV1:
    try:
        payload = task5.parse_canonical_json(data)
    except task5.CodecError as error:
        raise OfflineCodecError(str(error)) from error
    return OfflineSampleResultV1.from_dict(payload)


def _failure_result(
    *,
    source: OfflineSourceSampleV1,
    expected_authority: task4.CorpusSourceSampleAuthorityV1,
    evaluation_identity: str,
    evaluation_contract_identity: str,
    population_identity: str,
    expected_partition: str,
    reason: str,
    status: str,
    label_status: str = LABEL_NOT_RUN,
    candidate_info: Optional[tuple[int, tuple[str, ...], str]] = None,
    teacher_key: Optional[str] = None,
    teacher_ordinal: Optional[int] = None,
) -> OfflineSampleResultV1:
    context_fields = _accepted_context_fields(source.sample)
    candidate_count: Optional[int] = None
    domain_identity: Optional[str] = None
    keys: Optional[tuple[str, ...]] = None
    if candidate_info is not None:
        candidate_count, keys, domain_identity = candidate_info
    else:
        sample = source.sample
        if isinstance(sample.candidate_count if hasattr(sample, "candidate_count") else None, int):
            candidate_count = sample.candidate_count  # pragma: no cover - legacy DTO guard
        if (
            isinstance(sample.routing_keys, tuple)
            and sample.routing_keys
            and all(
                isinstance(key, str) and task5.is_public_action_key(key)
                for key in sample.routing_keys
            )
        ):
            keys = tuple(sample.routing_keys)
            candidate_count = len(keys)
        if keys is not None and isinstance(sample.ordered_candidate_domain_identity, str) and sample.ordered_candidate_domain_identity:
            domain_identity = sample.ordered_candidate_domain_identity
        else:
            domain_identity = None
    if keys is not None:
        try:
            if domain_identity is None:
                raise OfflineCodecError("domain is absent")
            if domain_identity.startswith(task5.ORDERED_CANDIDATE_DOMAIN_ID_PREFIX):
                task5.validate_ordered_candidate_domain_identity(
                    domain_identity, None, keys
                )
            else:
                _validate_digest(domain_identity, "ordered_candidate_domain_identity")
        except (task5.CodecError, OfflineCodecError, TypeError, ValueError):
            # An invalid domain must not be echoed as if it were an
            # authoritative identity.  The public key vector itself remains
            # useful audit data when its entries are individually canonical.
            domain_identity = None
    if keys is not None and len(set(keys)) != len(keys):
        # Preserve multiplicity for a rejected audit record, but never expose
        # a duplicate vector as a valid exact domain count.
        domain_identity = None
    safe_teacher_key = teacher_key if isinstance(teacher_key, str) and task5.is_public_action_key(teacher_key) else None
    safe_teacher_ordinal = teacher_ordinal if isinstance(teacher_ordinal, int) and not isinstance(teacher_ordinal, bool) and teacher_ordinal >= 0 else None
    if safe_teacher_key is None or safe_teacher_ordinal is None:
        safe_teacher_key = None
        safe_teacher_ordinal = None
    teacher_key_consistency: Optional[bool] = None
    teacher_ordinal_consistency: Optional[bool] = None
    if candidate_info is not None and safe_teacher_key is not None and safe_teacher_ordinal is not None:
        occurrences = tuple(
            index for index, key in enumerate(keys or ()) if key == safe_teacher_key
        )
        teacher_key_consistency = len(occurrences) == 1
        teacher_ordinal_consistency = (
            teacher_key_consistency and occurrences[0] == safe_teacher_ordinal
        )
    result = OfflineSampleResultV1(
        schema_id=OFFLINE_SAMPLE_SCHEMA_ID,
        evaluation_identity=evaluation_identity,
        evaluation_contract_identity=evaluation_contract_identity,
        teacher_state_population_identity=population_identity,
        bc_sample_identity=expected_authority.bc_sample_identity,
        partition=expected_partition,
        trajectory_record_id=expected_authority.trajectory_record_id,
        episode_semantic_id=expected_authority.episode_semantic_id,
        public_semantic_decision_id=expected_authority.public_semantic_decision_id,
        model_input_identity=expected_authority.model_input_identity,
        candidate_count=candidate_count,
        ordered_candidate_domain_identity=domain_identity,
        candidate_public_action_keys=keys,
        teacher_selected_public_action_key=safe_teacher_key,
        teacher_candidate_ordinal=safe_teacher_ordinal,
        status=status,
        failure_reason=reason,
        score_vector=None,
        score_vector_identity=None,
        loss_f64_bits=None,
        model_selected_public_action_key=None,
        model_candidate_ordinal=None,
        top1_agreement=None,
        top_k=None,
        top_k_agreement=None,
        label_consistency=label_status,
        teacher_key_consistency=teacher_key_consistency,
        teacher_ordinal_consistency=teacher_ordinal_consistency,
        **context_fields,
    )
    return result


def _slice_coordinates(result: OfflineSampleResultV1, kind: str, *, value: Optional[Any] = None) -> tuple[str, ...]:
    coordinates = {dimension: SLICE_COORDINATE_ABSENT for dimension in SLICE_DIMENSION_ORDER}
    if kind == "decision_request_family":
        coordinates["decision_request_family"] = result.decision_request_family or SLICE_COORDINATE_ABSENT
    elif kind in ("candidate_domain_size", "candidate_domain_witness"):
        coordinates["candidate_domain_size"] = str(value if value is not None else result.candidate_count)
    elif kind == "phase_decision_context":
        coordinates["phase"] = SLICE_COORDINATE_ABSENT if result.phase is None else str(result.phase)
        coordinates["turn_index"] = SLICE_COORDINATE_ABSENT if result.turn_index is None else str(result.turn_index)
    elif kind == "acting_participant_deck_role":
        coordinates["acting_participant"] = SLICE_COORDINATE_ABSENT if result.acting_participant is None else str(result.acting_participant)
        coordinates["locked_deck_role"] = result.locked_deck_role_id or SLICE_COORDINATE_ABSENT
    elif kind == "starting_player":
        coordinates["starting_player"] = SLICE_COORDINATE_ABSENT if result.starting_player is None else str(result.starting_player)
    elif kind == "continuation":
        coordinates["continuation"] = (
            SLICE_COORDINATE_ABSENT if result.continuation is None
            else ("continuation" if result.continuation else "non_continuation")
        )
    elif kind == "rare_critical":
        coordinates["rare_critical"] = result.rare_critical_slice or SLICE_COORDINATE_ABSENT
    else:
        raise OfflineCodecError("unknown slice kind")
    return tuple(coordinates[dimension] for dimension in SLICE_DIMENSION_ORDER)


def _result_metric_values(results: Sequence[OfflineSampleResultV1]) -> dict[str, Any]:
    scored = tuple(result for result in results if result.status == STATUS_SCORED)
    rejected = tuple(result for result in results if result.status == STATUS_REJECTED)
    unscored = tuple(result for result in results if result.status == STATUS_UNSCORED)
    failures = Counter(result.failure_reason for result in (*rejected, *unscored) if result.failure_reason is not None)
    losses = [loss_f64_value(result.loss_f64_bits) for result in scored if result.loss_f64_bits is not None]
    top1_values = [result.top1_agreement for result in scored if result.top1_agreement is not None]
    top_k_values = [result.top_k_agreement for result in scored if result.top_k_agreement is not None]
    label_values = [result.label_consistency for result in results if result.label_consistency != LABEL_NOT_RUN]
    key_values = [result.teacher_key_consistency for result in results if result.teacher_key_consistency is not None]
    ordinal_values = [result.teacher_ordinal_consistency for result in results if result.teacher_ordinal_consistency is not None]
    return {
        "total_count": len(results),
        "scored_count": len(scored),
        "rejected_count": len(rejected),
        "unscored_count": len(unscored),
        "loss_mean_f64_bits": (
            loss_f64_bits(math.fsum(loss / len(losses) for loss in losses))
            if losses else None
        ),
        "loss_denominator": len(losses),
        "top1_agreement_count": sum(value is True for value in top1_values),
        "top1_denominator": len(top1_values),
        "top_k_agreement_count": (sum(value is True for value in top_k_values) if top_k_values else None),
        "top_k_denominator": (len(top_k_values) if top_k_values else 0),
        "label_consistency_pass_count": sum(value == LABEL_PASS for value in label_values),
        "label_consistency_denominator": len(label_values),
        "teacher_key_consistency_count": sum(value is True for value in key_values),
        "teacher_key_consistency_denominator": len(key_values),
        "teacher_ordinal_consistency_count": sum(value is True for value in ordinal_values),
        "teacher_ordinal_consistency_denominator": len(ordinal_values),
        "failure_reason_counts": tuple(sorted(failures.items())),
        "capacity_failure_count": failures.get(FailureReason.CANDIDATE_CAPACITY_FAILURE, 0),
        "padding_mask_violation_count": failures.get(FailureReason.PADDING_MASK_VIOLATION, 0),
        "privacy_input_rejection_count": failures.get(FailureReason.PUBLIC_INPUT_REJECTION, 0),
        "label_mismatch_count": failures.get(FailureReason.LABEL_MISMATCH, 0),
    }


@dataclasses.dataclass(frozen=True)
class OfflineMetricsV1:
    schema_id: str
    evaluation_identity: str
    evaluation_contract_identity: str
    teacher_state_population_identity: str
    selected_partitions: tuple[str, ...]
    sample_result_identities: tuple[str, ...]
    total_count: int
    scored_count: int
    rejected_count: int
    unscored_count: int
    loss_mean_f64_bits: Optional[str]
    loss_denominator: int
    top1_agreement_count: int
    top1_denominator: int
    top_k: Optional[int]
    top_k_agreement_count: Optional[int]
    top_k_denominator: int
    label_consistency_pass_count: int
    label_consistency_denominator: int
    teacher_key_consistency_count: int
    teacher_key_consistency_denominator: int
    teacher_ordinal_consistency_count: int
    teacher_ordinal_consistency_denominator: int
    failure_reason_counts: tuple[tuple[str, int], ...]
    capacity_failure_count: int
    padding_mask_violation_count: int
    privacy_input_rejection_count: int
    label_mismatch_count: int

    @property
    def offline_metrics_identity(self) -> str:
        return _identity(OFFLINE_METRICS_ID_PREFIX, self._canonical_payload_bytes())

    def _payload(self, *, include_identity: bool) -> dict[str, Any]:
        payload: dict[str, Any] = {
            "capacity_failure_count": self.capacity_failure_count,
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_identity": self.evaluation_identity,
            "failure_reason_counts": [
                {"count": count, "reason": reason}
                for reason, count in self.failure_reason_counts
            ],
            "label_consistency_denominator": self.label_consistency_denominator,
            "label_consistency_pass_count": self.label_consistency_pass_count,
            "label_mismatch_count": self.label_mismatch_count,
            "loss_denominator": self.loss_denominator,
            "loss_mean_f64_bits": self.loss_mean_f64_bits,
            "padding_mask_violation_count": self.padding_mask_violation_count,
            "privacy_input_rejection_count": self.privacy_input_rejection_count,
            "rejected_count": self.rejected_count,
            "sample_result_identities": list(self.sample_result_identities),
            "schema_id": self.schema_id,
            "scored_count": self.scored_count,
            "selected_partitions": list(self.selected_partitions),
            "teacher_state_population_identity": self.teacher_state_population_identity,
            "teacher_key_consistency_count": self.teacher_key_consistency_count,
            "teacher_key_consistency_denominator": self.teacher_key_consistency_denominator,
            "teacher_ordinal_consistency_count": self.teacher_ordinal_consistency_count,
            "teacher_ordinal_consistency_denominator": self.teacher_ordinal_consistency_denominator,
            "top1_agreement_count": self.top1_agreement_count,
            "top1_denominator": self.top1_denominator,
            "top_k": self.top_k,
            "top_k_agreement_count": self.top_k_agreement_count,
            "top_k_denominator": self.top_k_denominator,
            "total_count": self.total_count,
            "unscored_count": self.unscored_count,
        }
        if include_identity:
            payload["offline_metrics_identity"] = self.offline_metrics_identity
        return payload

    def _canonical_payload_bytes(self) -> bytes:
        return task5.canonical_json_bytes(self._payload(include_identity=False))

    def validate(self) -> None:
        if self.schema_id != OFFLINE_METRICS_SCHEMA_ID:
            raise OfflineCodecError("offline metrics schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_contract_identity, task5.EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        if self.evaluation_contract_identity != task5.evaluation_contract_identity():
            raise OfflineCodecError("offline metrics contract identity is not accepted")
        _validate_prefixed(self.teacher_state_population_identity, TEACHER_STATE_POPULATION_ID_PREFIX, "teacher_state_population_identity")
        partitions = _validate_partitions(self.selected_partitions)
        if partitions != self.selected_partitions:
            raise OfflineCodecError("metrics partition vector is not canonical")
        counts = (
            self.total_count, self.scored_count, self.rejected_count, self.unscored_count,
            self.loss_denominator, self.top1_agreement_count, self.top1_denominator,
            self.top_k_denominator, self.label_consistency_pass_count,
            self.label_consistency_denominator, self.capacity_failure_count,
            self.padding_mask_violation_count, self.privacy_input_rejection_count,
            self.label_mismatch_count, self.teacher_key_consistency_count,
            self.teacher_key_consistency_denominator,
            self.teacher_ordinal_consistency_count,
            self.teacher_ordinal_consistency_denominator,
        )
        if any(isinstance(value, bool) or not isinstance(value, int) or value < 0 for value in counts):
            raise OfflineCodecError("offline metrics count is invalid")
        if self.total_count != self.scored_count + self.rejected_count + self.unscored_count:
            raise OfflineCodecError("offline metric count conservation failed")
        if self.loss_denominator != self.scored_count:
            raise OfflineCodecError("loss denominator does not equal scored count")
        if self.loss_mean_f64_bits is None:
            if self.loss_denominator != 0:
                raise OfflineCodecError("loss bits are absent with a nonzero denominator")
        else:
            loss_f64_bytes(self.loss_mean_f64_bits)
            if self.loss_denominator == 0:
                raise OfflineCodecError("loss bits exist with a zero denominator")
        if self.top1_denominator != self.scored_count or self.top1_agreement_count > self.top1_denominator:
            raise OfflineCodecError("top-1 denominator/count is inconsistent")
        if self.top_k is None:
            if self.top_k_agreement_count is not None or self.top_k_denominator != 0:
                raise OfflineCodecError("top-K counts exist without a declaration")
        else:
            if isinstance(self.top_k, bool) or not isinstance(self.top_k, int) or self.top_k < 1:
                raise OfflineCodecError("metrics top-K is invalid")
            if (
                self.top_k_agreement_count is None
                or isinstance(self.top_k_agreement_count, bool)
                or not isinstance(self.top_k_agreement_count, int)
                or self.top_k_agreement_count < 0
                or self.top_k_denominator != self.scored_count
                or self.top_k_agreement_count > self.top_k_denominator
            ):
                raise OfflineCodecError("top-K counts are inconsistent")
        if self.label_consistency_pass_count > self.label_consistency_denominator:
            raise OfflineCodecError("label consistency counts are inconsistent")
        if (
            self.teacher_key_consistency_count > self.teacher_key_consistency_denominator
            or self.teacher_ordinal_consistency_count > self.teacher_ordinal_consistency_denominator
        ):
            raise OfflineCodecError("Teacher key/ordinal consistency counts are inconsistent")
        previous_reason = None
        reason_total = 0
        for reason, count in self.failure_reason_counts:
            if reason not in FAILURE_REASONS or previous_reason is not None and reason <= previous_reason:
                raise OfflineCodecError("failure-reason vector is not sorted and unique")
            if isinstance(count, bool) or not isinstance(count, int) or count <= 0:
                raise OfflineCodecError("failure-reason count is invalid")
            previous_reason = reason
            reason_total += count
        if reason_total != self.rejected_count + self.unscored_count:
            raise OfflineCodecError("failure reasons do not reconcile with non-scored results")
        if not isinstance(self.sample_result_identities, tuple) or len(self.sample_result_identities) != self.total_count:
            raise OfflineCodecError("sample-result identity vector is incomplete")
        for identity in self.sample_result_identities:
            _validate_prefixed(identity, OFFLINE_SAMPLE_ID_PREFIX, "sample_result_identity")
        if len(set(self.sample_result_identities)) != len(self.sample_result_identities):
            raise OfflineCodecError("sample-result identity vector is duplicated")
        if self.offline_metrics_identity != _identity(OFFLINE_METRICS_ID_PREFIX, self._canonical_payload_bytes()):
            raise OfflineCodecError("offline metrics identity is not canonical")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return self._payload(include_identity=True)

    @classmethod
    def from_dict(cls, payload: Any) -> "OfflineMetricsV1":
        fields = (
            "schema_id", "evaluation_identity", "evaluation_contract_identity",
            "teacher_state_population_identity", "selected_partitions",
            "sample_result_identities", "total_count", "scored_count",
            "rejected_count", "unscored_count", "loss_mean_f64_bits",
            "loss_denominator", "top1_agreement_count", "top1_denominator", "top_k",
            "top_k_agreement_count", "top_k_denominator", "label_consistency_pass_count",
            "label_consistency_denominator", "failure_reason_counts",
            "teacher_key_consistency_count", "teacher_key_consistency_denominator",
            "teacher_ordinal_consistency_count", "teacher_ordinal_consistency_denominator",
            "capacity_failure_count", "padding_mask_violation_count",
            "privacy_input_rejection_count", "label_mismatch_count",
            "offline_metrics_identity",
        )
        data = _strict_object(payload, fields, "OfflineMetricsV1")
        counts: list[tuple[str, int]] = []
        if not isinstance(data["failure_reason_counts"], list):
            raise OfflineCodecError("failure_reason_counts is not a vector")
        for entry in data["failure_reason_counts"]:
            item = _strict_object(entry, ("reason", "count"), "failure reason")
            counts.append((item["reason"], item["count"]))
        value = cls(
            schema_id=data["schema_id"],
            evaluation_identity=data["evaluation_identity"],
            evaluation_contract_identity=data["evaluation_contract_identity"],
            teacher_state_population_identity=data["teacher_state_population_identity"],
            selected_partitions=tuple(data["selected_partitions"]),
            sample_result_identities=tuple(data["sample_result_identities"]),
            total_count=data["total_count"],
            scored_count=data["scored_count"],
            rejected_count=data["rejected_count"],
            unscored_count=data["unscored_count"],
            loss_mean_f64_bits=data["loss_mean_f64_bits"],
            loss_denominator=data["loss_denominator"],
            top1_agreement_count=data["top1_agreement_count"],
            top1_denominator=data["top1_denominator"],
            top_k=data["top_k"],
            top_k_agreement_count=data["top_k_agreement_count"],
            top_k_denominator=data["top_k_denominator"],
            label_consistency_pass_count=data["label_consistency_pass_count"],
            label_consistency_denominator=data["label_consistency_denominator"],
            teacher_key_consistency_count=data["teacher_key_consistency_count"],
            teacher_key_consistency_denominator=data["teacher_key_consistency_denominator"],
            teacher_ordinal_consistency_count=data["teacher_ordinal_consistency_count"],
            teacher_ordinal_consistency_denominator=data["teacher_ordinal_consistency_denominator"],
            failure_reason_counts=tuple(counts),
            capacity_failure_count=data["capacity_failure_count"],
            padding_mask_violation_count=data["padding_mask_violation_count"],
            privacy_input_rejection_count=data["privacy_input_rejection_count"],
            label_mismatch_count=data["label_mismatch_count"],
        )
        try:
            value.validate()
        except (task5.CodecError, TypeError, ValueError) as error:
            if isinstance(error, OfflineCodecError):
                raise
            raise OfflineCodecError(str(error)) from error
        if data["offline_metrics_identity"] != value.offline_metrics_identity:
            raise OfflineCodecError("offline metrics identity does not match payload")
        return value


def aggregate_offline_metrics(
    results: Sequence[OfflineSampleResultV1],
    *,
    evaluation_identity: str,
    evaluation_contract_identity: str,
    teacher_state_population_identity: str,
    selected_partitions: Sequence[str],
    top_k: Optional[int],
) -> OfflineMetricsV1:
    _validate_result_order(results)
    partitions = _validate_partitions(selected_partitions)
    if any(result.evaluation_identity != evaluation_identity for result in results):
        raise OfflineCodecError("metrics received a result from another evaluation")
    if any(result.evaluation_contract_identity != evaluation_contract_identity for result in results):
        raise OfflineCodecError("metrics received a result from another contract")
    if any(result.teacher_state_population_identity != teacher_state_population_identity for result in results):
        raise OfflineCodecError("metrics received a result from another population")
    values = _result_metric_values(results)
    if top_k is not None and values["top_k_agreement_count"] is None:
        values["top_k_agreement_count"] = 0
        values["top_k_denominator"] = 0
    metrics = OfflineMetricsV1(
        schema_id=OFFLINE_METRICS_SCHEMA_ID,
        evaluation_identity=evaluation_identity,
        evaluation_contract_identity=evaluation_contract_identity,
        teacher_state_population_identity=teacher_state_population_identity,
        selected_partitions=partitions,
        sample_result_identities=tuple(result.offline_sample_identity for result in results),
        top_k=top_k,
        **values,
    )
    metrics.validate()
    return metrics


@dataclasses.dataclass(frozen=True)
class OfflineSliceResultV1:
    schema_id: str
    evaluation_identity: str
    evaluation_contract_identity: str
    teacher_state_population_identity: str
    dimension_contract_id: str
    slice_kind: str
    coordinates: tuple[str, ...]
    member_sample_result_identities: tuple[str, ...]
    presence: str
    total_count: int
    scored_count: int
    rejected_count: int
    unscored_count: int
    loss_mean_f64_bits: Optional[str]
    loss_denominator: int
    top1_agreement_count: int
    top1_denominator: int
    top_k: Optional[int]
    top_k_agreement_count: Optional[int]
    top_k_denominator: int
    label_consistency_pass_count: int
    label_consistency_denominator: int
    teacher_key_consistency_count: int
    teacher_key_consistency_denominator: int
    teacher_ordinal_consistency_count: int
    teacher_ordinal_consistency_denominator: int
    failure_reason_counts: tuple[tuple[str, int], ...]
    capacity_failure_count: int
    padding_mask_violation_count: int
    privacy_input_rejection_count: int
    label_mismatch_count: int

    @property
    def offline_slice_identity(self) -> str:
        return _identity(OFFLINE_SLICE_ID_PREFIX, self._canonical_payload_bytes())

    def _payload(self, *, include_identity: bool) -> dict[str, Any]:
        payload = {
            "capacity_failure_count": self.capacity_failure_count,
            "coordinates": list(self.coordinates),
            "dimension_contract_id": self.dimension_contract_id,
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_identity": self.evaluation_identity,
            "failure_reason_counts": [
                {"count": count, "reason": reason} for reason, count in self.failure_reason_counts
            ],
            "label_consistency_denominator": self.label_consistency_denominator,
            "label_consistency_pass_count": self.label_consistency_pass_count,
            "label_mismatch_count": self.label_mismatch_count,
            "loss_denominator": self.loss_denominator,
            "loss_mean_f64_bits": self.loss_mean_f64_bits,
            "member_sample_result_identities": list(self.member_sample_result_identities),
            "padding_mask_violation_count": self.padding_mask_violation_count,
            "presence": self.presence,
            "privacy_input_rejection_count": self.privacy_input_rejection_count,
            "rejected_count": self.rejected_count,
            "schema_id": self.schema_id,
            "scored_count": self.scored_count,
            "slice_kind": self.slice_kind,
            "teacher_state_population_identity": self.teacher_state_population_identity,
            "teacher_key_consistency_count": self.teacher_key_consistency_count,
            "teacher_key_consistency_denominator": self.teacher_key_consistency_denominator,
            "teacher_ordinal_consistency_count": self.teacher_ordinal_consistency_count,
            "teacher_ordinal_consistency_denominator": self.teacher_ordinal_consistency_denominator,
            "top1_agreement_count": self.top1_agreement_count,
            "top1_denominator": self.top1_denominator,
            "top_k": self.top_k,
            "top_k_agreement_count": self.top_k_agreement_count,
            "top_k_denominator": self.top_k_denominator,
            "total_count": self.total_count,
            "unscored_count": self.unscored_count,
        }
        if include_identity:
            payload["offline_slice_identity"] = self.offline_slice_identity
        return payload

    def _canonical_payload_bytes(self) -> bytes:
        return task5.canonical_json_bytes(self._payload(include_identity=False))

    def validate(self) -> None:
        if self.schema_id != OFFLINE_SLICE_SCHEMA_ID:
            raise OfflineCodecError("offline slice schema is not accepted")
        _validate_prefixed(self.evaluation_identity, task5.EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_prefixed(self.evaluation_contract_identity, task5.EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        if self.evaluation_contract_identity != task5.evaluation_contract_identity():
            raise OfflineCodecError("offline slice contract identity is not accepted")
        _validate_prefixed(self.teacher_state_population_identity, TEACHER_STATE_POPULATION_ID_PREFIX, "teacher_state_population_identity")
        if self.dimension_contract_id != SLICE_DIMENSION_CONTRACT_ID:
            raise OfflineCodecError("slice dimension contract is not accepted")
        if self.slice_kind not in SLICE_KIND_ORDER:
            raise OfflineCodecError("slice kind is not accepted")
        if not isinstance(self.coordinates, tuple) or len(self.coordinates) != len(SLICE_DIMENSION_ORDER):
            raise OfflineCodecError("slice coordinates do not use the fixed dimension order")
        for coordinate in self.coordinates:
            _validate_string(coordinate, "slice coordinate")
        index_by_dimension = {
            dimension: index for index, dimension in enumerate(SLICE_DIMENSION_ORDER)
        }
        active_dimensions = {
            "decision_request_family": ("decision_request_family",),
            "candidate_domain_size": ("candidate_domain_size",),
            "candidate_domain_witness": ("candidate_domain_size",),
            "phase_decision_context": ("phase", "turn_index"),
            "acting_participant_deck_role": ("acting_participant", "locked_deck_role"),
            "starting_player": ("starting_player",),
            "continuation": ("continuation",),
            "rare_critical": ("rare_critical",),
        }[self.slice_kind]
        for dimension in SLICE_DIMENSION_ORDER:
            coordinate = self.coordinates[index_by_dimension[dimension]]
            if dimension not in active_dimensions and coordinate != SLICE_COORDINATE_ABSENT:
                raise OfflineCodecError("slice contains a coordinate outside its dimension")
        if self.slice_kind == "candidate_domain_witness":
            if self.coordinates[index_by_dimension["candidate_domain_size"]] not in {"24", "25", "129"}:
                raise OfflineCodecError("slice witness is not one of N=24/N=25/N=129")
        if self.presence not in (SLICE_PRESENT, SLICE_NOT_PRESENT):
            raise OfflineCodecError("slice presence is not accepted")
        if self.presence == SLICE_NOT_PRESENT and self.total_count != 0:
            raise OfflineCodecError("absent slice has members")
        if not isinstance(self.member_sample_result_identities, tuple):
            raise OfflineCodecError("slice member vector is not ordered")
        for identity in self.member_sample_result_identities:
            _validate_prefixed(identity, OFFLINE_SAMPLE_ID_PREFIX, "slice member identity")
        if len(set(self.member_sample_result_identities)) != len(self.member_sample_result_identities):
            raise OfflineCodecError("slice member vector contains duplicates")
        metric = OfflineMetricsV1(
            schema_id=OFFLINE_METRICS_SCHEMA_ID,
            evaluation_identity=self.evaluation_identity,
            evaluation_contract_identity=self.evaluation_contract_identity,
            teacher_state_population_identity=self.teacher_state_population_identity,
            selected_partitions=PARTITION_ORDER,
            sample_result_identities=self.member_sample_result_identities,
            total_count=self.total_count,
            scored_count=self.scored_count,
            rejected_count=self.rejected_count,
            unscored_count=self.unscored_count,
            loss_mean_f64_bits=self.loss_mean_f64_bits,
            loss_denominator=self.loss_denominator,
            top1_agreement_count=self.top1_agreement_count,
            top1_denominator=self.top1_denominator,
            top_k=self.top_k,
            top_k_agreement_count=self.top_k_agreement_count,
            top_k_denominator=self.top_k_denominator,
            label_consistency_pass_count=self.label_consistency_pass_count,
            label_consistency_denominator=self.label_consistency_denominator,
            teacher_key_consistency_count=self.teacher_key_consistency_count,
            teacher_key_consistency_denominator=self.teacher_key_consistency_denominator,
            teacher_ordinal_consistency_count=self.teacher_ordinal_consistency_count,
            teacher_ordinal_consistency_denominator=self.teacher_ordinal_consistency_denominator,
            failure_reason_counts=self.failure_reason_counts,
            capacity_failure_count=self.capacity_failure_count,
            padding_mask_violation_count=self.padding_mask_violation_count,
            privacy_input_rejection_count=self.privacy_input_rejection_count,
            label_mismatch_count=self.label_mismatch_count,
        )
        metric.validate()
        if self.offline_slice_identity != _identity(OFFLINE_SLICE_ID_PREFIX, self._canonical_payload_bytes()):
            raise OfflineCodecError("offline slice identity is not canonical")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return self._payload(include_identity=True)

    @classmethod
    def from_dict(cls, payload: Any) -> "OfflineSliceResultV1":
        fields = (
            "schema_id", "evaluation_identity", "evaluation_contract_identity",
            "teacher_state_population_identity", "dimension_contract_id", "slice_kind",
            "coordinates", "member_sample_result_identities", "presence", "total_count",
            "scored_count", "rejected_count", "unscored_count", "loss_mean_f64_bits",
            "loss_denominator", "top1_agreement_count", "top1_denominator", "top_k",
            "top_k_agreement_count", "top_k_denominator", "label_consistency_pass_count",
            "label_consistency_denominator", "failure_reason_counts",
            "teacher_key_consistency_count", "teacher_key_consistency_denominator",
            "teacher_ordinal_consistency_count", "teacher_ordinal_consistency_denominator",
            "capacity_failure_count", "padding_mask_violation_count",
            "privacy_input_rejection_count", "label_mismatch_count", "offline_slice_identity",
        )
        data = _strict_object(payload, fields, "OfflineSliceResultV1")
        counts = []
        if not isinstance(data["failure_reason_counts"], list):
            raise OfflineCodecError("slice failure_reason_counts is not a vector")
        for entry in data["failure_reason_counts"]:
            item = _strict_object(entry, ("reason", "count"), "slice failure reason")
            counts.append((item["reason"], item["count"]))
        value = cls(
            schema_id=data["schema_id"],
            evaluation_identity=data["evaluation_identity"],
            evaluation_contract_identity=data["evaluation_contract_identity"],
            teacher_state_population_identity=data["teacher_state_population_identity"],
            dimension_contract_id=data["dimension_contract_id"],
            slice_kind=data["slice_kind"],
            coordinates=tuple(data["coordinates"]),
            member_sample_result_identities=tuple(data["member_sample_result_identities"]),
            presence=data["presence"],
            total_count=data["total_count"],
            scored_count=data["scored_count"],
            rejected_count=data["rejected_count"],
            unscored_count=data["unscored_count"],
            loss_mean_f64_bits=data["loss_mean_f64_bits"],
            loss_denominator=data["loss_denominator"],
            top1_agreement_count=data["top1_agreement_count"],
            top1_denominator=data["top1_denominator"],
            top_k=data["top_k"],
            top_k_agreement_count=data["top_k_agreement_count"],
            top_k_denominator=data["top_k_denominator"],
            label_consistency_pass_count=data["label_consistency_pass_count"],
            label_consistency_denominator=data["label_consistency_denominator"],
            teacher_key_consistency_count=data["teacher_key_consistency_count"],
            teacher_key_consistency_denominator=data["teacher_key_consistency_denominator"],
            teacher_ordinal_consistency_count=data["teacher_ordinal_consistency_count"],
            teacher_ordinal_consistency_denominator=data["teacher_ordinal_consistency_denominator"],
            failure_reason_counts=tuple(counts),
            capacity_failure_count=data["capacity_failure_count"],
            padding_mask_violation_count=data["padding_mask_violation_count"],
            privacy_input_rejection_count=data["privacy_input_rejection_count"],
            label_mismatch_count=data["label_mismatch_count"],
        )
        try:
            value.validate()
        except (task5.CodecError, TypeError, ValueError) as error:
            if isinstance(error, OfflineCodecError):
                raise
            raise OfflineCodecError(str(error)) from error
        if data["offline_slice_identity"] != value.offline_slice_identity:
            raise OfflineCodecError("offline slice identity does not match payload")
        return value


def _make_slice(
    members: Sequence[OfflineSampleResultV1],
    *,
    evaluation_identity: str,
    evaluation_contract_identity: str,
    population_identity: str,
    kind: str,
    coordinates: tuple[str, ...],
    top_k: Optional[int],
) -> OfflineSliceResultV1:
    values = _result_metric_values(members)
    if top_k is not None and values["top_k_agreement_count"] is None:
        values["top_k_agreement_count"] = 0
        values["top_k_denominator"] = 0
    result = OfflineSliceResultV1(
        schema_id=OFFLINE_SLICE_SCHEMA_ID,
        evaluation_identity=evaluation_identity,
        evaluation_contract_identity=evaluation_contract_identity,
        teacher_state_population_identity=population_identity,
        dimension_contract_id=SLICE_DIMENSION_CONTRACT_ID,
        slice_kind=kind,
        coordinates=coordinates,
        member_sample_result_identities=tuple(result.offline_sample_identity for result in members),
        presence=SLICE_PRESENT if members else SLICE_NOT_PRESENT,
        top_k=top_k,
        **values,
    )
    result.validate()
    return result


def derive_offline_slices(
    results: Sequence[OfflineSampleResultV1],
    *,
    evaluation_identity: str,
    evaluation_contract_identity: str,
    teacher_state_population_identity: str,
    top_k: Optional[int],
) -> tuple[OfflineSliceResultV1, ...]:
    _validate_result_order(results)
    groups: dict[tuple[str, tuple[str, ...]], list[OfflineSampleResultV1]] = defaultdict(list)

    def add(kind: str, coordinates: tuple[str, ...], members: Iterable[OfflineSampleResultV1]) -> None:
        groups[(kind, coordinates)].extend(members)

    by_family: dict[str, list[OfflineSampleResultV1]] = defaultdict(list)
    by_size: dict[int, list[OfflineSampleResultV1]] = defaultdict(list)
    by_phase: dict[tuple[str, str], list[OfflineSampleResultV1]] = defaultdict(list)
    by_acting: dict[tuple[str, str], list[OfflineSampleResultV1]] = defaultdict(list)
    by_start: dict[str, list[OfflineSampleResultV1]] = defaultdict(list)
    by_cont: dict[str, list[OfflineSampleResultV1]] = defaultdict(list)
    by_rare: dict[str, list[OfflineSampleResultV1]] = defaultdict(list)
    for result in results:
        by_family[result.decision_request_family or SLICE_COORDINATE_ABSENT].append(result)
        if result.candidate_count is not None:
            by_size[result.candidate_count].append(result)
        by_phase[(SLICE_COORDINATE_ABSENT if result.phase is None else str(result.phase), SLICE_COORDINATE_ABSENT if result.turn_index is None else str(result.turn_index))].append(result)
        by_acting[(SLICE_COORDINATE_ABSENT if result.acting_participant is None else str(result.acting_participant), result.locked_deck_role_id or SLICE_COORDINATE_ABSENT)].append(result)
        by_start[SLICE_COORDINATE_ABSENT if result.starting_player is None else str(result.starting_player)].append(result)
        by_cont[SLICE_COORDINATE_ABSENT if result.continuation is None else ("continuation" if result.continuation else "non_continuation")].append(result)
        by_rare[result.rare_critical_slice or SLICE_COORDINATE_ABSENT].append(result)

    for key, members in by_family.items():
        add("decision_request_family", _slice_coordinates(members[0], "decision_request_family"), members)
    for key, members in by_size.items():
        add("candidate_domain_size", _slice_coordinates(members[0], "candidate_domain_size", value=key), members)
    for witness in (24, 25, 129):
        members = by_size.get(witness, [])
        coordinate_source = members[0] if members else results[0] if results else None
        if coordinate_source is not None:
            add("candidate_domain_witness", _slice_coordinates(coordinate_source, "candidate_domain_witness", value=witness), members)
        else:
            coordinates = tuple(
                str(witness) if dimension == "candidate_domain_size" else SLICE_COORDINATE_ABSENT
                for dimension in SLICE_DIMENSION_ORDER
            )
            add("candidate_domain_witness", coordinates, ())
    for (phase, turn), members in by_phase.items():
        add("phase_decision_context", _slice_coordinates(members[0], "phase_decision_context"), members)
    for key, members in by_acting.items():
        add("acting_participant_deck_role", _slice_coordinates(members[0], "acting_participant_deck_role"), members)
    for key, members in by_start.items():
        add("starting_player", _slice_coordinates(members[0], "starting_player"), members)
    for key, members in by_cont.items():
        add("continuation", _slice_coordinates(members[0], "continuation"), members)
    for key, members in by_rare.items():
        add("rare_critical", _slice_coordinates(members[0], "rare_critical"), members)

    ordered: list[OfflineSliceResultV1] = []
    for kind in SLICE_KIND_ORDER:
        entries = [
            (coordinates, tuple(sorted(members, key=lambda item: item.bc_sample_identity.encode("utf-8"))))
            for (entry_kind, coordinates), members in groups.items()
            if entry_kind == kind
        ]
        entries.sort(key=lambda item: tuple(value.encode("utf-8") for value in item[0]))
        for coordinates, members in entries:
            ordered.append(
                _make_slice(
                    members,
                    evaluation_identity=evaluation_identity,
                    evaluation_contract_identity=evaluation_contract_identity,
                    population_identity=teacher_state_population_identity,
                    kind=kind,
                    coordinates=coordinates,
                    top_k=top_k,
                )
            )
    return tuple(ordered)


def _expected_partition_map(authority: task4.CorpusAdmissionAuthorityV1) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for partition, episodes in zip(
        ("validation", "test"),
        (authority.validation_episode_ids, authority.test_episode_ids),
    ):
        for episode in episodes:
            if episode in mapping:
                raise OfflineEvaluationError("INVALID_SPLIT", "episode belongs to multiple evaluation partitions")
            mapping[episode] = partition
    return mapping


def _canonical_eval_sources(
    population: TrustedOfflinePopulationV1,
    selected_partitions: Sequence[str],
) -> tuple[tuple[OfflineSourceSampleV1, task4.CorpusSourceSampleAuthorityV1, str], ...]:
    partition_map = _expected_partition_map(population.admission_authority)
    expected_authority = {
        sample.bc_sample_identity: sample for sample in population.admission_authority.source_samples
    }
    sources = {source.sample.bc_sample_identity: source for source in population.source_samples}
    selected = set(_validate_partitions(selected_partitions))
    entries = [
        (sources[identity], authority_sample, partition_map[authority_sample.episode_semantic_id])
        for identity, authority_sample in expected_authority.items()
        if partition_map.get(authority_sample.episode_semantic_id) in selected
    ]
    entries.sort(key=lambda item: (PARTITION_ORDER.index(item[2]), item[1].bc_sample_identity.encode("utf-8")))
    return tuple(entries)


def _build_teacher_population_identity(
    entries: Sequence[tuple[OfflineSourceSampleV1, task4.CorpusSourceSampleAuthorityV1, str]],
    *,
    population: TrustedOfflinePopulationV1,
    selected_partitions: Sequence[str],
) -> str:
    # The shared population identity input is an ordered validation-then-test
    # vector.  Each partition is sorted independently, so the vector is
    # exactly the same order as the authoritative JSONL sample stream.
    ids = tuple(entry[1].bc_sample_identity for entry in entries)
    return teacher_state_population_identity(
        source_dataset_identity=population.source_dataset_identity,
        dataset_split_identity=population.dataset_split_identity,
        selected_partitions=selected_partitions,
        evaluation_contract_identity=population.evaluation_contract_identity,
        ordered_bc_sample_identities=ids,
        partition_sample_counts=tuple(
            sum(1 for entry in entries if entry[2] == partition)
            for partition in selected_partitions
        ),
    )


def _make_scored_result(
    *,
    source: OfflineSourceSampleV1,
    authority_sample: task4.CorpusSourceSampleAuthorityV1,
    partition: str,
    evaluation_identity: str,
    evaluation_contract_identity: str,
    population_identity: str,
    candidate_info: tuple[int, tuple[str, ...], str],
    teacher_ordinal: int,
    batch: OfflineScoreBatchV1,
    top_k: Optional[int],
) -> OfflineSampleResultV1:
    n, keys, domain_identity = candidate_info
    score_vector = task5.ScoreVectorV1(
        public_action_keys=tuple(keys),
        score_f32_bits=tuple(batch.score_f32_bits[:n]),
    )
    score_vector.validate()
    model_ordinal = task5.select_score_vector(score_vector)
    loss, loss_bits = exact_domain_loss(score_vector.score_f32_bits, teacher_ordinal)
    del loss
    top_k_agreement = None
    if top_k is not None:
        ordered = sorted(
            range(n),
            key=lambda index: (
                -task5.score_f32_value(score_vector.score_f32_bits[index]),
                score_vector.public_action_keys[index].encode("utf-8"),
            ),
        )
        top_k_agreement = teacher_ordinal in ordered[:top_k]
    fields = _accepted_context_fields(source.sample)
    result = OfflineSampleResultV1(
        schema_id=OFFLINE_SAMPLE_SCHEMA_ID,
        evaluation_identity=evaluation_identity,
        evaluation_contract_identity=evaluation_contract_identity,
        teacher_state_population_identity=population_identity,
        bc_sample_identity=authority_sample.bc_sample_identity,
        partition=partition,
        trajectory_record_id=authority_sample.trajectory_record_id,
        episode_semantic_id=authority_sample.episode_semantic_id,
        public_semantic_decision_id=authority_sample.public_semantic_decision_id,
        model_input_identity=authority_sample.model_input_identity,
        candidate_count=n,
        ordered_candidate_domain_identity=domain_identity,
        candidate_public_action_keys=tuple(keys),
        teacher_selected_public_action_key=source.sample.selected_public_action_key,
        teacher_candidate_ordinal=teacher_ordinal,
        status=STATUS_SCORED,
        failure_reason=None,
        score_vector=score_vector,
        score_vector_identity=task5.score_vector_identity(score_vector),
        loss_f64_bits=loss_bits,
        model_selected_public_action_key=score_vector.public_action_keys[model_ordinal],
        model_candidate_ordinal=model_ordinal,
        top1_agreement=(score_vector.public_action_keys[model_ordinal] == source.sample.selected_public_action_key),
        top_k=top_k,
        top_k_agreement=top_k_agreement,
        label_consistency=LABEL_PASS,
        teacher_key_consistency=True,
        teacher_ordinal_consistency=True,
        **fields,
    )
    result.validate()
    return result


def evaluate_offline(
    population: TrustedOfflinePopulationV1,
    config: OfflineEvaluationConfigV1,
    score_provider: Callable[[task4.CorpusSampleV1, int], Any],
) -> "OfflineEvaluationResultV1":
    """Evaluate exactly the admitted validation/test sample population.

    The function returns one result for every eligible source sample.  Input
    and admission errors become typed rejected/unscored results when the
    sample remains a member of the trusted population; population-level
    membership or configuration errors raise ``OfflineEvaluationError`` and
    no scoring is attempted.
    """

    if not callable(score_provider):
        raise OfflineEvaluationError("INVALID_SCORER", "score provider is not callable")
    config.validate()
    population.validate()
    if population.source_dataset_identity != config.source_dataset_identity:
        raise OfflineEvaluationError("INVALID_DATASET_MANIFEST", "evaluation dataset identity differs from population")
    if population.dataset_split_identity != config.dataset_split_identity:
        raise OfflineEvaluationError("INVALID_SPLIT", "evaluation split identity differs from population")
    entries = _canonical_eval_sources(population, config.selected_partitions)
    if not entries:
        raise OfflineEvaluationError("EMPTY_EVALUATION_POPULATION", "selected evaluation population is empty")
    if config.top_k is not None:
        for source, _, _ in entries:
            if not isinstance(source.sample.routing_keys, tuple) or config.top_k > len(source.sample.routing_keys):
                raise OfflineEvaluationError("INVALID_TOP_K", "declared top-K exceeds a source domain")
    population_identity = _build_teacher_population_identity(
        entries, population=population, selected_partitions=config.selected_partitions
    )
    expected_checkpoint_identity = config.evaluation_context.root.checkpoint_identity
    results: list[OfflineSampleResultV1] = []
    for source, authority_sample, partition in entries:
        sample = source.sample
        if sample.partition != partition:
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=FailureReason.SPLIT_LEAKAGE,
                    status=STATUS_REJECTED,
                )
            )
            continue
        try:
            candidate_info = _candidate_domain_validation(source)
        except OfflineEvaluationError as error:
            label = LABEL_FAIL if error.code == FailureReason.LABEL_MISMATCH else LABEL_NOT_RUN
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=error.code,
                    status=STATUS_REJECTED,
                    label_status=label,
                )
            )
            continue
        n, keys, domain_identity = candidate_info
        try:
            teacher_ordinal = _label_validation(sample, keys)
        except OfflineEvaluationError as error:
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=error.code,
                    status=STATUS_REJECTED,
                    label_status=LABEL_FAIL,
                    candidate_info=candidate_info,
                    teacher_key=sample.selected_public_action_key,
                    teacher_ordinal=sample.candidate_ordinal if isinstance(sample.candidate_ordinal, int) else None,
                )
            )
            continue
        physical_width = config.physical_candidate_capacity
        if physical_width is None:
            physical_width = n
        if physical_width < n:
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=FailureReason.CANDIDATE_CAPACITY_FAILURE,
                    status=STATUS_UNSCORED,
                    label_status=LABEL_PASS,
                    candidate_info=candidate_info,
                    teacher_key=sample.selected_public_action_key,
                    teacher_ordinal=teacher_ordinal,
                )
            )
            continue
        try:
            response = score_provider(sample, physical_width)
            batch = _score_batch_from_provider_response(
                response,
                sample,
                expected_checkpoint_identity,
                physical_width,
            )
            results.append(
                _make_scored_result(
                    source=source,
                    authority_sample=authority_sample,
                    partition=partition,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    candidate_info=candidate_info,
                    teacher_ordinal=teacher_ordinal,
                    batch=batch,
                    top_k=config.top_k,
                )
            )
        except OfflineEvaluationError as error:
            reason = error.code if error.code in FAILURE_REASONS else FailureReason.INFERENCE_FAILURE
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=reason,
                    status=STATUS_UNSCORED,
                    label_status=LABEL_PASS,
                    candidate_info=candidate_info,
                    teacher_key=sample.selected_public_action_key,
                    teacher_ordinal=teacher_ordinal,
                )
            )
        except (task5.CodecError, task4.CodecError, TypeError, ValueError, OverflowError) as error:
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=FailureReason.INFERENCE_FAILURE,
                    status=STATUS_UNSCORED,
                    label_status=LABEL_PASS,
                    candidate_info=candidate_info,
                    teacher_key=sample.selected_public_action_key,
                    teacher_ordinal=teacher_ordinal,
                )
            )
        except Exception:
            # A provider crash is an unscored inference failure.  It is never
            # converted into a Teacher/RandomLegal continuation, and the
            # exception text is not public evidence.
            results.append(
                _failure_result(
                    source=source,
                    expected_authority=authority_sample,
                    evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
                    evaluation_contract_identity=population.evaluation_contract_identity,
                    population_identity=population_identity,
                    expected_partition=partition,
                    reason=FailureReason.INFERENCE_FAILURE,
                    status=STATUS_UNSCORED,
                    label_status=LABEL_PASS,
                    candidate_info=candidate_info,
                    teacher_key=sample.selected_public_action_key,
                    teacher_ordinal=teacher_ordinal,
                )
            )
    ordered_results = tuple(results)
    _validate_result_order(ordered_results)
    metrics = aggregate_offline_metrics(
        ordered_results,
        evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
        evaluation_contract_identity=population.evaluation_contract_identity,
        teacher_state_population_identity=population_identity,
        selected_partitions=config.selected_partitions,
        top_k=config.top_k,
    )
    slices = derive_offline_slices(
        ordered_results,
        evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
        evaluation_contract_identity=population.evaluation_contract_identity,
        teacher_state_population_identity=population_identity,
        top_k=config.top_k,
    )
    evaluation_result = OfflineEvaluationResultV1(
        evaluation_identity=task5.evaluation_identity(config.evaluation_context.root),
        evaluation_contract_identity=population.evaluation_contract_identity,
        teacher_state_population_identity=population_identity,
        sample_results=ordered_results,
        slice_results=slices,
        metrics=metrics,
    )
    evaluation_result.validate()
    return evaluation_result


@dataclasses.dataclass(frozen=True)
class OfflineEvaluationResultV1:
    evaluation_identity: str
    evaluation_contract_identity: str
    teacher_state_population_identity: str
    sample_results: tuple[OfflineSampleResultV1, ...]
    slice_results: tuple[OfflineSliceResultV1, ...]
    metrics: OfflineMetricsV1

    def validate(self) -> None:
        _validate_result_order(self.sample_results)
        self.metrics.validate()
        if self.metrics.evaluation_identity != self.evaluation_identity or self.metrics.evaluation_contract_identity != self.evaluation_contract_identity or self.metrics.teacher_state_population_identity != self.teacher_state_population_identity:
            raise OfflineCodecError("offline result metrics context differs")
        if tuple(result.offline_sample_identity for result in self.sample_results) != self.metrics.sample_result_identities:
            raise OfflineCodecError("offline result sample vector differs from metrics")
        for slice_result in self.slice_results:
            slice_result.validate()
        expected_slices = derive_offline_slices(
            self.sample_results,
            evaluation_identity=self.evaluation_identity,
            evaluation_contract_identity=self.evaluation_contract_identity,
            teacher_state_population_identity=self.teacher_state_population_identity,
            top_k=self.metrics.top_k,
        )
        if expected_slices != self.slice_results:
            raise OfflineCodecError("offline slice results are not derived from sample results")


def encode_offline_slice_jsonl(slices: Sequence[OfflineSliceResultV1]) -> bytes:
    if not isinstance(slices, (tuple, list)):
        raise OfflineCodecError("offline slices are not ordered")
    previous: Optional[tuple[int, tuple[bytes, ...]]] = None
    values = []
    for slice_result in slices:
        slice_result.validate()
        key = (SLICE_KIND_ORDER.index(slice_result.slice_kind), tuple(value.encode("utf-8") for value in slice_result.coordinates))
        if previous is not None and key < previous:
            raise OfflineCodecError("offline slices are not in canonical kind/coordinate order")
        previous = key
        values.append(slice_result.to_dict())
    return task5.canonical_jsonl_bytes(values)


def decode_offline_slice_jsonl(data: bytes) -> tuple[OfflineSliceResultV1, ...]:
    try:
        payloads = task5.parse_canonical_jsonl(data)
    except task5.CodecError as error:
        raise OfflineCodecError(str(error)) from error
    values = tuple(OfflineSliceResultV1.from_dict(payload) for payload in payloads)
    encode_offline_slice_jsonl(values)
    return values


def encode_offline_metrics_json(metrics: OfflineMetricsV1) -> bytes:
    return task5.canonical_json_bytes(metrics.to_dict())


def decode_offline_metrics_json(data: bytes) -> OfflineMetricsV1:
    try:
        return OfflineMetricsV1.from_dict(task5.parse_canonical_json(data))
    except task5.CodecError as error:
        raise OfflineCodecError(str(error)) from error


class OfflineEvaluatorV1:
    """Value-owned evaluator wrapper with no framework or filesystem state."""

    def __init__(
        self,
        config: OfflineEvaluationConfigV1,
        score_provider: Callable[[task4.CorpusSampleV1, int], Any],
    ) -> None:
        if not callable(score_provider):
            raise OfflineEvaluationError("INVALID_SCORER", "score provider is not callable")
        self.config = config
        self.score_provider = score_provider

    def evaluate(self, population: TrustedOfflinePopulationV1) -> OfflineEvaluationResultV1:
        return evaluate_offline(population, self.config, self.score_provider)


# Short aliases are intentionally limited to T5B-owned values.  There is no
# alias for a T5C gameplay job or a T5D FirstDivergence artifact here.
OfflineSampleV1 = OfflineSampleResultV1
OfflineSliceV1 = OfflineSliceResultV1
OfflineMetrics = OfflineMetricsV1


__all__ = [
    "OfflineCodecError",
    "OfflineEvaluationError",
    "FailureReason",
    "FAILURE_REASONS",
    "OfflineSourceSampleV1",
    "TrustedOfflinePopulationV1",
    "OfflineEvaluationConfigV1",
    "OfflineSampleResultV1",
    "OfflineSliceResultV1",
    "OfflineMetricsV1",
    "OfflineEvaluationResultV1",
    "OfflineEvaluatorV1",
    "OfflineSampleV1",
    "OfflineSliceV1",
    "OfflineMetrics",
    "OFFLINE_SAMPLE_SCHEMA_ID",
    "OFFLINE_SLICE_SCHEMA_ID",
    "OFFLINE_METRICS_SCHEMA_ID",
    "TEACHER_STATE_POPULATION_SCHEMA_ID",
    "TEACHER_STATE_POPULATION_ID_PREFIX",
    "LOSS_F64_CODEC_ID",
    "SLICE_DIMENSION_CONTRACT_ID",
    "SLICE_DIMENSION_ORDER",
    "loss_f64_bits",
    "loss_f64_bytes",
    "loss_f64_value",
    "exact_domain_loss",
    "canonical_teacher_state_population_identity_bytes",
    "teacher_state_population_identity",
    "aggregate_offline_metrics",
    "derive_offline_slices",
    "evaluate_offline",
    "encode_offline_sample_jsonl",
    "decode_offline_sample_jsonl",
    "encode_offline_sample_json",
    "decode_offline_sample_json",
    "encode_offline_slice_jsonl",
    "decode_offline_slice_jsonl",
    "encode_offline_metrics_json",
    "decode_offline_metrics_json",
]
