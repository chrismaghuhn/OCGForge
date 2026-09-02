"""Framework-neutral Phase-6 Task-4A canonical codecs.

This module deliberately uses only the Python standard library.  Its byte
encodings are OCGForge-owned semantic/provenance encodings; PyTorch objects,
optimizer state, and execution paths are never serialized as authority.
"""

from __future__ import annotations

import dataclasses
import hashlib
import math
import struct
from typing import Iterable, Optional, Sequence


class CodecError(ValueError):
    """Raised when a Task-4A value is malformed or non-canonical."""


INFERENCE_NUMERIC_CONTRACT_ID = "ocgforge.phase6.inference_numeric.v1"
F32_CODEC_ID = "ocgforge.phase6.numeric.f32_ieee754_be.v1"
NUMERIC_PROJECTION_CONTRACT_ID = "ocgforge.phase6.task4.numeric_projection.v1"

ARCHITECTURE_CONFIG_DOMAIN = "ocgforge.phase6.bc_architecture_config.v1"
ARCHITECTURE_ID_PREFIX = "phase6_architecture_config.v1."
WEIGHT_EXPORT_CONTRACT_ID = "ocgforge.phase6.canonical_weight_export.v1"
WEIGHT_CONTENT_ID_PREFIX = "phase6_weight_content.v1."
CORPUS_SCHEMA_ID = "ocgforge.phase6.task4.smoke_corpus.v2"
CORPUS_ARTIFACT_ID_PREFIX = "phase6_corpus_artifact.v2."
CORPUS_AUTHORITY_SCHEMA_ID = "ocgforge.phase6.task4.corpus_authority.v1"
CORPUS_AUTHORITY_ID_PREFIX = "phase6_corpus_authority.v1."
NUMERIC_MODEL_INPUT_SCHEMA_ID = "ocgforge.phase6.numeric_model_input.v1"
NUMERIC_MODEL_INPUT_ID_PREFIX = "phase6_numeric_model_input.v1."
TRAINING_RUN_SCHEMA_ID = "ocgforge.phase6.training_run.v1"
TRAINING_RUN_ID_PREFIX = "phase6_training_run.v1."
CHECKPOINT_SCHEMA_ID = "ocgforge.phase6.checkpoint_manifest.v1"
CHECKPOINT_ID_PREFIX = "phase6_checkpoint.v1."
CHECKPOINT_ARTIFACT_SCHEMA_ID = "ocgforge.phase6.checkpoint_artifact.v1"
INFERENCE_REQUEST_SCHEMA_ID = "ocgforge.phase6.inference_request.v1"
INFERENCE_RESPONSE_SCHEMA_ID = "ocgforge.phase6.inference_response.v1"
REQUEST_ID_PREFIX = "phase6_inference_request.v1."
RESPONSE_ID_PREFIX = "phase6_inference_response.v1."
ORDERED_DOMAIN_ID_DOMAIN = "ocgforge.phase6.ordered_candidate_domain.v1"
ORDERED_DOMAIN_ID_PREFIX = "phase6_ordered_candidate_domain.v1."
SPLIT_CONTRACT_ID = "ocgforge.phase6.dataset_split.v1"
SPLIT_IDENTITY_DOMAIN = "ocgforge.phase6.dataset_split_identity.v1"
SPLIT_IDENTITY_PREFIX = "phase6_dataset_split.v1."
SPLIT_PARTITION_IDENTITY = "ocgforge.phase6.split.fixed_80_10_10_sha256.v1"
BC_SAMPLE_IDENTITY_DOMAIN = "ocgforge.phase6.bc_sample_identity.v1"
BC_SAMPLE_IDENTITY_PREFIX = "bc_sample.v1."
CUDA_PREFLIGHT_DOMAIN = "ocgforge.phase6.cuda_preflight.v1"
CUDA_PREFLIGHT_ID_PREFIX = "phase6_cuda_preflight.v1."
SMOKE_EVIDENCE_SCHEMA_ID = "ocgforge.phase6.task4b.smoke_evidence.v1"
SMOKE_EVIDENCE_ID_PREFIX = "phase6_task4b_smoke_evidence.v1."
DETERMINISM_CONFIG_DOMAIN = "ocgforge.phase6.determinism_config.v1"
DETERMINISM_CONFIG_ID_PREFIX = "phase6_determinism_config.v1."
TRAIN_SAMPLE_ORDER_IDENTITY = "bc_sample_identity_ascending.v1"
TRAIN_SAMPLE_SCHEDULE_IDENTITY = "step_i_modulo_train_sample_count.v1"
RESPONSE_SELECTION_ID_DOMAIN = "ocgforge.phase6.inference_response_identity.v1"

STATE_ROW_WIDTH = 8
CANDIDATE_ROW_WIDTH = 28
SMOKE_MAX_OPTIMIZER_STEPS = 500
EXPECTED_DEVICE_TYPE = "cuda"
EXPECTED_DEVICE_INDEX = 0
EXPECTED_GPU_NAME = "NVIDIA GeForce RTX 4060 Ti"

PARAMETER_ORDER = (
    "state_encoder.input.weight",
    "state_encoder.input.bias",
    "candidate_encoder.input.weight",
    "candidate_encoder.input.bias",
    "score_head.weight",
    "score_head.bias",
)

TEACHER_SOURCE_IDENTITIES = (
    "policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d",
    "policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527",
)



def _text(value: str) -> bytes:
    if not isinstance(value, str):
        raise CodecError("canonical string is not text")
    try:
        return value.encode("utf-8", "strict")
    except UnicodeError as error:
        raise CodecError("canonical string is not valid UTF-8") from error


def pack_u8(value: int) -> bytes:
    if not isinstance(value, int) or not 0 <= value <= 0xFF:
        raise CodecError("u8 is out of range")
    return struct.pack(">B", value)


def pack_u16(value: int) -> bytes:
    if not isinstance(value, int) or not 0 <= value <= 0xFFFF:
        raise CodecError("u16 is out of range")
    return struct.pack(">H", value)


def pack_u32(value: int) -> bytes:
    if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise CodecError("u32 is out of range")
    return struct.pack(">I", value)


def pack_u64(value: int) -> bytes:
    if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise CodecError("u64 is out of range")
    return struct.pack(">Q", value)


def pack_string(value: str) -> bytes:
    encoded = _text(value)
    if len(encoded) > 0xFFFFFFFF:
        raise CodecError("string exceeds u32 length")
    return pack_u32(len(encoded)) + encoded


def pack_bytes(value: bytes) -> bytes:
    if not isinstance(value, (bytes, bytearray)):
        raise CodecError("canonical bytes value is not bytes")
    value = bytes(value)
    return pack_u64(len(value)) + value


def f32_bytes(value: float) -> bytes:
    try:
        converted = float(value)
        if not math.isfinite(converted):
            raise CodecError("non-finite binary32 value")
        encoded = struct.pack(">f", converted)
        if not math.isfinite(struct.unpack(">f", encoded)[0]):
            raise CodecError("value overflows binary32")
        return encoded
    except (OverflowError, struct.error) as error:
        raise CodecError("value is not representable as binary32") from error


def f32_value(raw: bytes) -> float:
    if len(raw) != 4:
        raise CodecError("binary32 value has wrong length")
    value = struct.unpack(">f", raw)[0]
    if not math.isfinite(value):
        raise CodecError("non-finite binary32 bytes")
    return value


def _vector(values: Iterable[bytes]) -> bytes:
    values = tuple(values)
    if len(values) > 0xFFFFFFFF:
        raise CodecError("vector exceeds u32 count")
    return pack_u32(len(values)) + b"".join(values)


def _string_vector(values: Sequence[str]) -> bytes:
    return _vector(pack_string(value) for value in values)


def _optional_string(value: Optional[str]) -> bytes:
    return pack_u8(0) if value is None else pack_u8(1) + pack_string(value)


def _digest(prefix: str, payload: bytes) -> str:
    return prefix + hashlib.sha256(payload).hexdigest()


def _validate_lower_hex_digest(value: str, field: str) -> None:
    if (not isinstance(value, str) or len(value) != 64 or
            any(character not in "0123456789abcdef" for character in value)):
        raise CodecError(f"{field} is not a lowercase SHA-256 digest")


def _validate_prefixed_digest(value: str, prefix: str, field: str) -> None:
    if not isinstance(value, str) or not value.startswith(prefix):
        raise CodecError(f"{field} has the wrong identity prefix")
    _validate_lower_hex_digest(value[len(prefix):], field)


def _validate_git_commit(value: str, field: str) -> None:
    if (not isinstance(value, str) or len(value) != 40 or
            any(character not in "0123456789abcdef" for character in value)):
        raise CodecError(f"{field} is not an immutable commit identity")


def _validate_public_action_key(value: str, field: str) -> None:
    prefix = "public_action.v1."
    if not isinstance(value, str) or not value.startswith(prefix):
        raise CodecError(f"{field} has the wrong public-action prefix")
    suffix = value[len(prefix):]
    if (not suffix or len(suffix) % 2 != 0 or
            any(character not in "0123456789abcdef" for character in suffix)):
        raise CodecError(f"{field} is not lowercase hexadecimal")


def _validate_ordered_domain_identity(value: str, field: str) -> None:
    if isinstance(value, str) and value.startswith(ORDERED_DOMAIN_ID_PREFIX):
        _validate_prefixed_digest(value, ORDERED_DOMAIN_ID_PREFIX, field)
        return
    _validate_lower_hex_digest(value, field)


@dataclasses.dataclass(frozen=True)
class ArchitectureConfigV1:
    numeric_contract_identity: str = F32_CODEC_ID
    projection_contract_identity: str = NUMERIC_PROJECTION_CONTRACT_ID
    state_row_width: int = STATE_ROW_WIDTH
    state_hidden_width: int = 16
    candidate_row_width: int = CANDIDATE_ROW_WIDTH
    candidate_hidden_width: int = 16
    state_pooling_identity: str = "mean_max.v1"
    activation_identity: str = "relu.v1"
    parameter_order: tuple[str, ...] = PARAMETER_ORDER


def default_architecture_config() -> ArchitectureConfigV1:
    return ArchitectureConfigV1()


