"""Canonical checkpoint export and fail-closed inference for Phase 6 Task 4A.

The module is an OCGForge boundary around the provisional PyTorch module.  It
does not serialize PyTorch objects, pass routing identities into the network,
or provide a gameplay fallback.  The later CUDA smoke may use the same
boundary after its separate authorization.
"""

from __future__ import annotations

import dataclasses
from typing import Optional, Sequence

import torch
from torch import Tensor

from . import task4_codec as codec
from . import task4_model


class Task4InferenceError(RuntimeError):
    """Raised when checkpoint, request, response, or model execution is unsafe."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


@dataclasses.dataclass(frozen=True)
class ExportedCheckpointV1:
    artifact_bytes: bytes
    artifact: codec.CheckpointArtifactV1

    @property
    def checkpoint_identity(self) -> str:
        return self.artifact.checkpoint_identity


@dataclasses.dataclass(frozen=True)
class LoadedCheckpointV1:
    artifact: codec.CheckpointArtifactV1
    architecture_config: codec.ArchitectureConfigV1

    @property
    def checkpoint_identity(self) -> str:
        return self.artifact.checkpoint_identity


def _error_from_codec(error: Exception, code: str = "invalid_checkpoint") -> Task4InferenceError:
    return Task4InferenceError(code, str(error))


def _canonical_parameter_tensors(
    model: torch.nn.Module,
    config: codec.ArchitectureConfigV1,
) -> tuple[codec.CanonicalTensorV1, ...]:
    named_parameters = tuple(model.named_parameters())
    if tuple(name for name, _ in named_parameters) != codec.PARAMETER_ORDER:
        raise Task4InferenceError("wrong_architecture", "model parameter order is not accepted")
    tensors: list[codec.CanonicalTensorV1] = []
    for (name, parameter), expected_shape in zip(
        named_parameters, codec.expected_parameter_shapes(config)
    ):
        if parameter.dtype != torch.float32 or tuple(parameter.shape) != expected_shape:
            raise Task4InferenceError("wrong_architecture", "model parameter shape or dtype is not accepted")
        if parameter.grad is not None:
            raise Task4InferenceError("training_state_present", "canonical export cannot include gradient state")
        values = parameter.detach().cpu().contiguous().reshape(-1).tolist()
        try:
            raw = b"".join(codec.f32_bytes(value) for value in values)
        except codec.CodecError as error:
            raise _error_from_codec(error) from error
        tensors.append(codec.CanonicalTensorV1(name, expected_shape, raw))
    return tuple(tensors)


def export_canonical_checkpoint(
    model: torch.nn.Module,
    *,
    source_dataset_identity: str,
    dataset_split_identity: str,
    card_vocabulary_identity: str,
    architecture_config: Optional[codec.ArchitectureConfigV1] = None,
    parent_checkpoint_identity: Optional[str] = None,
) -> ExportedCheckpointV1:
    """Export only canonical f32 inference weights and an OCGForge manifest."""

    config = codec.default_architecture_config() if architecture_config is None else architecture_config
    architecture_identity = codec.architecture_config_identity(config)
    tensors = _canonical_parameter_tensors(model, config)
    try:
        weight_bytes = codec.canonical_weight_export_bytes(tensors, architecture_identity, config)
        weight_identity = codec.weight_content_identity(weight_bytes)
        manifest = codec.default_checkpoint_manifest(
            architecture_config_identity=architecture_identity,
            card_vocabulary_identity=card_vocabulary_identity,
            dataset_identity=source_dataset_identity,
            dataset_split_identity=dataset_split_identity,
            canonical_weight_content_identity=weight_identity,
        )
        if parent_checkpoint_identity is not None:
            manifest = dataclasses.replace(
                manifest, parent_checkpoint_identity=parent_checkpoint_identity
            )
        artifact = codec.CheckpointArtifactV1(
            checkpoint_identity=codec.checkpoint_identity(manifest),
            manifest=manifest,
            weight_export_bytes=weight_bytes,
        )
        artifact_bytes = codec.encode_checkpoint_artifact(artifact)
        decoded = codec.decode_checkpoint_artifact(artifact_bytes)
    except codec.CodecError as error:
        raise _error_from_codec(error) from error
    if decoded != artifact:
        raise Task4InferenceError("invalid_checkpoint", "exported checkpoint did not round-trip")
    return ExportedCheckpointV1(artifact_bytes, artifact)


def load_checkpoint_artifact(
    artifact_bytes: bytes,
    *,
    expected_architecture_config: Optional[codec.ArchitectureConfigV1] = None,
    expected_card_vocabulary_identity: Optional[str] = None,
    expected_dataset_identity: Optional[str] = None,
    expected_dataset_split_identity: Optional[str] = None,
) -> LoadedCheckpointV1:
    """Validate every semantic checkpoint binding before it can be inferred."""

    try:
        artifact = codec.decode_checkpoint_artifact(artifact_bytes)
    except (codec.CodecError, TypeError, ValueError) as error:
        raise _error_from_codec(error) from error
    config = codec.default_architecture_config() if expected_architecture_config is None else expected_architecture_config
    expected_architecture_identity = codec.architecture_config_identity(config)
    manifest = artifact.manifest
    if manifest.model_architecture_config_identity != expected_architecture_identity:
        raise Task4InferenceError("wrong_architecture", "checkpoint architecture identity is not accepted")
    if manifest.canonical_weight_export_codec_identity != codec.WEIGHT_EXPORT_CONTRACT_ID:
        raise Task4InferenceError("invalid_checkpoint", "checkpoint weight codec is not accepted")
    if manifest.training_contract_identity != "ocgforge.phase6.bc_contract.v1":
        raise Task4InferenceError("invalid_checkpoint", "checkpoint training contract is not accepted")
    if expected_card_vocabulary_identity is not None and manifest.card_vocabulary_identity != expected_card_vocabulary_identity:
        raise Task4InferenceError("wrong_vocabulary", "checkpoint vocabulary identity is not accepted")
    if expected_dataset_identity is not None and manifest.dataset_identity != expected_dataset_identity:
        raise Task4InferenceError("wrong_dataset", "checkpoint dataset identity is not accepted")
    if expected_dataset_split_identity is not None and manifest.dataset_split_identity != expected_dataset_split_identity:
        raise Task4InferenceError("wrong_split", "checkpoint split identity is not accepted")
    try:
        architecture_identity, _ = codec.decode_weight_export_bytes(artifact.weight_export_bytes)
    except codec.CodecError as error:
        raise _error_from_codec(error) from error
    if architecture_identity != expected_architecture_identity:
        raise Task4InferenceError("wrong_architecture", "weight architecture identity is not accepted")
    return LoadedCheckpointV1(artifact, config)


def _load_tensor_values(raw: bytes, shape: tuple[int, ...]) -> Tensor:
    values = [codec.f32_value(raw[offset:offset + 4]) for offset in range(0, len(raw), 4)]
    return torch.tensor(values, dtype=torch.float32).reshape(shape)


def reload_model_from_checkpoint(
    artifact_bytes: bytes,
    *,
    expected_architecture_config: Optional[codec.ArchitectureConfigV1] = None,
    expected_card_vocabulary_identity: Optional[str] = None,
    expected_dataset_identity: Optional[str] = None,
    expected_dataset_split_identity: Optional[str] = None,
) -> tuple[LoadedCheckpointV1, task4_model.Phase6TorchCandidateScorer]:
    """Create a fresh scorer and load only canonical inference tensors."""

    loaded = load_checkpoint_artifact(
        artifact_bytes,
        expected_architecture_config=expected_architecture_config,
        expected_card_vocabulary_identity=expected_card_vocabulary_identity,
        expected_dataset_identity=expected_dataset_identity,
        expected_dataset_split_identity=expected_dataset_split_identity,
    )
    try:
        _, tensors = codec.decode_weight_export_bytes(loaded.artifact.weight_export_bytes)
    except codec.CodecError as error:
        raise _error_from_codec(error) from error
    model = task4_model.Phase6TorchCandidateScorer(loaded.architecture_config)
    with torch.no_grad():
        for parameter, tensor in zip(model.parameters(), tensors):
            values = _load_tensor_values(tensor.raw_bytes, tuple(tensor.shape))
            parameter.copy_(values)
    return loaded, model


def _validate_request(request: codec.InferenceRequestV1, checkpoint: LoadedCheckpointV1) -> None:
    if not isinstance(request, codec.InferenceRequestV1):
        raise Task4InferenceError("malformed_request", "inference request has the wrong type")
    try:
        if codec.inference_request_identity(request) != request.request_identity:
            raise Task4InferenceError("malformed_request", "request identity does not match canonical request bytes")
        if codec.ordered_candidate_domain_identity(request.routing_keys) != request.ordered_candidate_domain_identity:
            raise Task4InferenceError("wrong_candidate_domain", "request candidate domain identity is stale")
    except codec.CodecError as error:
        raise _error_from_codec(error, "malformed_request") from error
    if request.checkpoint_identity != checkpoint.checkpoint_identity:
        raise Task4InferenceError("wrong_checkpoint", "request checkpoint identity is not loaded")


def _select_ordinal(scores: Sequence[float], routing_keys: Sequence[str]) -> int:
    if len(scores) != len(routing_keys) or not scores:
        raise Task4InferenceError("score_count_mismatch", "score count does not equal the ordered domain")
    finite_scores: list[float] = []
    key_bytes: list[bytes] = []
    try:
        for score, key in zip(scores, routing_keys):
            finite_scores.append(codec.f32_value(codec.f32_bytes(score)))
            key_bytes.append(key.encode("utf-8", "strict"))
    except (codec.CodecError, UnicodeError) as error:
        raise _error_from_codec(error, "non_finite_score") from error
    best = 0
    for index in range(1, len(finite_scores)):
        if finite_scores[index] > finite_scores[best] or (
            finite_scores[index] == finite_scores[best]
            and key_bytes[index] < key_bytes[best]
        ):
            best = index
    return best


def _rows_tensor(
    rows: Sequence[Sequence[float]] | Tensor,
    width: int,
    device: torch.device,
    name: str,
) -> Tensor:
    if isinstance(rows, Tensor):
        value = rows
    else:
        try:
            for row in rows:
                if len(row) != width:
                    raise Task4InferenceError("invalid_model_input", f"{name} has the wrong width")
                for element in row:
                    codec.f32_bytes(element)
            value = torch.tensor(rows, dtype=torch.float32, device=device)
        except (TypeError, ValueError, codec.CodecError) as error:
            if isinstance(error, Task4InferenceError):
                raise
            raise _error_from_codec(error, "invalid_model_input") from error
    if value.device != device:
        raise Task4InferenceError("wrong_device", f"{name} is not on the model device")
    return value


def infer_request(
    model: task4_model.Phase6TorchCandidateScorer,
    checkpoint: LoadedCheckpointV1,
    request: codec.InferenceRequestV1,
    state_rows: Sequence[Sequence[float]] | Tensor,
    candidate_rows: Sequence[Sequence[float]] | Tensor,
    *,
    physical_candidate_capacity: Optional[int] = None,
) -> codec.InferenceResponseV1:
    """Run one exact-domain request and return no response on any failure."""

    _validate_request(request, checkpoint)
    try:
        model_device = next(model.parameters()).device
    except StopIteration as error:
        raise Task4InferenceError("invalid_model", "scorer has no parameters") from error
    state = _rows_tensor(state_rows, codec.STATE_ROW_WIDTH, model_device, "state rows")
    candidates = _rows_tensor(candidate_rows, codec.CANDIDATE_ROW_WIDTH, model_device, "candidate rows")
    if candidates.shape[0] != len(request.routing_keys):
        raise Task4InferenceError("score_count_mismatch", "candidate rows do not equal the ordered domain")
    try:
        was_training = model.training
        model.eval()
        with torch.no_grad():
            logits = model(
                state,
                candidates,
                physical_candidate_capacity=physical_candidate_capacity,
            )
        if was_training:
            model.train()
        task4_model.validate_logits(logits, len(request.routing_keys))
        scores = tuple(float(value) for value in logits.detach().cpu().tolist())
        selected = _select_ordinal(scores, request.routing_keys)
        return codec.make_inference_response(request, scores, selected)
    except Task4InferenceError:
        raise
    except (codec.CodecError, RuntimeError, ValueError) as error:
        raise _error_from_codec(error, "inference_failed") from error
    finally:
        if "was_training" in locals() and was_training:
            model.train()


def validate_response(
    request: codec.InferenceRequestV1,
    response: codec.InferenceResponseV1,
) -> codec.InferenceResponseV1:
    """Validate a response against the exact request that is awaiting it."""

    if not isinstance(response, codec.InferenceResponseV1):
        raise Task4InferenceError("malformed_response", "inference response has the wrong type")
    if response.schema_id != codec.INFERENCE_RESPONSE_SCHEMA_ID:
        raise Task4InferenceError("malformed_response", "inference response schema is not accepted")
    if response.request_identity != request.request_identity:
        raise Task4InferenceError("stale_response", "response belongs to another request")
    if response.checkpoint_identity != request.checkpoint_identity:
        raise Task4InferenceError("wrong_checkpoint", "response checkpoint identity is wrong")
    if response.model_input_identity != request.model_input_identity:
        raise Task4InferenceError("wrong_model_input", "response model-input identity is wrong")
    if response.ordered_candidate_domain_identity != request.ordered_candidate_domain_identity:
        raise Task4InferenceError("wrong_candidate_domain", "response candidate-domain identity is wrong")
    try:
        scores = tuple(codec.f32_value(codec.f32_bytes(value)) for value in response.scores)
    except (codec.CodecError, TypeError, ValueError) as error:
        raise _error_from_codec(error, "non_finite_score") from error
    if len(scores) != len(request.routing_keys):
        raise Task4InferenceError("score_count_mismatch", "response score count is wrong")
    ordinal = response.selected_candidate_ordinal
    if not isinstance(ordinal, int) or isinstance(ordinal, bool) or ordinal < 0 or ordinal >= len(request.routing_keys):
        raise Task4InferenceError("invalid_selection", "response selected ordinal is outside the domain")
    if response.selected_public_action_key != request.routing_keys[ordinal]:
        raise Task4InferenceError("invalid_selection", "response selected key is not the ordinal key")
    if _select_ordinal(scores, request.routing_keys) != ordinal:
        raise Task4InferenceError("invalid_selection", "response selection does not use the deterministic score rule")
    expected_identity = codec.inference_response_selection_identity(
        dataclasses.replace(response, scores=scores)
    )
    if response.response_identity != expected_identity:
        raise Task4InferenceError("malformed_response", "response selection identity is not canonical")
    return dataclasses.replace(response, scores=scores)


class Phase6InferenceRunnerV1:
    """Single-use request ledger with fail-closed response handling."""

    def __init__(
        self,
        model: task4_model.Phase6TorchCandidateScorer,
        checkpoint: LoadedCheckpointV1,
        *,
        physical_candidate_capacity: Optional[int] = None,
    ) -> None:
        self._model = model
        self._checkpoint = checkpoint
        self._physical_candidate_capacity = physical_candidate_capacity
        self._pending: dict[str, codec.InferenceRequestV1] = {}
        self._consumed: set[str] = set()
        self._closed: set[str] = set()

    def submit_request(self, request: codec.InferenceRequestV1) -> None:
        _validate_request(request, self._checkpoint)
        identity = request.request_identity
        if identity in self._consumed or identity in self._closed:
            raise Task4InferenceError("duplicate_request", "request has already been consumed or closed")
        if identity in self._pending:
            raise Task4InferenceError("duplicate_request", "request is already pending")
        self._pending[identity] = request

    def accept_response(
        self,
        request: codec.InferenceRequestV1,
        response: codec.InferenceResponseV1,
    ) -> codec.InferenceResponseV1:
        identity = request.request_identity
        if identity in self._consumed or identity in self._closed:
            raise Task4InferenceError("duplicate_response", "request response is already closed")
        pending = self._pending.pop(identity, None)
        if pending is None:
            raise Task4InferenceError("late_response", "response has no pending request")
        if pending != request:
            self._closed.add(identity)
            raise Task4InferenceError("stale_request", "request envelope does not match the pending request")
        try:
            accepted = validate_response(pending, response)
        except Task4InferenceError:
            self._closed.add(identity)
            raise
        self._consumed.add(identity)
        return accepted

    def infer(
        self,
        request: codec.InferenceRequestV1,
        state_rows: Sequence[Sequence[float]] | Tensor,
        candidate_rows: Sequence[Sequence[float]] | Tensor,
    ) -> codec.InferenceResponseV1:
        self.submit_request(request)
        try:
            response = infer_request(
                self._model,
                self._checkpoint,
                request,
                state_rows,
                candidate_rows,
                physical_candidate_capacity=self._physical_candidate_capacity,
            )
            return self.accept_response(request, response)
        except Exception as error:
            self._pending.pop(request.request_identity, None)
            self._closed.add(request.request_identity)
            if isinstance(error, Task4InferenceError):
                raise
            raise Task4InferenceError("inference_failed", str(error)) from error