def canonical_architecture_config_bytes(config: ArchitectureConfigV1) -> bytes:
    if config.parameter_order != PARAMETER_ORDER:
        raise CodecError("architecture parameter order is not accepted")
    if config.state_row_width != STATE_ROW_WIDTH or config.candidate_row_width != CANDIDATE_ROW_WIDTH:
        raise CodecError("architecture feature width is not accepted")
    fields = (
        pack_string(ARCHITECTURE_CONFIG_DOMAIN),
        pack_string(ARCHITECTURE_CONFIG_DOMAIN),
        pack_string("provisional_mean_max_candidate_scorer"),
        pack_string(config.numeric_contract_identity),
        pack_string(config.projection_contract_identity),
        pack_u32(config.state_row_width),
        pack_u32(config.state_hidden_width),
        pack_u32(config.candidate_row_width),
        pack_u32(config.candidate_hidden_width),
        pack_string(config.state_pooling_identity),
        pack_string(config.activation_identity),
        _string_vector(config.parameter_order),
    )
    return b"".join(fields)


def architecture_config_identity(config: Optional[ArchitectureConfigV1] = None) -> str:
    config = default_architecture_config() if config is None else config
    return _digest(ARCHITECTURE_ID_PREFIX, canonical_architecture_config_bytes(config))


def _subidentity(domain: str, values: Iterable[bytes], prefix: str) -> str:
    return _digest(prefix, _subidentity_bytes(domain, values))


def _subidentity_bytes(domain: str, values: Iterable[bytes]) -> bytes:
    return pack_string(domain) + pack_string(domain) + b"".join(values)


def canonical_optimizer_config_bytes() -> bytes:
    return _subidentity_bytes(
        "ocgforge.phase6.optimizer_config.v1",
        (pack_string("adam"), f32_bytes(0.001), f32_bytes(0.9), f32_bytes(0.999),
         f32_bytes(1e-8), f32_bytes(0.0),
         pack_string("foreach"), pack_u8(0),
         pack_string("fused"), pack_u8(0),
         pack_string("amsgrad"), pack_u8(0),
         pack_string("maximize"), pack_u8(0),
         pack_string("capturable"), pack_u8(0),
         pack_string("differentiable"), pack_u8(0),
         pack_string("decoupled_weight_decay"), pack_u8(0)),
    )


def optimizer_config_identity() -> str:
    return _digest("phase6_optimizer_config.v1.", canonical_optimizer_config_bytes())


def schedule_config_identity() -> str:
    return _subidentity(
        "ocgforge.phase6.schedule_config.v1", (pack_string("none"),),
        "phase6_schedule_config.v1.",
    )


def canonical_batch_config_bytes() -> bytes:
    return _subidentity_bytes(
        "ocgforge.phase6.batch_config.v1",
        (pack_u32(1), pack_string("mean_per_real_sample"),
         pack_string("no_semantic_padding"), pack_string(TRAIN_SAMPLE_ORDER_IDENTITY),
         pack_string("shuffle"), pack_u8(0),
         pack_string(TRAIN_SAMPLE_SCHEDULE_IDENTITY)),
    )


def batch_config_identity() -> str:
    return _digest("phase6_batch_config.v1.", canonical_batch_config_bytes())


def gradient_accumulation_identity() -> str:
    return _subidentity(
        "ocgforge.phase6.gradient_accumulation.v1", (pack_u32(1),),
        "phase6_gradient_accumulation.v1.",
    )


def rng_initialization_identity() -> str:
    return _subidentity(
        "ocgforge.phase6.rng_initialization.v1",
        (pack_string("torch_cpu_cuda_manual_seed"), pack_u64(1729)),
        "phase6_rng_initialization.v1.",
    )


def precision_identity() -> str:
    return _subidentity(
        "ocgforge.phase6.precision.v1", (pack_string("f32"),),
        "phase6_precision.v1.",
    )


def canonical_deterministic_execution_bytes() -> bytes:
    return _subidentity_bytes(
        DETERMINISM_CONFIG_DOMAIN,
        (pack_string("torch.use_deterministic_algorithms"), pack_u8(1),
         pack_string("warn_only"), pack_u8(0),
         pack_string("torch.set_float32_matmul_precision"), pack_string("highest")),
    )


def deterministic_execution_identity() -> str:
    return _digest(DETERMINISM_CONFIG_ID_PREFIX,
                   canonical_deterministic_execution_bytes())


@dataclasses.dataclass(frozen=True)
class ExecutionProvenanceV1:
    backend_identity: str = "ocgforge.phase6.backend.pytorch.provisional.v1"
    framework_version: str = "2.12.1+cu126"
    device_type: str = EXPECTED_DEVICE_TYPE
    device_index: int = EXPECTED_DEVICE_INDEX
    gpu_name: str = EXPECTED_GPU_NAME
    torch_cuda_version_reported: str = "12.6"
    capability_major: int = 8
    capability_minor: int = 9
    distributed_strategy: str = "single_process"
    world_size: int = 1
    deterministic_algorithms: bool = True
    deterministic_warn_only: bool = False
    float32_matmul_precision: str = "highest"


EXECUTION_PROVENANCE_DOMAIN = "ocgforge.phase6.execution_provenance.v1"
EXECUTION_PROVENANCE_ID_PREFIX = "phase6_execution_provenance.v1."


def canonical_execution_provenance_bytes(
    provenance: ExecutionProvenanceV1,
) -> bytes:
    if provenance.device_index < 0 or provenance.world_size <= 0:
        raise CodecError("execution provenance integer is invalid")
    if provenance.capability_major < 0 or provenance.capability_minor < 0:
        raise CodecError("execution capability is invalid")
    if (not isinstance(provenance.deterministic_algorithms, bool) or
            not isinstance(provenance.deterministic_warn_only, bool) or
            not provenance.deterministic_algorithms or
            provenance.deterministic_warn_only or
            provenance.float32_matmul_precision != "highest"):
        raise CodecError("Task-4 deterministic execution configuration is not accepted")
    values = (
        provenance.backend_identity, provenance.framework_version,
        provenance.device_type, provenance.gpu_name,
        provenance.torch_cuda_version_reported,
        provenance.distributed_strategy, provenance.float32_matmul_precision,
    )
    if any(not isinstance(value, str) or not value for value in values):
        raise CodecError("execution provenance string is empty")
    return b"".join((pack_string(EXECUTION_PROVENANCE_DOMAIN),
                     pack_string(EXECUTION_PROVENANCE_DOMAIN),
                     pack_string(provenance.backend_identity),
                     pack_string(provenance.framework_version),
                     pack_string(provenance.device_type),
                     pack_u32(provenance.device_index),
                     pack_string(provenance.gpu_name),
                     pack_string(provenance.torch_cuda_version_reported),
                     pack_u32(provenance.capability_major),
                     pack_u32(provenance.capability_minor),
                     pack_string(provenance.distributed_strategy),
                     pack_u32(provenance.world_size),
                     pack_u8(1 if provenance.deterministic_algorithms else 0),
                     pack_u8(1 if provenance.deterministic_warn_only else 0),
                     pack_string(provenance.float32_matmul_precision)))


def execution_provenance_identity_for(
    provenance: ExecutionProvenanceV1,
) -> str:
    return _digest(EXECUTION_PROVENANCE_ID_PREFIX,
                   canonical_execution_provenance_bytes(provenance))


def execution_provenance_identity(
    *,
    backend_identity: str = "ocgforge.phase6.backend.pytorch.provisional.v1",
    framework_version: str = "2.12.1+cu126",
    device_type: str = EXPECTED_DEVICE_TYPE,
    device_index: int = EXPECTED_DEVICE_INDEX,
    gpu_name: str = EXPECTED_GPU_NAME,
    torch_cuda_version_reported: str = "12.6",
    capability_major: int = 8,
    capability_minor: int = 9,
    distributed_strategy: str = "single_process",
    world_size: int = 1,
    deterministic_algorithms: bool = True,
    deterministic_warn_only: bool = False,
    float32_matmul_precision: str = "highest",
) -> str:
    return execution_provenance_identity_for(ExecutionProvenanceV1(
        backend_identity=backend_identity,
        framework_version=framework_version,
        device_type=device_type,
        device_index=device_index,
        gpu_name=gpu_name,
        torch_cuda_version_reported=torch_cuda_version_reported,
        capability_major=capability_major,
        capability_minor=capability_minor,
        distributed_strategy=distributed_strategy,
        world_size=world_size,
        deterministic_algorithms=deterministic_algorithms,
        deterministic_warn_only=deterministic_warn_only,
        float32_matmul_precision=float32_matmul_precision,
    ))


@dataclasses.dataclass(frozen=True)
class CudaPreflightFactsV1:
    cuda_available: bool
    device_count: int
    execution_provenance: ExecutionProvenanceV1


def canonical_cuda_preflight_bytes(facts: CudaPreflightFactsV1) -> bytes:
    if not facts.cuda_available or facts.device_count < 1:
        raise CodecError("CUDA preflight facts do not prove an available device")
    return b"".join((pack_string(CUDA_PREFLIGHT_DOMAIN),
                     pack_string(CUDA_PREFLIGHT_DOMAIN),
                     pack_u8(1), pack_u32(facts.device_count),
                     canonical_execution_provenance_bytes(facts.execution_provenance)))


def cuda_preflight_identity_for(facts: CudaPreflightFactsV1) -> str:
    return _digest(CUDA_PREFLIGHT_ID_PREFIX, canonical_cuda_preflight_bytes(facts))


@dataclasses.dataclass(frozen=True)
class Task4BSmokeEvidenceV1:
    training_run_identity: str
    source_dataset_identity: str
    dataset_split_identity: str
    model_architecture_config_identity: str
    card_vocabulary_identity: str
    optimizer_configuration_identity: str
    learning_rate_schedule_identity: str
    batch_configuration_identity: str
    gradient_accumulation_configuration_identity: str
    training_rng_contract_identity: str
    training_seed_or_initialization_identity: str
    precision_mode_identity: str
    deterministic_execution_configuration_identity: str
    device_and_distributed_provenance_identity: str
    cuda_preflight_identity: str
    maximum_optimizer_steps: int
    actual_optimizer_steps: int
    final_exported_checkpoint_identity: Optional[str] = None
    gpu_memory_before: Optional[int] = None
    gpu_memory_peak: Optional[int] = None
    gpu_memory_after: Optional[int] = None
    schema_id: str = SMOKE_EVIDENCE_SCHEMA_ID


def canonical_smoke_evidence_bytes(
    evidence: Task4BSmokeEvidenceV1,
) -> bytes:
    if evidence.schema_id != SMOKE_EVIDENCE_SCHEMA_ID:
        raise CodecError("unknown Task-4B smoke-evidence schema")
    _validate_prefixed_digest(evidence.training_run_identity,
                              TRAINING_RUN_ID_PREFIX,
                              "smoke training-run identity")
    _validate_lower_hex_digest(evidence.source_dataset_identity,
                               "smoke source dataset identity")
    _validate_prefixed_digest(evidence.dataset_split_identity,
                              SPLIT_IDENTITY_PREFIX,
                              "smoke split identity")
    _validate_prefixed_digest(evidence.model_architecture_config_identity,
                              ARCHITECTURE_ID_PREFIX,
                              "smoke architecture identity")
    _validate_prefixed_digest(evidence.card_vocabulary_identity,
                              "model_card_vocabulary.v1.",
                              "smoke vocabulary identity")
    if evidence.optimizer_configuration_identity != optimizer_config_identity():
        raise CodecError("smoke optimizer identity is not accepted")
    if evidence.learning_rate_schedule_identity != schedule_config_identity():
        raise CodecError("smoke schedule identity is not accepted")
    if evidence.batch_configuration_identity != batch_config_identity():
        raise CodecError("smoke batch identity is not accepted")
    if evidence.gradient_accumulation_configuration_identity != gradient_accumulation_identity():
        raise CodecError("smoke gradient-accumulation identity is not accepted")
    if evidence.training_rng_contract_identity != "ocgforge.phase6.training_rng.v1":
        raise CodecError("smoke RNG contract is not accepted")
    if evidence.training_seed_or_initialization_identity != rng_initialization_identity():
        raise CodecError("smoke initialization identity is not accepted")
    if evidence.precision_mode_identity != precision_identity():
        raise CodecError("smoke precision identity is not accepted")
    if evidence.deterministic_execution_configuration_identity != deterministic_execution_identity():
        raise CodecError("smoke deterministic-execution identity is not accepted")
    _validate_prefixed_digest(evidence.device_and_distributed_provenance_identity,
                              EXECUTION_PROVENANCE_ID_PREFIX,
                              "smoke execution provenance identity")
    _validate_prefixed_digest(evidence.cuda_preflight_identity,
                              CUDA_PREFLIGHT_ID_PREFIX,
                              "smoke CUDA preflight identity")
    if evidence.maximum_optimizer_steps != SMOKE_MAX_OPTIMIZER_STEPS:
        raise CodecError("smoke maximum optimizer step contract changed")
    if not isinstance(evidence.actual_optimizer_steps, int) or isinstance(evidence.actual_optimizer_steps, bool) or not 0 <= evidence.actual_optimizer_steps <= evidence.maximum_optimizer_steps:
        raise CodecError("smoke actual optimizer step count is outside the hard bound")
    if evidence.final_exported_checkpoint_identity is not None:
        _validate_prefixed_digest(evidence.final_exported_checkpoint_identity,
                                  CHECKPOINT_ID_PREFIX,
                                  "smoke final checkpoint identity")
    memories = (evidence.gpu_memory_before, evidence.gpu_memory_peak,
                evidence.gpu_memory_after)
    if any(value is not None for value in memories):
        if any(not isinstance(value, int) or isinstance(value, bool) or value < 0
               for value in memories):
            raise CodecError("GPU memory evidence is invalid")
    if evidence.actual_optimizer_steps > 0 and any(value is None for value in memories):
        raise CodecError("positive smoke evidence requires complete GPU memory evidence")
    out = [pack_string(evidence.schema_id),
           pack_string(evidence.training_run_identity),
           pack_string(evidence.source_dataset_identity),
           pack_string(evidence.dataset_split_identity),
           pack_string(evidence.model_architecture_config_identity),
           pack_string(evidence.card_vocabulary_identity),
           pack_string(evidence.optimizer_configuration_identity),
           pack_string(evidence.learning_rate_schedule_identity),
           pack_string(evidence.batch_configuration_identity),
           pack_string(evidence.gradient_accumulation_configuration_identity),
           pack_string(evidence.training_rng_contract_identity),
           pack_string(evidence.training_seed_or_initialization_identity),
           pack_string(evidence.precision_mode_identity),
           pack_string(evidence.deterministic_execution_configuration_identity),
           pack_string(evidence.device_and_distributed_provenance_identity),
           pack_string(evidence.cuda_preflight_identity),
           pack_u32(evidence.maximum_optimizer_steps),
           pack_u32(evidence.actual_optimizer_steps),
           _optional_string(evidence.final_exported_checkpoint_identity)]
    for value in memories:
        out.append(pack_u8(0) if value is None else pack_u8(1) + pack_u64(value))
    return b"".join(out)


def smoke_evidence_identity(evidence: Task4BSmokeEvidenceV1) -> str:
    return _digest(SMOKE_EVIDENCE_ID_PREFIX,
                   canonical_smoke_evidence_bytes(evidence))


@dataclasses.dataclass(frozen=True)
class TrainingRunManifestV1:
    training_contract_identity: str
    source_dataset_identity: str
    dataset_split_identity: str
    phase5_logical_model_input_contract_identity: str
    phase5_encoded_model_input_contract_identity: str
    phase5_batch_layout_contract_identity: str
    card_vocabulary_identity: str
    model_architecture_config_identity: str
    behavior_policy_source_identities: tuple[str, ...]
    opponent_policy_source_identities: tuple[str, ...]
    training_code_commit: str
    framework_backend_identity: str
    framework_version: str
    optimizer_configuration_identity: str
    learning_rate_schedule_identity: str
    batch_configuration_identity: str
    gradient_accumulation_configuration_identity: str
    training_rng_contract_identity: str
    training_seed_or_initialization_identity: str
    initial_checkpoint_identity: Optional[str]
    precision_mode_identity: str
    device_and_distributed_provenance_identity: str
    final_exported_checkpoint_identity: Optional[str]
    schema_id: str = TRAINING_RUN_SCHEMA_ID


def replace_training_run(manifest: TrainingRunManifestV1, **changes: object) -> TrainingRunManifestV1:
    return dataclasses.replace(manifest, **changes)


def default_training_run_manifest(
    *,
    source_dataset_identity: str,
    dataset_split_identity: str,
    card_vocabulary_identity: str,
    training_code_commit: str,
    actual_optimizer_steps: int,
) -> TrainingRunManifestV1:
    if actual_optimizer_steps != 0:
        raise CodecError("generic training manifest constructor only issues zero-step infrastructure")
    return TrainingRunManifestV1(
        training_contract_identity="ocgforge.phase6.bc_contract.v1",
        source_dataset_identity=source_dataset_identity,
        dataset_split_identity=dataset_split_identity,
        phase5_logical_model_input_contract_identity="ocgforge.model_logical_input.v1",
        phase5_encoded_model_input_contract_identity="ocgforge.model_encoded_input.v1",
        phase5_batch_layout_contract_identity="ocgforge.model_batch_layout.v1",
        card_vocabulary_identity=card_vocabulary_identity,
        model_architecture_config_identity=architecture_config_identity(default_architecture_config()),
        behavior_policy_source_identities=tuple(sorted(TEACHER_SOURCE_IDENTITIES)),
        opponent_policy_source_identities=tuple(sorted(TEACHER_SOURCE_IDENTITIES)),
        training_code_commit=training_code_commit,
        framework_backend_identity="ocgforge.phase6.backend.pytorch.provisional.v1",
        framework_version="2.12.1+cu126",
        optimizer_configuration_identity=optimizer_config_identity(),
        learning_rate_schedule_identity=schedule_config_identity(),
        batch_configuration_identity=batch_config_identity(),
        gradient_accumulation_configuration_identity=gradient_accumulation_identity(),
        training_rng_contract_identity="ocgforge.phase6.training_rng.v1",
        training_seed_or_initialization_identity=rng_initialization_identity(),
        initial_checkpoint_identity=None,
        precision_mode_identity=precision_identity(),
        device_and_distributed_provenance_identity=execution_provenance_identity(),
        final_exported_checkpoint_identity=None,
    )


def _validate_identity(value: str, prefixes: Sequence[str], field: str) -> None:
    if not isinstance(value, str) or not value or not any(value.startswith(prefix) for prefix in prefixes):
        raise CodecError(f"{field} is not a valid identity")


def _validate_sorted_unique(values: Sequence[str], field: str) -> None:
    if tuple(values) != tuple(sorted(values)) or len(set(values)) != len(values):
        raise CodecError(f"{field} is not sorted and unique")


def canonical_training_run_manifest_bytes(manifest: TrainingRunManifestV1) -> bytes:
    if manifest.schema_id != TRAINING_RUN_SCHEMA_ID:
        raise CodecError("unknown training-run schema")
    if manifest.training_contract_identity != "ocgforge.phase6.bc_contract.v1":
        raise CodecError("unknown training contract")
    _validate_lower_hex_digest(manifest.source_dataset_identity,
                               "training source dataset identity")
    _validate_prefixed_digest(manifest.dataset_split_identity,
                              SPLIT_IDENTITY_PREFIX,
                              "training split identity")
    _validate_prefixed_digest(manifest.card_vocabulary_identity,
                              "model_card_vocabulary.v1.",
                              "training vocabulary identity")
    _validate_git_commit(manifest.training_code_commit, "training code commit")
    if manifest.phase5_logical_model_input_contract_identity != "ocgforge.model_logical_input.v1":
        raise CodecError("unknown logical model-input contract")
    if manifest.phase5_encoded_model_input_contract_identity != "ocgforge.model_encoded_input.v1":
        raise CodecError("unknown encoded model-input contract")
    if manifest.phase5_batch_layout_contract_identity != "ocgforge.model_batch_layout.v1":
        raise CodecError("unknown model-batch contract")
    if manifest.model_architecture_config_identity != architecture_config_identity():
        raise CodecError("training architecture identity is not accepted")
    if tuple(manifest.behavior_policy_source_identities) != tuple(sorted(TEACHER_SOURCE_IDENTITIES)):
        raise CodecError("training behavior policy identities are not accepted")
    if tuple(manifest.opponent_policy_source_identities) != tuple(sorted(TEACHER_SOURCE_IDENTITIES)):
        raise CodecError("training opponent policy identities are not accepted")
    if manifest.framework_backend_identity != "ocgforge.phase6.backend.pytorch.provisional.v1":
        raise CodecError("training backend identity is not the provisional backend")
    if manifest.optimizer_configuration_identity != optimizer_config_identity():
        raise CodecError("training optimizer identity is not accepted")
    if manifest.learning_rate_schedule_identity != schedule_config_identity():
        raise CodecError("training schedule identity is not accepted")
    if manifest.batch_configuration_identity != batch_config_identity():
        raise CodecError("training batch identity is not accepted")
    if manifest.gradient_accumulation_configuration_identity != gradient_accumulation_identity():
        raise CodecError("training gradient-accumulation identity is not accepted")
    if manifest.training_rng_contract_identity != "ocgforge.phase6.training_rng.v1":
        raise CodecError("training RNG contract is not accepted")
    if manifest.training_seed_or_initialization_identity != rng_initialization_identity():
        raise CodecError("training initialization identity is not accepted")
    if manifest.precision_mode_identity != precision_identity():
        raise CodecError("training precision identity is not accepted")
    _validate_identity(
        manifest.device_and_distributed_provenance_identity,
        ("phase6_execution_provenance.v1.",),
        "execution provenance identity",
    )
    if manifest.initial_checkpoint_identity is not None:
        _validate_identity(manifest.initial_checkpoint_identity, (CHECKPOINT_ID_PREFIX,), "initial checkpoint identity")
    if manifest.final_exported_checkpoint_identity is not None:
        _validate_identity(manifest.final_exported_checkpoint_identity, (CHECKPOINT_ID_PREFIX,), "final checkpoint identity")
    _validate_sorted_unique(manifest.behavior_policy_source_identities, "behavior policy identities")
    _validate_sorted_unique(manifest.opponent_policy_source_identities, "opponent policy identities")
    strings = (
        manifest.schema_id, manifest.training_contract_identity, manifest.source_dataset_identity,
        manifest.dataset_split_identity, manifest.phase5_logical_model_input_contract_identity,
        manifest.phase5_encoded_model_input_contract_identity, manifest.phase5_batch_layout_contract_identity,
        manifest.card_vocabulary_identity, manifest.model_architecture_config_identity,
    )
    out = [pack_string(value) for value in strings]
    out.extend((_string_vector(manifest.behavior_policy_source_identities),
                _string_vector(manifest.opponent_policy_source_identities)))
    out.extend(pack_string(value) for value in (
        manifest.training_code_commit, manifest.framework_backend_identity,
        manifest.framework_version, manifest.optimizer_configuration_identity,
        manifest.learning_rate_schedule_identity, manifest.batch_configuration_identity,
        manifest.gradient_accumulation_configuration_identity, manifest.training_rng_contract_identity,
        manifest.training_seed_or_initialization_identity))
    out.append(_optional_string(manifest.initial_checkpoint_identity))
    out.append(pack_string(manifest.precision_mode_identity))
    out.append(pack_string(manifest.device_and_distributed_provenance_identity))
    out.append(_optional_string(manifest.final_exported_checkpoint_identity))
    return b"".join(out)


def training_run_identity(manifest: TrainingRunManifestV1) -> str:
    return _digest(TRAINING_RUN_ID_PREFIX, canonical_training_run_manifest_bytes(manifest))


@dataclasses.dataclass(frozen=True)
class CanonicalTensorV1:
    name: str
    shape: tuple[int, ...]
    raw_bytes: bytes
    dtype_identity: str = F32_CODEC_ID


def expected_parameter_shapes(config: ArchitectureConfigV1) -> tuple[tuple[int, ...], ...]:
    return (
        (config.state_hidden_width, config.state_row_width),
        (config.state_hidden_width,),
        (config.candidate_hidden_width,
         config.state_hidden_width * 2 + config.candidate_row_width),
        (config.candidate_hidden_width,),
        (1, config.state_hidden_width * 2 + config.candidate_hidden_width),
        (1,),
    )


def _validate_tensor(tensor: CanonicalTensorV1, expected_name: str, expected_shape: tuple[int, ...]) -> None:
    if tensor.name != expected_name or tuple(tensor.shape) != expected_shape:
        raise CodecError("tensor name or shape is not canonical")
    if tensor.dtype_identity != F32_CODEC_ID or any(not isinstance(value, int) or value <= 0 for value in tensor.shape):
        raise CodecError("tensor dtype or shape is invalid")
    elements = 1
    for value in tensor.shape:
        elements *= value
    if len(tensor.raw_bytes) != elements * 4:
        raise CodecError("tensor byte length does not match shape")
    for offset in range(0, len(tensor.raw_bytes), 4):
        f32_value(tensor.raw_bytes[offset:offset + 4])


def canonical_weight_export_bytes(
    tensors: Sequence[CanonicalTensorV1],
    architecture_identity: str,
    config: Optional[ArchitectureConfigV1] = None,
) -> bytes:
    config = default_architecture_config() if config is None else config
    _validate_identity(architecture_identity, (ARCHITECTURE_ID_PREFIX,), "architecture identity")
    if architecture_identity != architecture_config_identity(config):
        raise CodecError("architecture identity does not match configuration")
    if tuple(tensor.name for tensor in tensors) != PARAMETER_ORDER:
        raise CodecError("tensor order is not canonical")
    shapes = expected_parameter_shapes(config)
    for tensor, name, shape in zip(tensors, PARAMETER_ORDER, shapes):
        _validate_tensor(tensor, name, shape)
    out = [pack_string(WEIGHT_EXPORT_CONTRACT_ID), pack_string(WEIGHT_EXPORT_CONTRACT_ID),
           pack_string(F32_CODEC_ID), pack_string(architecture_identity), pack_u32(len(tensors))]
    for tensor in tensors:
        out.extend((pack_string(tensor.name), pack_u32(len(tensor.shape)),
                    b"".join(pack_u64(value) for value in tensor.shape),
                    pack_string(tensor.dtype_identity), pack_u64(len(tensor.raw_bytes) // 4),
                    pack_u64(len(tensor.raw_bytes)), bytes(tensor.raw_bytes)))
    return b"".join(out)


def weight_content_identity(weight_bytes: bytes) -> str:
    return _digest(WEIGHT_CONTENT_ID_PREFIX, bytes(weight_bytes))


class _Reader:
    def __init__(self, data: bytes):
        self.data = bytes(data)
        self.offset = 0

    def take(self, length: int) -> bytes:
        if length < 0 or self.offset + length > len(self.data):
            raise CodecError("truncated canonical bytes")
        value = self.data[self.offset:self.offset + length]
        self.offset += length
        return value

    def u8(self) -> int:
        return struct.unpack(">B", self.take(1))[0]

    def u32(self) -> int:
        return struct.unpack(">I", self.take(4))[0]

    def u64(self) -> int:
        return struct.unpack(">Q", self.take(8))[0]

    def string(self) -> str:
        raw = self.take(self.u32())
        try:
            return raw.decode("utf-8", "strict")
        except UnicodeError as error:
            raise CodecError("invalid UTF-8 in canonical bytes") from error

    def optional_string(self) -> Optional[str]:
        present = self.u8()
        if present == 0:
            return None
        if present != 1:
            raise CodecError("invalid optional-string presence")
        return self.string()

    def bytes_u64(self) -> bytes:
        return self.take(self.u64())

    def done(self) -> bool:
        return self.offset == len(self.data)


def decode_weight_export_bytes(weight_bytes: bytes) -> tuple[str, tuple[CanonicalTensorV1, ...]]:
    reader = _Reader(weight_bytes)
    if reader.string() != WEIGHT_EXPORT_CONTRACT_ID or reader.string() != WEIGHT_EXPORT_CONTRACT_ID:
        raise CodecError("unknown weight export schema")
    if reader.string() != F32_CODEC_ID:
        raise CodecError("unknown weight numeric codec")
    architecture_identity = reader.string()
    count = reader.u32()
    if count != len(PARAMETER_ORDER):
        raise CodecError("unexpected tensor count")
    tensors = []
    config = default_architecture_config()
    shapes = expected_parameter_shapes(config)
    for name, shape in zip(PARAMETER_ORDER, shapes):
        if reader.string() != name:
            raise CodecError("unexpected tensor name/order")
        rank = reader.u32()
        actual_shape = tuple(reader.u64() for _ in range(rank))
        dtype = reader.string()
        elements = reader.u64()
        raw = reader.bytes_u64()
        if elements * 4 != len(raw):
            raise CodecError("tensor element count mismatch")
        tensor = CanonicalTensorV1(name, actual_shape, raw, dtype)
        _validate_tensor(tensor, name, shape)
        tensors.append(tensor)
    if not reader.done():
        raise CodecError("weight export has trailing bytes")
    if canonical_weight_export_bytes(tensors, architecture_identity) != bytes(weight_bytes):
        raise CodecError("weight export is not canonical")
    return architecture_identity, tuple(tensors)


@dataclasses.dataclass(frozen=True)
class CheckpointManifestV1:
    model_architecture_config_identity: str
    phase5_logical_model_input_contract_identity: str
    phase5_encoded_model_input_contract_identity: str
    phase5_batch_layout_contract_identity: str
    card_vocabulary_identity: str
    dataset_identity: str
    dataset_split_identity: str
    training_contract_identity: str
    parent_checkpoint_identity: Optional[str]
    canonical_weight_export_codec_identity: str
    canonical_weight_content_identity: str
    schema_id: str = CHECKPOINT_SCHEMA_ID
    checkpoint_schema_version: str = CHECKPOINT_SCHEMA_ID


def default_checkpoint_manifest(
    *,
    architecture_config_identity: str,
    card_vocabulary_identity: str,
    dataset_identity: str,
    dataset_split_identity: str,
    canonical_weight_content_identity: str,
) -> CheckpointManifestV1:
    return CheckpointManifestV1(
        model_architecture_config_identity=architecture_config_identity,
        phase5_logical_model_input_contract_identity="ocgforge.model_logical_input.v1",
        phase5_encoded_model_input_contract_identity="ocgforge.model_encoded_input.v1",
        phase5_batch_layout_contract_identity="ocgforge.model_batch_layout.v1",
        card_vocabulary_identity=card_vocabulary_identity,
        dataset_identity=dataset_identity,
        dataset_split_identity=dataset_split_identity,
        training_contract_identity="ocgforge.phase6.bc_contract.v1",
        parent_checkpoint_identity=None,
        canonical_weight_export_codec_identity=WEIGHT_EXPORT_CONTRACT_ID,
        canonical_weight_content_identity=canonical_weight_content_identity,
    )


def canonical_checkpoint_manifest_bytes(manifest: CheckpointManifestV1) -> bytes:
    if manifest.schema_id != CHECKPOINT_SCHEMA_ID or manifest.checkpoint_schema_version != CHECKPOINT_SCHEMA_ID:
        raise CodecError("unknown checkpoint schema")
    if manifest.canonical_weight_export_codec_identity != WEIGHT_EXPORT_CONTRACT_ID:
        raise CodecError("unknown checkpoint weight codec")
    if manifest.phase5_logical_model_input_contract_identity != "ocgforge.model_logical_input.v1":
        raise CodecError("unknown logical Phase-5 contract")
    if manifest.phase5_encoded_model_input_contract_identity != "ocgforge.model_encoded_input.v1":
        raise CodecError("unknown encoded Phase-5 contract")
    if manifest.phase5_batch_layout_contract_identity != "ocgforge.model_batch_layout.v1":
        raise CodecError("unknown batch-layout Phase-5 contract")
    if manifest.training_contract_identity != "ocgforge.phase6.bc_contract.v1":
        raise CodecError("unknown checkpoint training contract")
    _validate_identity(
        manifest.model_architecture_config_identity,
        (ARCHITECTURE_ID_PREFIX,),
        "checkpoint architecture identity",
    )
    _validate_identity(
        manifest.canonical_weight_content_identity,
        (WEIGHT_CONTENT_ID_PREFIX,),
        "checkpoint weight content identity",
    )
    _validate_lower_hex_digest(manifest.dataset_identity,
                               "checkpoint dataset identity")
    _validate_prefixed_digest(manifest.dataset_split_identity,
                              SPLIT_IDENTITY_PREFIX,
                              "checkpoint split identity")
    _validate_prefixed_digest(manifest.card_vocabulary_identity,
                              "model_card_vocabulary.v1.",
                              "checkpoint vocabulary identity")
    if manifest.parent_checkpoint_identity is not None:
        _validate_identity(manifest.parent_checkpoint_identity, (CHECKPOINT_ID_PREFIX,), "parent checkpoint identity")
    values = (
        manifest.schema_id, manifest.checkpoint_schema_version,
        manifest.model_architecture_config_identity,
        manifest.phase5_logical_model_input_contract_identity,
        manifest.phase5_encoded_model_input_contract_identity,
        manifest.phase5_batch_layout_contract_identity, manifest.card_vocabulary_identity,
        manifest.dataset_identity, manifest.dataset_split_identity,
        manifest.training_contract_identity,
    )
    out = [pack_string(value) for value in values]
    out.extend((_optional_string(manifest.parent_checkpoint_identity),
                pack_string(manifest.canonical_weight_export_codec_identity),
                pack_string(manifest.canonical_weight_content_identity)))
    return b"".join(out)


def checkpoint_identity(manifest: CheckpointManifestV1) -> str:
    return _digest(CHECKPOINT_ID_PREFIX, canonical_checkpoint_manifest_bytes(manifest))


def _decode_checkpoint_manifest(reader: _Reader) -> CheckpointManifestV1:
    values = tuple(reader.string() for _ in range(10))
    return CheckpointManifestV1(
        model_architecture_config_identity=values[2],
        phase5_logical_model_input_contract_identity=values[3],
        phase5_encoded_model_input_contract_identity=values[4],
        phase5_batch_layout_contract_identity=values[5],
        card_vocabulary_identity=values[6],
        dataset_identity=values[7],
        dataset_split_identity=values[8],
        training_contract_identity=values[9],
        parent_checkpoint_identity=(reader.string() if reader.u8() else None),
        canonical_weight_export_codec_identity=reader.string(),
        canonical_weight_content_identity=reader.string(),
        schema_id=values[0],
        checkpoint_schema_version=values[1],
    )


@dataclasses.dataclass(frozen=True)
class CheckpointArtifactV1:
    checkpoint_identity: str
    manifest: CheckpointManifestV1
    weight_export_bytes: bytes


def canonical_checkpoint_artifact_bytes(
    artifact: CheckpointArtifactV1,
) -> bytes:
    """Encode the immutable physical wrapper around semantic checkpoint data.

    The wrapper is intentionally small and OCGForge-owned.  It contains the
    canonical manifest and canonical weight body, never a framework-native
    object or optimizer/training state.  The checkpoint identity is a
    declaration which the decoder recomputes from both referenced contents.
    """

    _validate_identity(artifact.checkpoint_identity, (CHECKPOINT_ID_PREFIX,), "checkpoint identity")
    manifest_bytes = canonical_checkpoint_manifest_bytes(artifact.manifest)
    if checkpoint_identity(artifact.manifest) != artifact.checkpoint_identity:
        raise CodecError("checkpoint identity does not match manifest")
    architecture_identity, _ = decode_weight_export_bytes(artifact.weight_export_bytes)
    if architecture_identity != artifact.manifest.model_architecture_config_identity:
        raise CodecError("weight architecture does not match checkpoint manifest")
    if weight_content_identity(artifact.weight_export_bytes) != artifact.manifest.canonical_weight_content_identity:
        raise CodecError("weight content does not match checkpoint manifest")
    return b"".join((pack_string(CHECKPOINT_ARTIFACT_SCHEMA_ID),
                     pack_string(artifact.checkpoint_identity),
                     pack_bytes(manifest_bytes),
                     pack_bytes(artifact.weight_export_bytes)))


def encode_checkpoint_artifact(artifact: CheckpointArtifactV1) -> bytes:
    return canonical_checkpoint_artifact_bytes(artifact)


def decode_checkpoint_artifact(artifact_bytes: bytes) -> CheckpointArtifactV1:
    reader = _Reader(artifact_bytes)
    if reader.string() != CHECKPOINT_ARTIFACT_SCHEMA_ID:
        raise CodecError("unknown checkpoint artifact schema")
    declared_identity = reader.string()
    manifest_bytes = reader.bytes_u64()
    weight_bytes = reader.bytes_u64()
    if not reader.done():
        raise CodecError("checkpoint artifact has trailing bytes")
    manifest_reader = _Reader(manifest_bytes)
    values = manifest_reader
    manifest = _decode_checkpoint_manifest(values)
    if not manifest_reader.done() or canonical_checkpoint_manifest_bytes(manifest) != manifest_bytes:
        raise CodecError("checkpoint manifest is not canonical")
    architecture_identity, _ = decode_weight_export_bytes(weight_bytes)
    if architecture_identity != manifest.model_architecture_config_identity:
        raise CodecError("weight architecture does not match checkpoint manifest")
    if weight_content_identity(weight_bytes) != manifest.canonical_weight_content_identity:
        raise CodecError("checkpoint weight content digest mismatch")
    if checkpoint_identity(manifest) != declared_identity:
        raise CodecError("checkpoint identity does not match manifest")
    decoded = CheckpointArtifactV1(declared_identity, manifest, bytes(weight_bytes))
    if canonical_checkpoint_artifact_bytes(decoded) != bytes(artifact_bytes):
        raise CodecError("checkpoint artifact is not canonical")
    return decoded


@dataclasses.dataclass(frozen=True)
class CorpusSampleV1:
    bc_sample_identity: str
    trajectory_record_id: str
    episode_semantic_id: str
    public_semantic_decision_id: str
    model_input_identity: str
    selected_public_action_key: str
    partition: str
    candidate_ordinal: int
    ordered_candidate_domain_identity: str
    state_rows: tuple[tuple[float, ...], ...]
    candidate_rows: tuple[tuple[float, ...], ...]
    routing_keys: tuple[str, ...]
    public_candidate_domain_digest: Optional[str] = None
    perspective_player: int = 0
    decision_index: int = 0


@dataclasses.dataclass(frozen=True)
class DerivedCorpusV1:
    source_dataset_identity: str
    split_identity: str
    derivation_contract_identity: str
    card_vocabulary_identity: str
    episode_ids: tuple[str, ...]
    samples: tuple[CorpusSampleV1, ...]


def _row_bytes(row: Sequence[float], width: int) -> bytes:
    if len(row) != width:
        raise CodecError("numeric row width mismatch")
    return b"".join(f32_bytes(value) for value in row)


def canonical_bc_sample_identity_bytes(sample: CorpusSampleV1) -> bytes:
    _validate_prefixed_digest(sample.trajectory_record_id,
                              "trajectory_record.v1.", "trajectory record identity")
    _validate_lower_hex_digest(sample.episode_semantic_id, "episode identity")
    _validate_lower_hex_digest(sample.public_semantic_decision_id,
                               "public decision identity")
    _validate_prefixed_digest(sample.model_input_identity,
                              "model_input.v1.", "model-input identity")
    _validate_public_action_key(sample.selected_public_action_key,
                                "selected public action key")
    if not isinstance(sample.candidate_ordinal, int) or sample.candidate_ordinal < 0:
        raise CodecError("candidate ordinal is invalid")
    return b"".join((pack_string(BC_SAMPLE_IDENTITY_DOMAIN),
                     pack_string(BC_SAMPLE_IDENTITY_DOMAIN),
                     pack_string(sample.trajectory_record_id),
                     pack_string(sample.episode_semantic_id),
                     pack_string(sample.public_semantic_decision_id),
                     pack_string(sample.model_input_identity),
                     pack_string(sample.selected_public_action_key),
                     pack_u32(sample.candidate_ordinal)))


def bc_sample_identity(sample: CorpusSampleV1) -> str:
    return _digest(BC_SAMPLE_IDENTITY_PREFIX,
                   canonical_bc_sample_identity_bytes(sample))


def _partition_bucket(episode_semantic_id: str) -> int:
    _validate_lower_hex_digest(episode_semantic_id, "episode identity")
    body = pack_string(SPLIT_PARTITION_IDENTITY) + pack_string(episode_semantic_id)
    return int.from_bytes(hashlib.sha256(body).digest()[:8], "big") % 1000


def partition_for_episode(episode_semantic_id: str) -> str:
    bucket = _partition_bucket(episode_semantic_id)
    if bucket < 800:
        return "train"
    if bucket < 900:
        return "validation"
    return "test"


def canonical_split_identity_bytes(
    source_dataset_identity: str,
    train_episode_ids: Sequence[str],
    validation_episode_ids: Sequence[str],
    test_episode_ids: Sequence[str],
) -> bytes:
    _validate_lower_hex_digest(source_dataset_identity, "source dataset identity")
    groups = (tuple(train_episode_ids), tuple(validation_episode_ids), tuple(test_episode_ids))
    all_ids: list[str] = []
    for group in groups:
        if tuple(group) != tuple(sorted(set(group))):
            raise CodecError("split episode identities are not sorted and unique")
        for episode in group:
            _validate_lower_hex_digest(episode, "split episode identity")
            all_ids.append(episode)
    if len(set(all_ids)) != len(all_ids):
        raise CodecError("split episode appears in multiple partitions")
    return b"".join((pack_string(SPLIT_IDENTITY_DOMAIN),
                     pack_string(SPLIT_IDENTITY_DOMAIN),
                     pack_string(source_dataset_identity),
                     pack_string(SPLIT_CONTRACT_ID),
                     pack_string(SPLIT_PARTITION_IDENTITY),
                     pack_u32(len(groups[0])),
                     b"".join(pack_string(value) for value in groups[0]),
                     pack_u32(len(groups[1])),
                     b"".join(pack_string(value) for value in groups[1]),
                     pack_u32(len(groups[2])),
                     b"".join(pack_string(value) for value in groups[2])))


def split_identity(
    source_dataset_identity: str,
    train_episode_ids: Sequence[str],
    validation_episode_ids: Sequence[str],
    test_episode_ids: Sequence[str],
) -> str:
    return _digest(SPLIT_IDENTITY_PREFIX, canonical_split_identity_bytes(
        source_dataset_identity, train_episode_ids,
        validation_episode_ids, test_episode_ids))


def _validate_corpus_sample(sample: CorpusSampleV1, episode_ids: Sequence[str]) -> None:
    if not isinstance(sample.bc_sample_identity, str):
        raise CodecError("sample identity is not text")
    if not isinstance(sample.trajectory_record_id, str):
        raise CodecError("trajectory record identity is not text")
    _validate_lower_hex_digest(sample.episode_semantic_id, "sample episode identity")
    _validate_lower_hex_digest(sample.public_semantic_decision_id,
                               "sample public decision identity")
    _validate_prefixed_digest(sample.model_input_identity,
                              "model_input.v1.", "sample model-input identity")
    _validate_public_action_key(sample.selected_public_action_key,
                                "sample selected public action key")
    if sample.partition not in ("train", "validation", "test"):
        raise CodecError("unknown corpus partition")
    if sample.perspective_player not in (0, 1) or sample.decision_index < 0:
        raise CodecError("sample decision context is invalid")
    if sample.public_candidate_domain_digest is not None:
        _validate_lower_hex_digest(sample.public_candidate_domain_digest,
                                   "sample Phase-5 candidate domain digest")
    if sample.episode_semantic_id not in episode_ids:
        raise CodecError("sample episode is absent from corpus episode identities")
    if len(sample.candidate_rows) == 0 or len(sample.candidate_rows) != len(sample.routing_keys):
        raise CodecError("candidate/key cardinality mismatch")
    if not 0 <= sample.candidate_ordinal < len(sample.candidate_rows):
        raise CodecError("candidate ordinal is outside the exact domain")
    if sample.routing_keys[sample.candidate_ordinal] != sample.selected_public_action_key:
        raise CodecError("selected key is not paired with the label ordinal")
    if len(set(sample.routing_keys)) != len(sample.routing_keys):
        raise CodecError("duplicate routing key")
    for key in sample.routing_keys:
        _validate_public_action_key(key, "sample routing key")
    expected_domain_identity = ordered_candidate_domain_identity(
        sample.routing_keys, sample.public_candidate_domain_digest
    )
    if sample.ordered_candidate_domain_identity != expected_domain_identity:
        raise CodecError("sample ordered candidate-domain identity does not match the source domain")
    if sample.bc_sample_identity != bc_sample_identity(sample):
        raise CodecError("sample identity does not match canonical sample fields")
    for row in sample.state_rows:
        _row_bytes(row, STATE_ROW_WIDTH)
    for row in sample.candidate_rows:
        _row_bytes(row, CANDIDATE_ROW_WIDTH)


def canonical_corpus_bytes(corpus: DerivedCorpusV1) -> bytes:
    if tuple(corpus.episode_ids) != tuple(sorted(set(corpus.episode_ids))):
        raise CodecError("corpus episode identities are not sorted and unique")
    _validate_lower_hex_digest(corpus.source_dataset_identity,
                               "corpus source dataset identity")
    _validate_prefixed_digest(corpus.split_identity, SPLIT_IDENTITY_PREFIX,
                              "corpus split identity")
    _validate_prefixed_digest(corpus.card_vocabulary_identity,
                              "model_card_vocabulary.v1.",
                              "corpus vocabulary identity")
    for episode in corpus.episode_ids:
        _validate_lower_hex_digest(episode, "corpus episode identity")
    if corpus.derivation_contract_identity != NUMERIC_PROJECTION_CONTRACT_ID:
        raise CodecError("unknown corpus derivation contract")
    for sample in corpus.samples:
        _validate_corpus_sample(sample, corpus.episode_ids)
    if not any(sample.partition == "train" for sample in corpus.samples):
        raise CodecError("corpus has no train sample")
    out = [pack_string(CORPUS_SCHEMA_ID), pack_string(corpus.source_dataset_identity),
           pack_string(corpus.split_identity), pack_string(corpus.derivation_contract_identity),
           pack_string(corpus.card_vocabulary_identity), _string_vector(corpus.episode_ids),
           pack_u32(len(corpus.samples))]
    for sample in corpus.samples:
        out.extend((pack_string(sample.bc_sample_identity), pack_string(sample.trajectory_record_id),
                    pack_string(sample.episode_semantic_id), pack_string(sample.public_semantic_decision_id),
                    pack_string(sample.model_input_identity),
                    _optional_string(sample.public_candidate_domain_digest),
                    pack_u8(sample.perspective_player), pack_u64(sample.decision_index),
                    pack_string(sample.selected_public_action_key),
                    pack_string(sample.partition), pack_u32(sample.candidate_ordinal),
                    pack_string(sample.ordered_candidate_domain_identity), pack_u32(len(sample.state_rows))))
        out.extend(_row_bytes(row, STATE_ROW_WIDTH) for row in sample.state_rows)
        out.append(pack_u32(len(sample.candidate_rows)))
        out.extend(_row_bytes(row, CANDIDATE_ROW_WIDTH) for row in sample.candidate_rows)
        out.append(_string_vector(sample.routing_keys))
    return b"".join(out)


def derived_corpus_content_identity(body: bytes) -> str:
    return _digest(CORPUS_ARTIFACT_ID_PREFIX, bytes(body))


def encode_corpus_artifact(corpus: DerivedCorpusV1) -> bytes:
    body = canonical_corpus_bytes(corpus)
    return pack_string(derived_corpus_content_identity(body)) + body


def decode_corpus_artifact(artifact: bytes) -> DerivedCorpusV1:
    reader = _Reader(artifact)
    declared_identity = reader.string()
    body = reader.data[reader.offset:]
    if declared_identity != derived_corpus_content_identity(body):
        raise CodecError("derived corpus content digest mismatch")
    body_reader = _Reader(body)
    if body_reader.string() != CORPUS_SCHEMA_ID:
        raise CodecError("unknown corpus schema")
    source_dataset_identity = body_reader.string()
    split_identity = body_reader.string()
    derivation_identity = body_reader.string()
    vocabulary_identity = body_reader.string()
    episode_count = body_reader.u32()
    episode_ids = tuple(body_reader.string() for _ in range(episode_count))
    sample_count = body_reader.u32()
    samples = []
    for _ in range(sample_count):
        fields = [body_reader.string() for _ in range(5)]
        phase5_domain_digest = body_reader.optional_string()
        perspective_player = body_reader.u8()
        decision_index = body_reader.u64()
        fields.append(body_reader.string())
        partition = body_reader.string()
        ordinal = body_reader.u32()
        domain_identity = body_reader.string()
        state_count = body_reader.u32()
        state_rows = []
        for _ in range(state_count):
            state_rows.append(tuple(f32_value(body_reader.take(4)) for _ in range(STATE_ROW_WIDTH)))
        candidate_count = body_reader.u32()
        candidate_rows = []
        for _ in range(candidate_count):
            candidate_rows.append(tuple(f32_value(body_reader.take(4)) for _ in range(CANDIDATE_ROW_WIDTH)))
        key_count = body_reader.u32()
        keys = tuple(body_reader.string() for _ in range(key_count))
        samples.append(CorpusSampleV1(
            bc_sample_identity=fields[0], trajectory_record_id=fields[1], episode_semantic_id=fields[2],
            public_semantic_decision_id=fields[3], model_input_identity=fields[4],
            selected_public_action_key=fields[5], partition=partition, candidate_ordinal=ordinal,
            ordered_candidate_domain_identity=domain_identity, state_rows=tuple(state_rows),
            candidate_rows=tuple(candidate_rows), routing_keys=keys,
            public_candidate_domain_digest=phase5_domain_digest,
            perspective_player=perspective_player, decision_index=decision_index))
    if not body_reader.done():
        raise CodecError("derived corpus has trailing bytes")
    corpus = DerivedCorpusV1(source_dataset_identity, split_identity, derivation_identity,
                             vocabulary_identity, episode_ids, tuple(samples))
    if canonical_corpus_bytes(corpus) != body:
        raise CodecError("derived corpus is not canonical")
    return corpus


def ordered_candidate_domain_identity(
    routing_keys: Sequence[str],
    public_candidate_domain_digest: Optional[str] = None,
) -> str:
    if not routing_keys or len(set(routing_keys)) != len(routing_keys):
        raise CodecError("ordered candidate domain is empty or duplicated")
    for key in routing_keys:
        _validate_public_action_key(key, "ordered candidate routing key")
    if public_candidate_domain_digest is not None:
        _validate_lower_hex_digest(
            public_candidate_domain_digest,
            "Phase-5 public candidate domain digest",
        )
        return public_candidate_domain_digest
    body = pack_string(ORDERED_DOMAIN_ID_DOMAIN) + pack_u32(len(routing_keys))
    body += b"".join(pack_string(key) for key in routing_keys)
    return _digest(ORDERED_DOMAIN_ID_PREFIX, body)


def validate_ordered_candidate_domain_identity(
    identity: str,
    routing_keys: Sequence[str],
) -> str:
    _validate_ordered_domain_identity(identity, "ordered candidate-domain identity")
    if identity.startswith(ORDERED_DOMAIN_ID_PREFIX):
        if identity != ordered_candidate_domain_identity(routing_keys):
            raise CodecError("fallback ordered domain identity does not match keys")
    return identity


@dataclasses.dataclass(frozen=True)
class Task4NumericModelInputV1:
    model_input_identity: str
    ordered_candidate_domain_identity: str
    public_candidate_domain_digest: Optional[str]
    public_semantic_decision_id: Optional[str]
    perspective_player: int
    decision_index: int
    state_rows: tuple[tuple[float, ...], ...]
    candidate_rows: tuple[tuple[float, ...], ...]
    routing_keys: tuple[str, ...]
    numeric_input_identity: str


def _validate_numeric_model_input_fields(
    value: Task4NumericModelInputV1,
) -> None:
    _validate_prefixed_digest(value.model_input_identity,
                              "model_input.v1.", "numeric model-input identity")
    if value.public_candidate_domain_digest is not None:
        _validate_lower_hex_digest(value.public_candidate_domain_digest,
                                   "numeric Phase-5 domain digest")
    if value.public_semantic_decision_id is not None:
        _validate_lower_hex_digest(value.public_semantic_decision_id,
                                   "numeric public decision identity")
    if value.perspective_player not in (0, 1) or value.decision_index < 0:
        raise CodecError("numeric model-input decision context is invalid")
    if len(value.candidate_rows) == 0 or len(value.candidate_rows) != len(value.routing_keys):
        raise CodecError("numeric model-input candidate/key cardinality mismatch")
    expected_domain = ordered_candidate_domain_identity(
        value.routing_keys, value.public_candidate_domain_digest
    )
    if value.ordered_candidate_domain_identity != expected_domain:
        raise CodecError("numeric model-input domain identity does not match source domain")
    for row in value.state_rows:
        _row_bytes(row, STATE_ROW_WIDTH)
    for row in value.candidate_rows:
        _row_bytes(row, CANDIDATE_ROW_WIDTH)


def canonical_numeric_model_input_bytes(
    value: Task4NumericModelInputV1,
) -> bytes:
    _validate_numeric_model_input_fields(value)
    out = [pack_string(NUMERIC_MODEL_INPUT_SCHEMA_ID),
           pack_string(NUMERIC_MODEL_INPUT_SCHEMA_ID),
           pack_string(value.model_input_identity),
           pack_string(value.ordered_candidate_domain_identity),
           _optional_string(value.public_candidate_domain_digest),
           _optional_string(value.public_semantic_decision_id),
           pack_u8(value.perspective_player), pack_u64(value.decision_index),
           pack_u32(len(value.state_rows))]
    out.extend(_row_bytes(row, STATE_ROW_WIDTH) for row in value.state_rows)
    out.append(pack_u32(len(value.candidate_rows)))
    out.extend(_row_bytes(row, CANDIDATE_ROW_WIDTH) for row in value.candidate_rows)
    out.append(_string_vector(value.routing_keys))
    return b"".join(out)


def numeric_model_input_identity(value: Task4NumericModelInputV1) -> str:
    return _digest(NUMERIC_MODEL_INPUT_ID_PREFIX,
                   canonical_numeric_model_input_bytes(value))


def validate_numeric_model_input(
    value: Task4NumericModelInputV1,
) -> Task4NumericModelInputV1:
    if value.numeric_input_identity != numeric_model_input_identity(value):
        raise CodecError("numeric model-input identity does not match canonical rows")
    return value


def make_numeric_model_input(
    *,
    model_input_identity: str,
    state_rows: Sequence[Sequence[float]],
    candidate_rows: Sequence[Sequence[float]],
    routing_keys: Sequence[str],
    public_candidate_domain_digest: Optional[str],
    public_semantic_decision_id: Optional[str],
    perspective_player: int,
    decision_index: int,
) -> Task4NumericModelInputV1:
    try:
        state = tuple(tuple(row) for row in state_rows)
        candidates = tuple(tuple(row) for row in candidate_rows)
        keys = tuple(routing_keys)
    except TypeError as error:
        raise CodecError("numeric model-input rows are not sequences") from error
    provisional = Task4NumericModelInputV1(
        model_input_identity=model_input_identity,
        ordered_candidate_domain_identity=ordered_candidate_domain_identity(
            keys, public_candidate_domain_digest
        ),
        public_candidate_domain_digest=public_candidate_domain_digest,
        public_semantic_decision_id=public_semantic_decision_id,
        perspective_player=perspective_player,
        decision_index=decision_index,
        state_rows=state,
        candidate_rows=candidates,
        routing_keys=keys,
        numeric_input_identity="",
    )
    return dataclasses.replace(
        provisional, numeric_input_identity=numeric_model_input_identity(provisional)
    )


@dataclasses.dataclass(frozen=True)
class CorpusSourceSampleAuthorityV1:
    bc_sample_identity: str
    trajectory_record_id: str
    episode_semantic_id: str
    public_semantic_decision_id: str
    model_input_identity: str
    selected_public_action_key: str
    candidate_ordinal: int
    ordered_candidate_domain_identity: str
    public_candidate_domain_digest: Optional[str]
    perspective_player: int
    decision_index: int


def source_sample_authority(sample: CorpusSampleV1) -> CorpusSourceSampleAuthorityV1:
    return CorpusSourceSampleAuthorityV1(
        bc_sample_identity=sample.bc_sample_identity,
        trajectory_record_id=sample.trajectory_record_id,
        episode_semantic_id=sample.episode_semantic_id,
        public_semantic_decision_id=sample.public_semantic_decision_id,
        model_input_identity=sample.model_input_identity,
        selected_public_action_key=sample.selected_public_action_key,
        candidate_ordinal=sample.candidate_ordinal,
        ordered_candidate_domain_identity=sample.ordered_candidate_domain_identity,
        public_candidate_domain_digest=sample.public_candidate_domain_digest,
        perspective_player=sample.perspective_player,
        decision_index=sample.decision_index,
    )


@dataclasses.dataclass(frozen=True)
class CorpusAdmissionAuthorityV1:
    expected_artifact_identity: str
    source_dataset_identity: str
    split_identity: str
    card_vocabulary_identity: str
    train_episode_ids: tuple[str, ...]
    validation_episode_ids: tuple[str, ...]
    test_episode_ids: tuple[str, ...]
    source_samples: tuple[CorpusSourceSampleAuthorityV1, ...]


def _validate_source_sample_authority(
    sample: CorpusSourceSampleAuthorityV1,
) -> None:
    _validate_prefixed_digest(sample.bc_sample_identity,
                              BC_SAMPLE_IDENTITY_PREFIX,
                              "authority BC sample identity")
    _validate_prefixed_digest(sample.trajectory_record_id,
                              "trajectory_record.v1.",
                              "authority trajectory identity")
    _validate_lower_hex_digest(sample.episode_semantic_id,
                               "authority episode identity")
    _validate_lower_hex_digest(sample.public_semantic_decision_id,
                               "authority public decision identity")
    _validate_prefixed_digest(sample.model_input_identity,
                              "model_input.v1.",
                              "authority model-input identity")
    _validate_public_action_key(sample.selected_public_action_key,
                                "authority selected public action key")
    if not isinstance(sample.candidate_ordinal, int) or sample.candidate_ordinal < 0:
        raise CodecError("authority candidate ordinal is invalid")
    _validate_ordered_domain_identity(sample.ordered_candidate_domain_identity,
                                      "authority ordered domain identity")
    if sample.public_candidate_domain_digest is not None:
        _validate_lower_hex_digest(sample.public_candidate_domain_digest,
                                   "authority Phase-5 domain digest")
    if sample.perspective_player not in (0, 1) or sample.decision_index < 0:
        raise CodecError("authority decision context is invalid")
    if sample.bc_sample_identity != bc_sample_identity(CorpusSampleV1(
        bc_sample_identity=sample.bc_sample_identity,
        trajectory_record_id=sample.trajectory_record_id,
        episode_semantic_id=sample.episode_semantic_id,
        public_semantic_decision_id=sample.public_semantic_decision_id,
        model_input_identity=sample.model_input_identity,
        selected_public_action_key=sample.selected_public_action_key,
        partition="train",
        candidate_ordinal=sample.candidate_ordinal,
        ordered_candidate_domain_identity=sample.ordered_candidate_domain_identity,
        state_rows=(), candidate_rows=(), routing_keys=(),
    )):
        raise CodecError("authority BC sample identity is not canonical")


def canonical_corpus_authority_bytes(
    authority: CorpusAdmissionAuthorityV1,
) -> bytes:
    _validate_prefixed_digest(authority.expected_artifact_identity,
                              CORPUS_ARTIFACT_ID_PREFIX,
                              "authority expected corpus artifact identity")
    _validate_lower_hex_digest(authority.source_dataset_identity,
                               "authority source dataset identity")
    _validate_prefixed_digest(authority.split_identity, SPLIT_IDENTITY_PREFIX,
                              "authority split identity")
    _validate_prefixed_digest(authority.card_vocabulary_identity,
                              "model_card_vocabulary.v1.",
                              "authority vocabulary identity")
    split_groups = (
        tuple(authority.train_episode_ids),
        tuple(authority.validation_episode_ids),
        tuple(authority.test_episode_ids),
    )
    for group in split_groups:
        if tuple(group) != tuple(sorted(set(group))):
            raise CodecError("authority split episode identities are not sorted and unique")
        for episode in group:
            _validate_lower_hex_digest(episode, "authority split episode identity")
    expected_split = split_identity(authority.source_dataset_identity, *split_groups)
    if expected_split != authority.split_identity:
        raise CodecError("authority split identity does not match its episode vectors")
    if tuple(sample.bc_sample_identity for sample in authority.source_samples) != tuple(
        sorted(sample.bc_sample_identity for sample in authority.source_samples)
    ):
        raise CodecError("authority source samples are not in canonical order")
    if len({sample.bc_sample_identity for sample in authority.source_samples}) != len(authority.source_samples):
        raise CodecError("authority source samples are duplicated")
    for sample in authority.source_samples:
        _validate_source_sample_authority(sample)
    out = [pack_string(CORPUS_AUTHORITY_SCHEMA_ID),
           pack_string(authority.expected_artifact_identity),
           pack_string(authority.source_dataset_identity),
           pack_string(authority.split_identity),
           pack_string(authority.card_vocabulary_identity),
           _string_vector(split_groups[0]), _string_vector(split_groups[1]),
           _string_vector(split_groups[2]), pack_u32(len(authority.source_samples))]
    for sample in authority.source_samples:
        out.extend((pack_string(sample.bc_sample_identity),
                    pack_string(sample.trajectory_record_id),
                    pack_string(sample.episode_semantic_id),
                    pack_string(sample.public_semantic_decision_id),
                    pack_string(sample.model_input_identity),
                    pack_string(sample.selected_public_action_key),
                    pack_u32(sample.candidate_ordinal),
                    pack_string(sample.ordered_candidate_domain_identity),
                    _optional_string(sample.public_candidate_domain_digest),
                    pack_u8(sample.perspective_player),
                    pack_u64(sample.decision_index)))
    return b"".join(out)


def encode_corpus_authority_artifact(
    authority: CorpusAdmissionAuthorityV1,
) -> bytes:
    body = canonical_corpus_authority_bytes(authority)
    return pack_string(CORPUS_AUTHORITY_ID_PREFIX + hashlib.sha256(body).hexdigest()) + body


def decode_corpus_authority_artifact(
    artifact: bytes,
) -> CorpusAdmissionAuthorityV1:
    reader = _Reader(artifact)
    declared_identity = reader.string()
    body = reader.data[reader.offset:]
    if declared_identity != CORPUS_AUTHORITY_ID_PREFIX + hashlib.sha256(body).hexdigest():
        raise CodecError("corpus authority content digest mismatch")
    body_reader = _Reader(body)
    if body_reader.string() != CORPUS_AUTHORITY_SCHEMA_ID:
        raise CodecError("unknown corpus authority schema")
    expected_artifact_identity = body_reader.string()
    source_dataset_identity = body_reader.string()
    split_identity_value = body_reader.string()
    card_vocabulary_identity = body_reader.string()
    groups = []
    for _ in range(3):
        groups.append(tuple(body_reader.string() for _ in range(body_reader.u32())))
    samples = []
    for _ in range(body_reader.u32()):
        bc_sample_identity_value = body_reader.string()
        trajectory_record_id = body_reader.string()
        episode_semantic_id = body_reader.string()
        public_decision_id = body_reader.string()
        model_input_identity_value = body_reader.string()
        selected_key = body_reader.string()
        ordinal = body_reader.u32()
        domain_identity = body_reader.string()
        phase5_domain_digest = body_reader.optional_string()
        perspective = body_reader.u8()
        decision_index = body_reader.u64()
        samples.append(CorpusSourceSampleAuthorityV1(
            bc_sample_identity=bc_sample_identity_value,
            trajectory_record_id=trajectory_record_id,
            episode_semantic_id=episode_semantic_id,
            public_semantic_decision_id=public_decision_id,
            model_input_identity=model_input_identity_value,
            selected_public_action_key=selected_key,
            candidate_ordinal=ordinal,
            ordered_candidate_domain_identity=domain_identity,
            public_candidate_domain_digest=phase5_domain_digest,
            perspective_player=perspective,
            decision_index=decision_index,
        ))
    if not body_reader.done():
        raise CodecError("corpus authority has trailing bytes")
    authority = CorpusAdmissionAuthorityV1(
        expected_artifact_identity=expected_artifact_identity,
        source_dataset_identity=source_dataset_identity,
        split_identity=split_identity_value,
        card_vocabulary_identity=card_vocabulary_identity,
        train_episode_ids=groups[0],
        validation_episode_ids=groups[1],
        test_episode_ids=groups[2],
        source_samples=tuple(samples),
    )
    if canonical_corpus_authority_bytes(authority) != body:
        raise CodecError("corpus authority is not canonical")
    if (CORPUS_AUTHORITY_ID_PREFIX + hashlib.sha256(body).hexdigest()) != declared_identity:
        raise CodecError("corpus authority identity is not canonical")
    return authority


def admit_corpus_artifact(
    artifact: bytes,
    authority: CorpusAdmissionAuthorityV1,
) -> DerivedCorpusV1:
    """Admit a derived corpus only with independently supplied authority.

    The outer artifact digest proves byte integrity, not DatasetManifest
    membership.  The caller must therefore provide the expected artifact
    identity and the identities/materialized membership obtained from the
    trusted C++ producer and Task-2 path.
    """

    canonical_corpus_authority_bytes(authority)
    corpus = decode_corpus_artifact(artifact)
    actual_artifact_identity = derived_corpus_content_identity(
        canonical_corpus_bytes(corpus)
    )
    _validate_prefixed_digest(
        authority.expected_artifact_identity,
        CORPUS_ARTIFACT_ID_PREFIX,
        "expected corpus artifact identity",
    )
    if actual_artifact_identity != authority.expected_artifact_identity:
        raise CodecError("corpus artifact identity is not the pinned trusted artifact")
    if corpus.source_dataset_identity != authority.source_dataset_identity:
        raise CodecError("corpus source dataset identity is not admitted")
    if corpus.card_vocabulary_identity != authority.card_vocabulary_identity:
        raise CodecError("corpus vocabulary identity is not admitted")
    expected_split_identity = split_identity(
        authority.source_dataset_identity,
        authority.train_episode_ids,
        authority.validation_episode_ids,
        authority.test_episode_ids,
    )
    if expected_split_identity != authority.split_identity or corpus.split_identity != authority.split_identity:
        raise CodecError("corpus split identity is not admitted")
    expected_partitions = {
        episode: "train" for episode in authority.train_episode_ids
    }
    expected_partitions.update({
        episode: "validation" for episode in authority.validation_episode_ids
    })
    expected_partitions.update({
        episode: "test" for episode in authority.test_episode_ids
    })
    if len(expected_partitions) != (
        len(authority.train_episode_ids) +
        len(authority.validation_episode_ids) +
        len(authority.test_episode_ids)
    ):
        raise CodecError("admitted split episode appears in multiple partitions")
    for episode, expected_partition in expected_partitions.items():
        if partition_for_episode(episode) != expected_partition:
            raise CodecError("admitted split does not follow the deterministic episode partition")
    if tuple(corpus.episode_ids) != tuple(sorted(expected_partitions)):
        raise CodecError("corpus episode identities do not match the admitted split")
    expected_samples = {
        sample.bc_sample_identity: sample for sample in authority.source_samples
    }
    if len(expected_samples) != len(authority.source_samples):
        raise CodecError("admitted source samples are duplicated")
    if len(corpus.samples) != len(expected_samples):
        raise CodecError("corpus sample membership cardinality is not admitted")
    actual_ids: set[str] = set()
    for sample in corpus.samples:
        if sample.bc_sample_identity in actual_ids:
            raise CodecError("corpus sample identity is duplicated")
        actual_ids.add(sample.bc_sample_identity)
        if expected_partitions.get(sample.episode_semantic_id) != sample.partition:
            raise CodecError("corpus sample partition does not match the admitted split")
        expected = expected_samples.get(sample.bc_sample_identity)
        if expected is None or source_sample_authority(sample) != expected:
            raise CodecError("corpus sample is not an admitted source sample")
    if actual_ids != set(expected_samples):
        raise CodecError("corpus source sample membership is incomplete")
    return corpus


@dataclasses.dataclass(frozen=True)
class InferenceRequestV1:
    checkpoint_identity: str
    model_input_identity: str
    ordered_candidate_domain_identity: str
    public_semantic_decision_id: Optional[str]
    perspective_player: int
    decision_index: int
    routing_keys: tuple[str, ...]
    request_identity: str
    schema_id: str = INFERENCE_REQUEST_SCHEMA_ID


def canonical_inference_request_bytes(request: InferenceRequestV1) -> bytes:
    if request.schema_id != INFERENCE_REQUEST_SCHEMA_ID:
        raise CodecError("unknown inference request schema")
    _validate_prefixed_digest(request.checkpoint_identity,
                              CHECKPOINT_ID_PREFIX,
                              "inference checkpoint identity")
    _validate_prefixed_digest(request.model_input_identity,
                              "model_input.v1.",
                              "inference model-input identity")
    _validate_ordered_domain_identity(request.ordered_candidate_domain_identity,
                                      "inference ordered domain identity")
    if request.public_semantic_decision_id is not None:
        _validate_lower_hex_digest(request.public_semantic_decision_id,
                                   "inference public decision identity")
    if request.perspective_player not in (0, 1):
        raise CodecError("invalid inference perspective")
    if request.decision_index < 0:
        raise CodecError("invalid inference decision index")
    return b"".join((pack_string(request.schema_id), pack_string(request.checkpoint_identity),
                     pack_string(request.model_input_identity),
                     pack_string(request.ordered_candidate_domain_identity),
                     _optional_string(request.public_semantic_decision_id),
                     pack_u8(request.perspective_player), pack_u64(request.decision_index)))


def inference_request_identity(request: InferenceRequestV1) -> str:
    return _digest(REQUEST_ID_PREFIX, canonical_inference_request_bytes(request))


def make_inference_request(
    *, checkpoint_identity: str, model_input: Task4NumericModelInputV1,
) -> InferenceRequestV1:
    validate_numeric_model_input(model_input)
    provisional = InferenceRequestV1(
        checkpoint_identity=checkpoint_identity,
        model_input_identity=model_input.model_input_identity,
        ordered_candidate_domain_identity=model_input.ordered_candidate_domain_identity,
        public_semantic_decision_id=model_input.public_semantic_decision_id,
        perspective_player=model_input.perspective_player,
        decision_index=model_input.decision_index,
        routing_keys=model_input.routing_keys,
        request_identity="",
    )
    request_identity = inference_request_identity(provisional)
    return dataclasses.replace(provisional, request_identity=request_identity)


@dataclasses.dataclass(frozen=True)
class InferenceResponseV1:
    request_identity: str
    checkpoint_identity: str
    model_input_identity: str
    ordered_candidate_domain_identity: str
    scores: tuple[float, ...]
    selected_candidate_ordinal: int
    selected_public_action_key: str
    response_identity: str
    schema_id: str = INFERENCE_RESPONSE_SCHEMA_ID


def canonical_selection_envelope_bytes(response: InferenceResponseV1) -> bytes:
    return b"".join((pack_string(RESPONSE_SELECTION_ID_DOMAIN),
                     pack_string(RESPONSE_SELECTION_ID_DOMAIN),
                     pack_string(response.request_identity),
                     pack_string(response.checkpoint_identity),
                     pack_string(response.model_input_identity),
                     pack_string(response.ordered_candidate_domain_identity),
                     pack_u32(response.selected_candidate_ordinal),
                     pack_string(response.selected_public_action_key)))


def inference_response_selection_identity(response: InferenceResponseV1) -> str:
    return _digest(RESPONSE_ID_PREFIX, canonical_selection_envelope_bytes(response))


def make_inference_response(
    request: InferenceRequestV1, scores: Sequence[float], selected_ordinal: int
) -> InferenceResponseV1:
    values = tuple(f32_value(f32_bytes(value)) for value in scores)
    if len(values) != len(request.routing_keys):
        raise CodecError("inference score count does not equal candidate count")
    if not 0 <= selected_ordinal < len(values):
        raise CodecError("selected ordinal is outside candidate domain")
    provisional = InferenceResponseV1(
        request_identity=request.request_identity,
        checkpoint_identity=request.checkpoint_identity,
        model_input_identity=request.model_input_identity,
        ordered_candidate_domain_identity=request.ordered_candidate_domain_identity,
        scores=values,
        selected_candidate_ordinal=selected_ordinal,
        selected_public_action_key=request.routing_keys[selected_ordinal],
        response_identity="",
    )
    return dataclasses.replace(
        provisional, response_identity=inference_response_selection_identity(provisional)
    )
