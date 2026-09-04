"""Task7 exact non-smoke materialization and PyTorch execution adapter.

The C++ ``ygo::phase6`` implementation is the source-validation authority.
This module deliberately accepts only its canonical, already validated sample
bytes.  It does not inspect engine state, reconstruct gameplay semantics, or
provide a model/scorer.  Its only execution responsibility is decoding exact
limbs and masks into immutable typed records backed by ``torch.int64`` and
``torch.bool`` tensors.
"""

from __future__ import annotations

import dataclasses
import hashlib
import struct
from collections.abc import Iterable, Mapping, Sequence
from types import MappingProxyType
from typing import Any, Optional, Tuple

try:
    import torch
except ImportError:  # pragma: no cover - the focused test is torch-gated.
    torch = None  # type: ignore[assignment]


MATERIALIZATION_SCHEMA_ID = "ocgforge.phase6.task7.input_materialization.v1"
CONFIG_SCHEMA_ID = "ocgforge.phase6.task7.input_materialization_config.v1"
CONFIGURATION_IDENTITY_PREFIX = "phase6_task7_input_materialization_config.v1."
PHASE5_CONTRACT_IDS = (
    "ocgforge.model_logical_input.v1",
    "ocgforge.model_encoded_input.v1",
    "ocgforge.model_card_vocabulary.v1",
    "ocgforge.model_input_identity.v1",
    "ocgforge.model_batch_layout.v1",
)
LIMB_ORDER_TOKEN = "u16_most_significant_first"
INTEGER_TENSOR_TYPE_TOKEN = "torch.int64"
BOOLEAN_TENSOR_TYPE_TOKEN = "torch.bool"

CONFIG_CANONICAL_BYTES_LENGTH = 8133
CONFIG_CANONICAL_BYTES_SHA256 = (
    "20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a"
)
CONFIG_CANONICAL_BYTES_PREFIX_HEX = (
    "000000356f6367666f7267652e7068617365362e7461736b372e696e7075745f6d6174657269616c697a6174696f6e5f"
)
CONFIG_CANONICAL_BYTES_SUFFIX_HEX = (
    "495354494e435400000019636861696e5f73746174655f6c656e6774685f736f757263650000000844495354494e4354"
)
CONFIGURATION_IDENTITY = CONFIGURATION_IDENTITY_PREFIX + CONFIG_CANONICAL_BYTES_SHA256


class Task7MaterializationError(ValueError):
    """Raised when canonical Task7 bytes are malformed or detached."""


def _pack_u8(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFF:
        raise Task7MaterializationError("u8 is out of range")
    return struct.pack(">B", value)


def _pack_u16(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFF:
        raise Task7MaterializationError("u16 is out of range")
    return struct.pack(">H", value)


def _pack_u32(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise Task7MaterializationError("u32 is out of range")
    return struct.pack(">I", value)


def _pack_u64(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise Task7MaterializationError("u64 is out of range")
    return struct.pack(">Q", value)


def _pack_string(value: str) -> bytes:
    if not isinstance(value, str):
        raise Task7MaterializationError("canonical string is not text")
    try:
        raw = value.encode("utf-8", "strict")
    except UnicodeError as error:
        raise Task7MaterializationError("canonical string is not UTF-8") from error
    return _pack_u32(len(raw)) + raw


def _pack_vector(values: Iterable[bytes]) -> bytes:
    entries = tuple(values)
    return _pack_u32(len(entries)) + b"".join(entries)


def _pack_optional_string(value: Optional[str]) -> bytes:
    return _pack_u8(0) if value is None else _pack_u8(1) + _pack_string(value)


@dataclasses.dataclass(frozen=True)
class _ReferenceComponentDescriptor:
    name: str
    source_type: str
    presence_rule: str


@dataclasses.dataclass(frozen=True)
class _ReferenceDescriptor:
    name: str
    components: tuple[_ReferenceComponentDescriptor, ...]


@dataclasses.dataclass(frozen=True)
class _ColumnDescriptor:
    name: str
    source_type: str
    limb_count: int
    presence_rule: str
    padding_rule: str


@dataclasses.dataclass(frozen=True)
class _TableDescriptor:
    name: str
    kind: str
    row_order: str
    parent: Optional[str]
    parent_offset: Optional[str]
    row_mask_rule: str
    columns: tuple[_ColumnDescriptor, ...]


@dataclasses.dataclass(frozen=True)
class _RuleDescriptor:
    identifier: str
    value: str


def _column(name: str, source_type: str, limbs: int, presence: str,
            padding: str) -> _ColumnDescriptor:
    return _ColumnDescriptor(name, source_type, limbs, presence, padding)


def _reference_descriptors() -> tuple[_ReferenceDescriptor, ...]:
    return (
        _ReferenceDescriptor(
            "R",
            (
                _ReferenceComponentDescriptor("public_locator_ordinal", "U32", "required"),
                _ReferenceComponentDescriptor("current_entity_ordinal", "P<U32>", "optional"),
            ),
        ),
        _ReferenceDescriptor(
            "OR",
            (
                _ReferenceComponentDescriptor("present", "Bool", "required"),
                _ReferenceComponentDescriptor("reference", "R", "composite_defined"),
            ),
        ),
        _ReferenceDescriptor(
            "CR",
            (
                _ReferenceComponentDescriptor("present", "Bool", "required"),
                _ReferenceComponentDescriptor("kind_code", "U8", "composite_defined"),
                _ReferenceComponentDescriptor("reference", "R", "composite_defined"),
            ),
        ),
        _ReferenceDescriptor(
            "HR",
            (
                _ReferenceComponentDescriptor("present", "Bool", "required"),
                _ReferenceComponentDescriptor("public_locator_ordinal", "U32", "composite_defined"),
            ),
        ),
    )


def _table_descriptors() -> tuple[_TableDescriptor, ...]:
    required = "required"
    optional = "optional"
    composite = "composite_defined"
    zero = "zero"
    false = "false"
    pad_id = "pad_id_zero"
    not_applicable = "not_applicable"
    singleton_mask = "singleton_all_true"
    real_mask = "real_rows_true"
    return (
        _TableDescriptor(
            "sample_header", "singleton", "sample_order", None, None, singleton_mask,
            (
                _column("perspective_player", "U8", 1, required, zero),
                _column("decision_index", "U64", 4, required, zero),
                _column("public_observation_context_kind_code", "P<U16>", 1, optional, zero),
                _column("public_observation_context_player", "P<U8>", 1, optional, zero),
                _column("public_locator_count", "U32", 2, required, zero),
                _column("candidate_count", "U32", 2, required, zero),
            ),
        ),
        _TableDescriptor(
            "globals", "singleton", "sample_order", None, None, singleton_mask,
            (
                _column("duel_flags", "U64", 4, required, zero),
                _column("player_to_act", "P<U8>", 1, optional, zero),
                _column("turn_player", "P<U8>", 1, optional, zero),
                _column("turn_count", "P<U32>", 2, optional, zero),
                _column("phase", "P<U32>", 2, optional, zero),
                _column("chain_length", "U32", 2, required, zero),
                _column("winner", "P<U8>", 1, optional, zero),
                _column("win_reason", "P<U8>", 1, optional, zero),
                _column("terminal", "Bool", 0, required, false),
            ),
        ),
        _TableDescriptor(
            "chain_state", "singleton", "sample_order", None, None, singleton_mask,
            (_column("length", "U32", 2, required, zero),),
        ),
        _TableDescriptor(
            "match_context", "singleton", "sample_order", None, None, singleton_mask,
            (
                _column("perspective_player", "U8", 1, required, zero),
                _column("duel_flags", "U64", 4, required, zero),
                _column("own_decklist_known", "Bool", 0, required, false),
                _column("opponent_decklist_known", "Bool", 0, required, false),
                _column("own_deck_known", "Bool", 0, required, false),
                _column("opponent_deck_known", "Bool", 0, required, false),
            ),
        ),
        _TableDescriptor("life_points", "ragged", "life_point_source_order", None, None, real_mask,
                         (_column("value", "U32", 2, required, zero),)),
        _TableDescriptor(
            "decision_context_references", "ragged",
            "public_observation_context_reference_order", None, None, real_mask,
            (_column("public_locator_ordinal", "U32", 2, required, zero),),
        ),
        _TableDescriptor(
            "zones", "ragged", "public_safe_state_zone_order", None, None, real_mask,
            (
                _column("player", "U8", 1, required, zero),
                _column("kind_code", "U8", 1, required, zero),
                _column("total_count", "U32", 2, required, zero),
                _column("public_identity_count", "U32", 2, required, zero),
                _column("hidden_count", "U32", 2, required, zero),
                _column("player_observable_order", "Bool", 0, required, false),
            ),
        ),
        _TableDescriptor(
            "entities", "ragged", "canonical_locator_order", None, None, real_mask,
            (
                _column("public_locator_ordinal", "U32", 2, required, zero),
                _column("identity_known", "Bool", 0, required, false),
                _column("card_vocabulary_id", "U32", 2, required, pad_id),
                _column("owner", "P<U8>", 1, optional, zero),
                _column("controller", "P<U8>", 1, optional, zero),
                _column("zone_code", "U8", 1, required, zero),
                _column("sequence", "P<U32>", 2, optional, zero),
                _column("overlay_sequence", "P<U32>", 2, optional, zero),
                _column("position_code", "U8", 1, required, zero),
                _column("face_up", "Bool", 0, required, false),
                _column("face_down", "Bool", 0, required, false),
            ),
        ),
        _TableDescriptor(
            "entity_properties", "child", "entity_property_role_order", "entities",
            "entity_property_offsets", real_mask,
            (
                _column("property_role", "U8", 1, required, zero),
                _column("property_present", "Bool", 0, required, false),
                _column("type", "P<U32>", 2, optional, zero),
                _column("attribute", "P<U32>", 2, optional, zero),
                _column("race", "P<U64>", 4, optional, zero),
                _column("attack", "P<I32>", 2, optional, zero),
                _column("defense", "P<I32>", 2, optional, zero),
                _column("base_attack", "P<I32>", 2, optional, zero),
                _column("base_defense", "P<I32>", 2, optional, zero),
                _column("level", "P<U32>", 2, optional, zero),
                _column("rank", "P<U32>", 2, optional, zero),
                _column("link_rating", "P<U32>", 2, optional, zero),
                _column("left_scale", "P<U32>", 2, optional, zero),
                _column("right_scale", "P<U32>", 2, optional, zero),
                _column("status_flags", "P<U32>", 2, optional, zero),
            ),
        ),
        _TableDescriptor(
            "property_link_markers", "child", "property_link_marker_source_order",
            "entity_properties", "property_link_marker_offsets", real_mask,
            (_column("link_marker_code", "U8", 1, required, zero),),
        ),
        _TableDescriptor(
            "property_counters", "child", "property_counter_source_order",
            "entity_properties", "property_counter_offsets", real_mask,
            (
                _column("type", "U32", 2, required, zero),
                _column("count", "U32", 2, required, zero),
            ),
        ),
        _TableDescriptor(
            "relationships", "ragged", "relationship_source_order", None, None, real_mask,
            (
                _column("kind_code", "U8", 1, required, zero),
                _column("source", "R", 0, composite, not_applicable),
                _column("target", "R", 0, composite, not_applicable),
            ),
        ),
        _TableDescriptor(
            "chain_links", "ragged", "chain_link_source_order", None, None, real_mask,
            (
                _column("index", "U32", 2, required, zero),
                _column("activating_player", "P<U8>", 1, optional, zero),
                _column("source", "OR", 0, composite, not_applicable),
                _column("activation_zone_code", "P<U8>", 1, optional, zero),
                _column("effect_description", "P<U64>", 4, optional, zero),
            ),
        ),
        _TableDescriptor(
            "chain_targets", "child", "chain_target_source_order", "chain_links",
            "chain_target_offsets", real_mask,
            (_column("target", "R", 0, composite, not_applicable),),
        ),
        _TableDescriptor(
            "visible_events", "ragged", "visible_event_source_order", None, None, real_mask,
            (
                _column("event_index", "U64", 4, required, zero),
                _column("kind_code", "U8", 1, required, zero),
                _column("player", "P<U8>", 1, optional, zero),
                _column("entity", "HR", 0, composite, not_applicable),
                _column("public_card_vocabulary_id", "P<U32>", 2, optional, zero),
                _column("from_zone_code", "P<U8>", 1, optional, zero),
                _column("to_zone_code", "P<U8>", 1, optional, zero),
                _column("count", "P<U32>", 2, optional, zero),
                _column("amount", "P<I32>", 2, optional, zero),
                _column("counter_type", "P<U32>", 2, optional, zero),
                _column("phase", "P<U32>", 2, optional, zero),
                _column("winner", "P<U8>", 1, optional, zero),
                _column("win_reason", "P<U8>", 1, optional, zero),
                _column("effect_description", "P<U64>", 4, optional, zero),
            ),
        ),
        _TableDescriptor(
            "visible_event_targets", "child", "visible_event_target_source_order",
            "visible_events", "visible_event_target_offsets", real_mask,
            (_column("public_locator_ordinal", "U32", 2, required, zero),),
        ),
        _TableDescriptor("own_main_deck_ids", "ragged", "deck_public_safe_order", None, None, real_mask,
                         (_column("card_vocabulary_id", "U32", 2, required, pad_id),)),
        _TableDescriptor("opponent_main_deck_ids", "ragged", "deck_public_safe_order", None, None, real_mask,
                         (_column("card_vocabulary_id", "U32", 2, required, pad_id),)),
        _TableDescriptor("own_extra_deck_ids", "ragged", "deck_public_safe_order", None, None, real_mask,
                         (_column("card_vocabulary_id", "U32", 2, required, pad_id),)),
        _TableDescriptor("opponent_extra_deck_ids", "ragged", "deck_public_safe_order", None, None, real_mask,
                         (_column("card_vocabulary_id", "U32", 2, required, pad_id),)),
        _TableDescriptor(
            "public_locator_control_sidecar", "control_sidecar", "public_locator_token_order",
            None, None, real_mask, (_column("public_locator_token", "String", 0, required, not_applicable),),
        ),
        _TableDescriptor(
            "candidates", "candidate", "candidate_source_order", None, None, real_mask,
            (
                _column("action_kind_code", "U16", 1, required, zero),
                _column("choice_present", "Bool", 0, required, false),
                _column("choice_kind_code", "U8", 1, required, zero),
                _column("choice_value", "U64", 4, required, zero),
                _column("choice_response_index", "P<U32>", 2, optional, zero),
                _column("source_reference", "CR", 0, composite, not_applicable),
                _column("target_reference", "CR", 0, composite, not_applicable),
                _column("phase", "P<U32>", 2, optional, zero),
                _column("position", "P<U8>", 1, optional, zero),
                _column("source_index", "P<U32>", 2, optional, zero),
                _column("amount", "P<I32>", 2, optional, zero),
                _column("continuation_operation_code", "U8", 1, required, zero),
                _column("submits_engine_response", "Bool", 0, required, false),
            ),
        ),
        _TableDescriptor(
            "routing_key_control_sidecar", "control_sidecar", "candidate_source_order",
            None, None, real_mask, (_column("public_action_key", "String", 0, required, not_applicable),),
        ),
    )


def _rule_descriptors() -> tuple[_RuleDescriptor, ...]:
    return (
        _RuleDescriptor("candidate_cardinality", "N_TO_N"),
        _RuleDescriptor("candidate_order", "SOURCE_ORDER"),
        _RuleDescriptor("candidate_split", "FORBIDDEN"),
        _RuleDescriptor("routing_key_learned_feature", "NO"),
        _RuleDescriptor("raw_locator_learned_feature", "NO"),
        _RuleDescriptor("padding_semantic", "NO"),
        _RuleDescriptor("ragged_authority", "RAGGED_FIRST"),
        _RuleDescriptor("padded_equivalence", "EXACT_UNPAD"),
        _RuleDescriptor("globals_chain_length_source", "DISTINCT"),
        _RuleDescriptor("chain_state_length_source", "DISTINCT"),
    )


def _encode_reference_descriptor(value: _ReferenceDescriptor) -> bytes:
    return _pack_string(value.name) + _pack_vector(
        _pack_string(component.name)
        + _pack_string(component.source_type)
        + _pack_string(component.presence_rule)
        for component in value.components
    )


def _encode_table_descriptor(value: _TableDescriptor) -> bytes:
    return (
        _pack_string(value.name)
        + _pack_string(value.kind)
        + _pack_string(value.row_order)
        + _pack_optional_string(value.parent)
        + _pack_optional_string(value.parent_offset)
        + _pack_string(value.row_mask_rule)
        + _pack_vector(
            _pack_string(column.name)
            + _pack_string(column.source_type)
            + _pack_u8(column.limb_count)
            + _pack_string(column.presence_rule)
            + _pack_string(column.padding_rule)
            for column in value.columns
        )
    )


def canonical_configuration_bytes() -> bytes:
    """Return the frozen Task7 configuration byte stream, with its KAT checked."""
    payload = (
        _pack_string(CONFIG_SCHEMA_ID)
        + _pack_string(MATERIALIZATION_SCHEMA_ID)
        + _pack_vector(_pack_string(value) for value in PHASE5_CONTRACT_IDS)
        + _pack_string(LIMB_ORDER_TOKEN)
        + _pack_string(INTEGER_TENSOR_TYPE_TOKEN)
        + _pack_string(BOOLEAN_TENSOR_TYPE_TOKEN)
        + _pack_vector(_encode_reference_descriptor(value) for value in _reference_descriptors())
        + _pack_vector(_encode_table_descriptor(value) for value in _table_descriptors())
        + _pack_vector(
            _pack_string(value.identifier) + _pack_string(value.value)
            for value in _rule_descriptors()
        )
    )
    digest = hashlib.sha256(payload).hexdigest()
    if (len(payload) != CONFIG_CANONICAL_BYTES_LENGTH or
            digest != CONFIG_CANONICAL_BYTES_SHA256 or
            payload[:len(CONFIG_CANONICAL_BYTES_PREFIX_HEX) // 2].hex() != CONFIG_CANONICAL_BYTES_PREFIX_HEX or
            payload[-(len(CONFIG_CANONICAL_BYTES_SUFFIX_HEX) // 2):].hex() != CONFIG_CANONICAL_BYTES_SUFFIX_HEX):
        raise Task7MaterializationError("Task7 configuration KAT mismatch")
    return payload


def canonical_configuration_digest(payload: bytes) -> str:
    if not isinstance(payload, (bytes, bytearray)):
        raise Task7MaterializationError("configuration bytes are not bytes")
    return hashlib.sha256(bytes(payload)).hexdigest()


def configuration_identity() -> str:
    return CONFIGURATION_IDENTITY_PREFIX + canonical_configuration_digest(
        canonical_configuration_bytes()
    )


def u8_limbs(value: int) -> Tuple[int]:
    if not isinstance(value, int) or not 0 <= value <= 0xFF:
        raise Task7MaterializationError("u8 is out of range")
    return (value,)


def u16_limbs(value: int) -> Tuple[int]:
    if not isinstance(value, int) or not 0 <= value <= 0xFFFF:
        raise Task7MaterializationError("u16 is out of range")
    return (value,)


def u32_limbs(value: int) -> Tuple[int, int]:
    if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise Task7MaterializationError("u32 is out of range")
    return ((value >> 16) & 0xFFFF, value & 0xFFFF)


def u64_limbs(value: int) -> Tuple[int, int, int, int]:
    if not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise Task7MaterializationError("u64 is out of range")
    return ((value >> 48) & 0xFFFF, (value >> 32) & 0xFFFF,
            (value >> 16) & 0xFFFF, value & 0xFFFF)


def i32_limbs(value: int) -> Tuple[int, int]:
    if isinstance(value, bool) or not isinstance(value, int) or not -(1 << 31) <= value < (1 << 31):
        raise Task7MaterializationError("i32 is out of range")
    return u32_limbs(value & 0xFFFFFFFF)


def reconstruct_limbs(limbs: Sequence[int]) -> int:
    if not limbs or any(isinstance(value, bool) or not isinstance(value, int) or
                        not 0 <= value <= 0xFFFF for value in limbs):
        raise Task7MaterializationError("invalid limb")
    value = 0
    for limb in limbs:
        value = (value << 16) | limb
    return value


def reconstruct_i32(limbs: Sequence[int]) -> int:
    if len(limbs) != 2:
        raise Task7MaterializationError("i32 has wrong limb count")
    value = reconstruct_limbs(limbs)
    return value - (1 << 32) if value & (1 << 31) else value


@dataclasses.dataclass(frozen=True)
class Task7ReferenceColumnV1:
    """Expanded, type-faithful reference columns.

    ``outer_present`` is ``None`` for ``R``; ``kind_code`` is non-``None`` only
    for ``CR``.  Current-entity presence/value columns are ``None`` for ``HR``.
    """

    form: str
    public_locator_ordinal: Any
    current_entity_present: Optional[Any]
    current_entity_ordinal: Optional[Any]
    outer_present: Optional[Any] = None
    kind_code: Optional[Any] = None

    def learner_tensors(self) -> tuple[tuple[str, Any], ...]:
        values: list[tuple[str, Any]] = [("public_locator_ordinal", self.public_locator_ordinal)]
        if self.current_entity_present is not None:
            values.append(("current_entity_present", self.current_entity_present))
        if self.current_entity_ordinal is not None:
            values.append(("current_entity_ordinal", self.current_entity_ordinal))
        if self.outer_present is not None:
            values.insert(0, ("present", self.outer_present))
        if self.kind_code is not None:
            values.insert(1 if self.outer_present is not None else 0, ("kind_code", self.kind_code))
        return tuple(values)


@dataclasses.dataclass(frozen=True)
class Task7MaterializedColumnV1:
    name: str
    source_type: str
    limb_count: int
    presence_rule: str
    padding_rule: str
    values: Any
    presence: Optional[Any] = None

    def is_control_string(self) -> bool:
        return self.source_type == "String"


@dataclasses.dataclass(frozen=True)
class Task7MaterializedTableV1:
    identity: str
    kind: str
    row_order: str
    parent_table_identity: Optional[str]
    parent_offset_identity: Optional[str]
    row_count: int
    sample_offsets: Any
    parent_offsets: Optional[Any]
    columns: tuple[Task7MaterializedColumnV1, ...]
    row_mask: Any

    def column(self, name: str) -> Task7MaterializedColumnV1:
        for value in self.columns:
            if value.name == name:
                return value
        raise KeyError(name)

    @property
    def column_map(self) -> Mapping[str, Task7MaterializedColumnV1]:
        return MappingProxyType({value.name: value for value in self.columns})


@dataclasses.dataclass(frozen=True)
class Task7MaterializedSampleV1:
    configuration_identity: str
    model_input_identity: str
    phase5_contract_ids: tuple[str, ...]
    card_vocabulary_identity: str
    public_observation_digest: str
    public_candidate_domain_digest: Optional[str]
    tables: tuple[Task7MaterializedTableV1, ...]
    public_locator_tokens: tuple[str, ...]
    routing_keys: tuple[str, ...]
    canonical_bytes: bytes

    @property
    def schema_id(self) -> str:
        return MATERIALIZATION_SCHEMA_ID

    def table(self, identity: str) -> Task7MaterializedTableV1:
        for value in self.tables:
            if value.identity == identity:
                return value
        raise KeyError(identity)

    @property
    def table_map(self) -> Mapping[str, Task7MaterializedTableV1]:
        return MappingProxyType({value.identity: value for value in self.tables})

    @property
    def candidate_count(self) -> int:
        return self.table("candidates").row_count

    @property
    def source_identities(self) -> Mapping[str, Optional[str]]:
        return MappingProxyType({
            "model_input_identity": self.model_input_identity,
            "card_vocabulary_identity": self.card_vocabulary_identity,
            "public_observation_digest": self.public_observation_digest,
            "public_candidate_domain_digest": self.public_candidate_domain_digest,
        })

    @property
    def learner_tensors(self) -> tuple[tuple[str, str, Any], ...]:
        """Return only learner-facing tensors, excluding all control strings."""
        result: list[tuple[str, str, Any]] = []
        for table in self.tables:
            if table.kind == "control_sidecar":
                continue
            for column in table.columns:
                if isinstance(column.values, Task7ReferenceColumnV1):
                    result.extend(
                        (table.identity, f"{column.name}.{name}", tensor)
                        for name, tensor in column.values.learner_tensors()
                    )
                elif not column.is_control_string():
                    result.append((table.identity, column.name, column.values))
                if column.presence is not None:
                    result.append((table.identity, f"{column.name}_presence", column.presence))
        return tuple(result)

    def pad(self, widths: Mapping[str, int]) -> "Task7PaddedSampleV1":
        return Task7PaddedSampleV1.from_sample(self, widths)


@dataclasses.dataclass(frozen=True)
class Task7MaterializedBatchV1:
    schema_id: str
    configuration_identity: str
    samples: tuple[Task7MaterializedSampleV1, ...]

    def __post_init__(self) -> None:
        if self.schema_id != MATERIALIZATION_SCHEMA_ID or not self.samples:
            raise Task7MaterializationError("Task7 materialized batch is invalid")
        if any(sample.configuration_identity != self.configuration_identity
               for sample in self.samples):
            raise Task7MaterializationError("Task7 batch configuration is inconsistent")
        expected_tables = tuple(table.identity for table in self.samples[0].tables)
        if any(tuple(table.identity for table in sample.tables) != expected_tables
               for sample in self.samples[1:]):
            raise Task7MaterializationError("Task7 batch table surface is inconsistent")

    def pad(self, widths: Optional[Mapping[str, int]] = None) -> "Task7PaddedBatchV1":
        return Task7PaddedBatchV1.from_batch(self, widths)


@dataclasses.dataclass(frozen=True)
class Task7PaddedSampleV1:
    source: Task7MaterializedSampleV1
    widths: Mapping[str, int]
    tables: tuple[Task7MaterializedTableV1, ...]

    @classmethod
    def from_sample(cls, sample: Task7MaterializedSampleV1,
                    widths: Mapping[str, int]) -> "Task7PaddedSampleV1":
        if not isinstance(widths, Mapping):
            raise Task7MaterializationError("padding widths are not a mapping")
        known_tables = {table.identity for table in sample.tables}
        if any(identity not in known_tables for identity in widths):
            raise Task7MaterializationError("padding width names are not canonical")
        padded_tables: list[Task7MaterializedTableV1] = []
        normalized: dict[str, int] = {}
        for table in sample.tables:
            width = table.row_count if table.kind == "singleton" else widths.get(table.identity, table.row_count)
            if (isinstance(width, bool) or
                    table.kind == "singleton" and table.identity in widths and width != table.row_count):
                raise Task7MaterializationError("singleton table width is not canonical")
            if not isinstance(width, int) or width < table.row_count:
                raise Task7MaterializationError("padding capacity is below a real collection")
            normalized[table.identity] = width
        for table in sample.tables:
            padded_tables.append(
                _pad_table(
                    table,
                    normalized[table.identity],
                    None if table.parent_table_identity is None
                    else normalized[table.parent_table_identity],
                )
            )
        return cls(sample, MappingProxyType(normalized), tuple(padded_tables))

    def table(self, identity: str) -> Task7MaterializedTableV1:
        for value in self.tables:
            if value.identity == identity:
                return value
        raise KeyError(identity)

    def unpad(self) -> Task7MaterializedSampleV1:
        real_tables_list: list[Task7MaterializedTableV1] = []
        for table in self.tables:
            original = self.source.table(table.identity)
            parent_width = (
                None if original.parent_table_identity is None
                else self.table(original.parent_table_identity).row_count
            )
            original_parent_count = (
                None if original.parent_table_identity is None
                else self.source.table(original.parent_table_identity).row_count
            )
            unpadded = _unpad_table(
                table, original.row_count, original_parent_count, parent_width
            )
            if not _tables_equal(unpadded, original):
                raise Task7MaterializationError("padded real rows differ from source")
            real_tables_list.append(unpadded)
        real_tables = tuple(real_tables_list)
        return dataclasses.replace(self.source, tables=real_tables)


@dataclasses.dataclass(frozen=True)
class Task7PaddedBatchV1:
    source: Task7MaterializedBatchV1
    widths: Mapping[str, int]
    samples: tuple[Task7PaddedSampleV1, ...]

    @classmethod
    def from_batch(cls, batch: Task7MaterializedBatchV1,
                   widths: Optional[Mapping[str, int]] = None) -> "Task7PaddedBatchV1":
        if widths is not None and not isinstance(widths, Mapping):
            raise Task7MaterializationError("batch padding widths are not a mapping")
        known_tables = {table.identity for table in batch.samples[0].tables}
        requested = {} if widths is None else dict(widths)
        if any(identity not in known_tables for identity in requested):
            raise Task7MaterializationError("batch padding width names are not canonical")
        normalized: dict[str, int] = {}
        for table in batch.samples[0].tables:
            maximum = max(sample.table(table.identity).row_count for sample in batch.samples)
            value = maximum if table.kind != "singleton" else 1
            if table.identity in requested:
                value = requested[table.identity]
            if isinstance(value, bool) or table.kind == "singleton" and value != 1:
                raise Task7MaterializationError("singleton table width is not canonical")
            if not isinstance(value, int) or value < maximum:
                raise Task7MaterializationError("padding capacity is below a real collection")
            normalized[table.identity] = value
        padded = tuple(sample.pad(normalized) for sample in batch.samples)
        return cls(batch, MappingProxyType(normalized), padded)

    def table(self, sample_index: int, identity: str) -> Task7MaterializedTableV1:
        return self.samples[sample_index].table(identity)

    def unpad(self) -> Task7MaterializedBatchV1:
        samples = tuple(sample.unpad() for sample in self.samples)
        return Task7MaterializedBatchV1(
            MATERIALIZATION_SCHEMA_ID, self.source.configuration_identity, samples
        )


def _torch_required() -> Any:
    if torch is None:
        raise Task7MaterializationError("PyTorch is required for Task7 tensor decoding")
    return torch


def _int_tensor(rows: Sequence[Sequence[int]], limbs: int) -> Any:
    runtime = _torch_required()
    if any(len(row) != limbs for row in rows):
        raise Task7MaterializationError("decoded limb width is inconsistent")
    if not rows:
        return runtime.empty((0, limbs), dtype=runtime.int64)
    return runtime.tensor(list(rows), dtype=runtime.int64)


def _bool_tensor(values: Sequence[bool]) -> Any:
    runtime = _torch_required()
    return runtime.tensor(list(values), dtype=runtime.bool)


def _offset_tensor(values: Sequence[int]) -> Any:
    runtime = _torch_required()
    if any(isinstance(value, bool) or not isinstance(value, int) or
           value < 0 or value > (1 << 63) - 1 for value in values):
        raise Task7MaterializationError("offset is outside executable range")
    return runtime.tensor(list(values), dtype=runtime.int64)


def _u32_from_row(values: Any, row: int = 0) -> int:
    if values.ndim != 2 or values.shape[1] != 2 or row < 0 or row >= values.shape[0]:
        raise Task7MaterializationError("u32 tensor shape is invalid")
    return (int(values[row, 0].item()) << 16) | int(values[row, 1].item())


@dataclasses.dataclass
class _Reader:
    payload: bytes
    position: int = 0

    def _read(self, count: int) -> bytes:
        if count < 0 or self.position + count > len(self.payload):
            raise Task7MaterializationError("canonical bytes are truncated")
        result = self.payload[self.position:self.position + count]
        self.position += count
        return result

    def u8(self) -> int:
        return self._read(1)[0]

    def u16(self) -> int:
        return struct.unpack(">H", self._read(2))[0]

    def u32(self) -> int:
        return struct.unpack(">I", self._read(4))[0]

    def u64(self) -> int:
        return struct.unpack(">Q", self._read(8))[0]

    def boolean(self) -> bool:
        value = self.u8()
        if value not in (0, 1):
            raise Task7MaterializationError("boolean is not canonical")
        return bool(value)

    def string(self) -> str:
        size = self.u32()
        raw = self._read(size)
        try:
            return raw.decode("utf-8", "strict")
        except UnicodeError as error:
            raise Task7MaterializationError("canonical string is not UTF-8") from error

    def vector(self, reader: Any) -> list[Any]:
        count = self.u32()
        if count > len(self.payload) - self.position:
            raise Task7MaterializationError("vector count exceeds remaining bytes")
        return [reader() for _ in range(count)]

    def at_end(self) -> bool:
        return self.position == len(self.payload)


def _read_limbs(reader: _Reader, count: int) -> tuple[int, ...]:
    values = tuple(reader.u16() for _ in range(count))
    if any(value > 0xFFFF for value in values):
        raise Task7MaterializationError("invalid limb")
    return values


def _read_scalar_column(reader: _Reader, source_type: str, rows: int,
                        limb_count: int) -> tuple[Any, Optional[Any]]:
    if source_type == "Bool":
        return _bool_tensor([reader.boolean() for _ in range(rows)]), None
    if source_type == "String":
        return tuple(reader.string() for _ in range(rows)), None
    if source_type in {"U8", "U16", "U32", "U64", "I32"}:
        all_rows = [_read_limbs(reader, limb_count) for _ in range(rows)]
        if source_type == "U8" and any(row[0] > 0xFF for row in all_rows):
            raise Task7MaterializationError("u8 limb exceeds source domain")
        return _int_tensor(all_rows, limb_count), None
    if source_type.startswith("P<") and source_type.endswith(">"):
        base_type = source_type[2:-1]
        expected_limbs = {"U8": 1, "U16": 1, "U32": 2, "U64": 4, "I32": 2}.get(base_type)
        if expected_limbs is None or expected_limbs != limb_count:
            raise Task7MaterializationError("optional source type has wrong limb count")
        present: list[bool] = []
        values: list[tuple[int, ...]] = []
        for _ in range(rows):
            is_present = reader.boolean()
            present.append(is_present)
            values.append(_read_limbs(reader, limb_count) if is_present else (0,) * limb_count)
        if base_type == "U8" and any(
                row[0] > 0xFF for row, is_present in zip(values, present) if is_present):
            raise Task7MaterializationError("u8 limb exceeds source domain")
        return _int_tensor(values, limb_count), _bool_tensor(present)
    raise Task7MaterializationError(f"unknown scalar source type: {source_type}")


def _read_current_reference(reader: _Reader, rows: int) -> tuple[Any, Any, Any]:
    locators: list[tuple[int, int]] = []
    current_present: list[bool] = []
    current_values: list[tuple[int, int]] = []
    for _ in range(rows):
        locators.append(_read_limbs(reader, 2))
        present = reader.boolean()
        current_present.append(present)
        current_values.append(_read_limbs(reader, 2) if present else (0, 0))
    return (_int_tensor(locators, 2), _bool_tensor(current_present),
            _int_tensor(current_values, 2))


def _read_reference_column(reader: _Reader, form: str, rows: int) -> Task7ReferenceColumnV1:
    outer: Optional[list[bool]] = None
    kinds: Optional[list[tuple[int]]] = None
    locators: list[tuple[int, int]] = []
    current_present: list[bool] = []
    current_values: list[tuple[int, int]] = []
    if form == "R":
        for _ in range(rows):
            locator, present, current = _read_current_reference(reader, 1)
            locators.append(tuple(int(v) for v in locator[0].tolist()))
            current_present.append(bool(present[0].item()))
            current_values.append(tuple(int(v) for v in current[0].tolist()))
    elif form in {"OR", "CR"}:
        outer = []
        if form == "CR":
            kinds = []
        for _ in range(rows):
            is_present = reader.boolean()
            outer.append(is_present)
            if not is_present:
                locators.append((0, 0))
                current_present.append(False)
                current_values.append((0, 0))
                if kinds is not None:
                    kinds.append((0,))
                continue
            if kinds is not None:
                kind = reader.u16()
                if kind > 0xFF:
                    raise Task7MaterializationError("CR kind code exceeds U8")
                kinds.append((kind,))
            locator, present, current = _read_current_reference(reader, 1)
            locators.append(tuple(int(v) for v in locator[0].tolist()))
            current_present.append(bool(present[0].item()))
            current_values.append(tuple(int(v) for v in current[0].tolist()))
    elif form == "HR":
        outer = []
        for _ in range(rows):
            is_present = reader.boolean()
            outer.append(is_present)
            locators.append(_read_limbs(reader, 2) if is_present else (0, 0))
        return Task7ReferenceColumnV1(
            form, _int_tensor(locators, 2), None, None, _bool_tensor(outer), None
        )
    else:
        raise Task7MaterializationError("unknown reference form")
    return Task7ReferenceColumnV1(
        form,
        _int_tensor(locators, 2),
        _bool_tensor(current_present),
        _int_tensor(current_values, 2),
        _bool_tensor(outer) if outer is not None else None,
        _int_tensor(kinds, 1) if kinds is not None else None,
    )


def _read_column(reader: _Reader, descriptor: _ColumnDescriptor,
                 rows: int) -> Task7MaterializedColumnV1:
    if descriptor.source_type in {"R", "OR", "CR", "HR"}:
        values = _read_reference_column(reader, descriptor.source_type, rows)
        presence = None
    else:
        values, presence = _read_scalar_column(
            reader, descriptor.source_type, rows, descriptor.limb_count
        )
    return Task7MaterializedColumnV1(
        descriptor.name, descriptor.source_type, descriptor.limb_count,
        descriptor.presence_rule, descriptor.padding_rule, values, presence
    )


def _validate_digest(value: str, field: str) -> None:
    if not isinstance(value, str) or len(value) != 64 or any(
            character not in "0123456789abcdef" for character in value):
        raise Task7MaterializationError(f"{field} is not a lowercase SHA-256 digest")


def _validate_identity(value: str, prefix: str, field: str) -> None:
    if not isinstance(value, str) or not value.startswith(prefix):
        raise Task7MaterializationError(f"{field} has the wrong identity prefix")
    _validate_digest(value[len(prefix):], field)


def _validate_public_action_key(value: str) -> None:
    prefix = "public_action.v1."
    if not isinstance(value, str) or not value.startswith(prefix):
        raise Task7MaterializationError("routing sidecar contains an invalid public key")
    suffix = value[len(prefix):]
    if not suffix or len(suffix) % 2 or any(character not in "0123456789abcdef" for character in suffix):
        raise Task7MaterializationError("routing sidecar contains an invalid public key")


def _validate_public_locator_token(value: str) -> None:
    if not isinstance(value, str) or not value or any(
            ord(character) < 0x20 or ord(character) == 0x7F for character in value):
        raise Task7MaterializationError("locator sidecar contains an invalid public token")


def _read_table(reader: _Reader, descriptor: _TableDescriptor,
                previous_rows: Mapping[str, int]) -> Task7MaterializedTableV1:
    identity = reader.string()
    if identity != descriptor.name:
        raise Task7MaterializationError("table order or identity is invalid")
    row_count = reader.u64()
    if row_count > (1 << 63) - 1:
        raise Task7MaterializationError("table row count exceeds executable range")
    if row_count > len(reader.payload) - reader.position:
        raise Task7MaterializationError("table row count exceeds remaining bytes")
    if descriptor.kind == "singleton" and row_count != 1:
        raise Task7MaterializationError("singleton table row count is invalid")
    if descriptor.kind != "singleton":
        offsets = tuple(reader.vector(reader.u64))
        if offsets != (0, row_count):
            raise Task7MaterializationError("sample-relative offsets are invalid")
        sample_offsets = _offset_tensor(offsets)
    else:
        sample_offsets = _offset_tensor((0, 1))
    parent_offsets = None
    if descriptor.parent_offset is not None:
        parent_rows = previous_rows.get(descriptor.parent or "")
        if parent_rows is None:
            raise Task7MaterializationError("child parent table is missing")
        offsets = tuple(reader.vector(reader.u64))
        if len(offsets) != parent_rows + 1 or offsets[0] != 0 or offsets[-1] != row_count:
            raise Task7MaterializationError("child parent offsets are invalid")
        if any(left > right for left, right in zip(offsets, offsets[1:])):
            raise Task7MaterializationError("child parent offsets are not monotonic")
        parent_offsets = _offset_tensor(offsets)
    column_names = tuple(reader.vector(reader.string))
    expected_names = tuple(value.name for value in descriptor.columns)
    if column_names != expected_names:
        raise Task7MaterializationError("column order or identity is invalid")
    columns = tuple(_read_column(reader, value, row_count) for value in descriptor.columns)
    masks = tuple(reader.vector(reader.boolean))
    if len(masks) != row_count or any(not value for value in masks):
        raise Task7MaterializationError(f"canonical real-row mask is invalid for {descriptor.name}")
    return Task7MaterializedTableV1(
        identity, descriptor.kind, descriptor.row_order, descriptor.parent,
        descriptor.parent_offset, row_count, sample_offsets, parent_offsets,
        columns, _bool_tensor(masks)
    )


def _validate_entity_padding_semantics(sample: Task7MaterializedSampleV1) -> None:
    entities = sample.table("entities")
    ids = entities.column("card_vocabulary_id").values
    known = entities.column("identity_known").values
    for row in range(entities.row_count):
        identifier = int(ids[row, 0].item()) << 16 | int(ids[row, 1].item())
        is_known = bool(known[row].item())
        if identifier == 0:
            raise Task7MaterializationError("PAD ID appears on a real entity row")
        if is_known and identifier < 2:
            raise Task7MaterializationError("known entity has an invalid vocabulary ID")
        if not is_known and identifier != 1:
            raise Task7MaterializationError("redacted entity is not vocabulary ID 1")


def _u32_rows(values: Any) -> list[int]:
    if values.ndim != 2 or values.shape[1] != 2:
        raise Task7MaterializationError("u32 column shape is invalid")
    return [((int(row[0].item()) << 16) | int(row[1].item())) for row in values]


def _validate_reference_column(column: Task7MaterializedColumnV1,
                               locator_count: int,
                               entity_locators: Sequence[int]) -> None:
    reference = column.values
    if not isinstance(reference, Task7ReferenceColumnV1):
        raise Task7MaterializationError("reference column has the wrong value type")
    locators = _u32_rows(reference.public_locator_ordinal)
    active = ([True] * len(locators) if reference.outer_present is None
              else [bool(value) for value in reference.outer_present.tolist()])
    if any(active_row and value >= locator_count for value, active_row in zip(locators, active)):
        raise Task7MaterializationError("reference locator ordinal is out of range")
    if reference.current_entity_present is not None:
        if reference.current_entity_ordinal is None:
            raise Task7MaterializationError("reference current ordinal is missing")
        current = _u32_rows(reference.current_entity_ordinal)
        for row, (locator, present, entity) in enumerate(zip(
                locators, reference.current_entity_present.tolist(), current)):
            if active[row] and present and (entity >= len(entity_locators) or entity_locators[entity] != locator):
                raise Task7MaterializationError("reference current ordinal is detached")


def _validate_reference_bounds(sample: Task7MaterializedSampleV1) -> None:
    locators = tuple(sample.public_locator_tokens)
    if any(left.encode("utf-8") >= right.encode("utf-8")
           for left, right in zip(locators, locators[1:])):
        raise Task7MaterializationError("locator sidecar order is not canonical")
    entity_locators = _u32_rows(sample.table("entities").column("public_locator_ordinal").values)
    if any(value >= len(locators) for value in entity_locators):
        raise Task7MaterializationError("entity locator ordinal is out of range")
    if any(left >= right for left, right in zip(entity_locators, entity_locators[1:])):
        raise Task7MaterializationError("entity locator order is not canonical")
    if any(locators[left].encode("utf-8") >= locators[right].encode("utf-8")
           for left, right in zip(entity_locators, entity_locators[1:])):
        raise Task7MaterializationError("entity locator token order is not canonical")
    for table_name, column_name in (
            ("relationships", "source"), ("relationships", "target"),
            ("chain_links", "source"), ("chain_targets", "target"),
            ("candidates", "source_reference"), ("candidates", "target_reference"),
            ("visible_events", "entity")):
        _validate_reference_column(
            sample.table(table_name).column(column_name), len(locators), entity_locators
        )
    context_refs = _u32_rows(
        sample.table("decision_context_references").column("public_locator_ordinal").values
    )
    if any(value >= len(locators) for value in context_refs):
        raise Task7MaterializationError("context locator ordinal is out of range")
    event_targets = _u32_rows(
        sample.table("visible_event_targets").column("public_locator_ordinal").values
    )
    if any(value >= len(locators) for value in event_targets):
        raise Task7MaterializationError("event target locator ordinal is out of range")


def decode_canonical_sample(payload: bytes) -> Task7MaterializedSampleV1:
    """Strictly decode one C++ canonical unpadded Task7 sample."""
    if not isinstance(payload, (bytes, bytearray)):
        raise Task7MaterializationError("canonical sample is not bytes")
    payload = bytes(payload)
    reader = _Reader(payload)
    if reader.string() != MATERIALIZATION_SCHEMA_ID:
        raise Task7MaterializationError("unknown materialization schema")
    config_identity = reader.string()
    if config_identity != configuration_identity():
        raise Task7MaterializationError("configuration identity mismatch")
    model_identity = reader.string()
    _validate_identity(model_identity, "model_input.v1.", "model_input_identity")
    phase5_ids = tuple(reader.vector(reader.string))
    if phase5_ids != PHASE5_CONTRACT_IDS:
        raise Task7MaterializationError("Phase-5 contract identity vector mismatch")
    vocabulary_identity = reader.string()
    _validate_identity(vocabulary_identity, "model_card_vocabulary.v1.", "card_vocabulary_identity")
    observation_digest = reader.string()
    _validate_digest(observation_digest, "public_observation_digest")
    has_candidate_digest = reader.u8()
    if has_candidate_digest not in (0, 1):
        raise Task7MaterializationError("optional candidate digest presence is invalid")
    candidate_digest = reader.string() if has_candidate_digest else None
    if candidate_digest is not None:
        _validate_digest(candidate_digest, "public_candidate_domain_digest")

    tables: list[Task7MaterializedTableV1] = []
    row_counts: dict[str, int] = {}
    for descriptor in _table_descriptors():
        table = _read_table(reader, descriptor, row_counts)
        tables.append(table)
        row_counts[table.identity] = table.row_count
    sample = Task7MaterializedSampleV1(
        config_identity, model_identity, phase5_ids, vocabulary_identity, observation_digest,
        candidate_digest, tuple(tables), (), (), payload
    )
    locator_table = sample.table("public_locator_control_sidecar")
    candidate_table = sample.table("candidates")
    routing_table = sample.table("routing_key_control_sidecar")
    locator_values = locator_table.column("public_locator_token").values
    routing_values = routing_table.column("public_action_key").values
    if candidate_table.row_count == 0:
        raise Task7MaterializationError("candidate table is empty")
    if len(locator_values) != _u32_from_row(
            sample.table("sample_header").column("public_locator_count").values):
        raise Task7MaterializationError("locator sidecar count is detached")
    if len(routing_values) != candidate_table.row_count:
        raise Task7MaterializationError("routing sidecar count is detached")
    if candidate_table.row_count != _u32_from_row(
            sample.table("sample_header").column("candidate_count").values):
        raise Task7MaterializationError("candidate count is detached")
    if len(set(routing_values)) != len(routing_values) or any(not value for value in routing_values):
        raise Task7MaterializationError("routing sidecar is not unique and nonempty")
    for value in locator_values:
        _validate_public_locator_token(value)
    for value in routing_values:
        _validate_public_action_key(value)
    sample = dataclasses.replace(sample, public_locator_tokens=tuple(locator_values),
                                 routing_keys=tuple(routing_values))
    _validate_entity_padding_semantics(sample)
    _validate_reference_bounds(sample)
    if not reader.at_end():
        raise Task7MaterializationError("canonical sample has trailing bytes")
    return sample


def materialize_tensors(payload: bytes) -> Task7MaterializedSampleV1:
    """Adapter-facing alias for :func:`decode_canonical_sample`."""
    return decode_canonical_sample(payload)


def decode_canonical_batch(payloads: Iterable[bytes]) -> Task7MaterializedBatchV1:
    samples = tuple(decode_canonical_sample(payload) for payload in payloads)
    if not samples:
        raise Task7MaterializationError("Task7 materialized batch is empty")
    return Task7MaterializedBatchV1(
        MATERIALIZATION_SCHEMA_ID, samples[0].configuration_identity, samples
    )


def _pad_column(column: Task7MaterializedColumnV1, width: int,
                real_count: int) -> Task7MaterializedColumnV1:
    runtime = _torch_required()
    if isinstance(column.values, Task7ReferenceColumnV1):
        reference = column.values
        def pad_tensor(value: Any, shape: tuple[int, ...], dtype: Any) -> Any:
            output = runtime.zeros(shape, dtype=dtype)
            if real_count:
                output[:real_count] = value
            return output
        padded_ref = Task7ReferenceColumnV1(
            reference.form,
            pad_tensor(reference.public_locator_ordinal, (width, 2), runtime.int64),
            None if reference.current_entity_present is None else pad_tensor(reference.current_entity_present, (width,), runtime.bool),
            None if reference.current_entity_ordinal is None else pad_tensor(reference.current_entity_ordinal, (width, 2), runtime.int64),
            None if reference.outer_present is None else pad_tensor(reference.outer_present, (width,), runtime.bool),
            None if reference.kind_code is None else pad_tensor(reference.kind_code, (width, 1), runtime.int64),
        )
        return dataclasses.replace(column, values=padded_ref)
    if column.is_control_string():
        values = tuple(column.values) + ("",) * (width - real_count)
        return dataclasses.replace(column, values=values)
    values = column.values
    if values.ndim == 1:
        padded_values = runtime.zeros((width,), dtype=values.dtype)
        if real_count:
            padded_values[:real_count] = values
    else:
        padded_values = runtime.zeros((width, values.shape[1]), dtype=values.dtype)
        if real_count:
            padded_values[:real_count] = values
    presence = column.presence
    if presence is not None:
        padded_presence = runtime.zeros((width,), dtype=runtime.bool)
        if real_count:
            padded_presence[:real_count] = presence
    else:
        padded_presence = None
    return dataclasses.replace(column, values=padded_values, presence=padded_presence)


def _pad_table(table: Task7MaterializedTableV1, width: int,
               parent_width: Optional[int]) -> Task7MaterializedTableV1:
    runtime = _torch_required()
    if width < table.row_count:
        raise Task7MaterializationError("padding capacity is below a real collection")
    if table.parent_offsets is None:
        if parent_width is not None:
            raise Task7MaterializationError("non-child table has a parent width")
        padded_parent_offsets = None
    else:
        if parent_width is None:
            raise Task7MaterializationError("child table has no parent width")
        real_parent_count = table.parent_offsets.shape[0] - 1
        if parent_width < real_parent_count:
            raise Task7MaterializationError("parent padding capacity is below real rows")
        # The real child terminal is retained; every newly padded parent owns
        # an empty span at that terminal rather than a fabricated child row.
        terminal = table.parent_offsets[-1].reshape(1)
        if parent_width == real_parent_count:
            padded_parent_offsets = table.parent_offsets
        else:
            padded_parent_offsets = runtime.cat(
                (table.parent_offsets, terminal.repeat(parent_width - real_parent_count))
            )
    columns = tuple(_pad_column(column, width, table.row_count) for column in table.columns)
    mask = runtime.zeros((width,), dtype=runtime.bool)
    if table.row_count:
        mask[:table.row_count] = True
    return dataclasses.replace(
        table, row_count=width, sample_offsets=_offset_tensor((0, table.row_count)),
        parent_offsets=padded_parent_offsets, columns=columns, row_mask=mask
    )


def _unpad_column(column: Task7MaterializedColumnV1, real_count: int) -> Task7MaterializedColumnV1:
    if isinstance(column.values, Task7ReferenceColumnV1):
        reference = column.values
        for tensor in reference.learner_tensors():
            value = tensor[1]
            if value.shape[0] > real_count:
                if value.dtype == torch.bool:
                    if bool(value[real_count:].any().item()):
                        raise Task7MaterializationError("nonzero reference padding")
                elif bool(value[real_count:].any().item()):
                    raise Task7MaterializationError("nonzero reference padding")
        sliced = Task7ReferenceColumnV1(
            reference.form,
            reference.public_locator_ordinal[:real_count],
            None if reference.current_entity_present is None else reference.current_entity_present[:real_count],
            None if reference.current_entity_ordinal is None else reference.current_entity_ordinal[:real_count],
            None if reference.outer_present is None else reference.outer_present[:real_count],
            None if reference.kind_code is None else reference.kind_code[:real_count],
        )
        return dataclasses.replace(column, values=sliced)
    if column.is_control_string():
        if any(value for value in column.values[real_count:]):
            raise Task7MaterializationError("nonempty string padding")
        return dataclasses.replace(column, values=tuple(column.values[:real_count]))
    if column.values.ndim == 1:
        tail = column.values[real_count:]
    else:
        tail = column.values[real_count:, :]
    if bool(tail.any().item()):
        raise Task7MaterializationError("nonzero numeric padding")
    if column.presence is not None and bool(column.presence[real_count:].any().item()):
        raise Task7MaterializationError("present padded value")
    return dataclasses.replace(column, values=column.values[:real_count],
                               presence=None if column.presence is None else column.presence[:real_count])


def _values_equal(left: Any, right: Any) -> bool:
    if isinstance(left, Task7ReferenceColumnV1) and isinstance(right, Task7ReferenceColumnV1):
        if left.form != right.form:
            return False
        for left_name, left_tensor in left.learner_tensors():
            right_tensor = dict(right.learner_tensors()).get(left_name)
            if right_tensor is None or not torch.equal(left_tensor, right_tensor):
                return False
        return True
    if hasattr(left, "dtype") and hasattr(right, "dtype"):
        return bool(torch.equal(left, right))
    return left == right


def _tables_equal(left: Task7MaterializedTableV1,
                  right: Task7MaterializedTableV1) -> bool:
    if (left.identity != right.identity or left.kind != right.kind or
            left.row_order != right.row_order or
            left.parent_table_identity != right.parent_table_identity or
            left.parent_offset_identity != right.parent_offset_identity or
            left.row_count != right.row_count or
            not torch.equal(left.sample_offsets, right.sample_offsets) or
            not torch.equal(left.row_mask, right.row_mask)):
        return False
    if (left.parent_offsets is None) != (right.parent_offsets is None):
        return False
    if left.parent_offsets is not None and not torch.equal(left.parent_offsets, right.parent_offsets):
        return False
    if len(left.columns) != len(right.columns):
        return False
    for left_column, right_column in zip(left.columns, right.columns):
        if (left_column.name != right_column.name or
                left_column.source_type != right_column.source_type or
                left_column.limb_count != right_column.limb_count or
                left_column.presence_rule != right_column.presence_rule or
                left_column.padding_rule != right_column.padding_rule or
                not _values_equal(left_column.values, right_column.values)):
            return False
        if (left_column.presence is None) != (right_column.presence is None):
            return False
        if left_column.presence is not None and not torch.equal(left_column.presence, right_column.presence):
            return False
    return True


def _unpad_table(table: Task7MaterializedTableV1, real_count: int,
                 real_parent_count: Optional[int],
                 padded_parent_width: Optional[int]) -> Task7MaterializedTableV1:
    runtime = _torch_required()
    if real_count < 0 or real_count > table.row_count:
        raise Task7MaterializationError("invalid real row count")
    if table.sample_offsets.shape != (2,) or table.sample_offsets.tolist() != [0, real_count]:
        raise Task7MaterializationError("padded sample offsets have wrong terminal")
    if bool(table.row_mask[real_count:].any().item()):
        raise Task7MaterializationError("padding row mask is true")
    if bool(table.row_mask[:real_count].logical_not().any().item()):
        raise Task7MaterializationError("real row mask is false")
    if table.parent_offsets is None:
        if real_parent_count is not None or padded_parent_width is not None:
            raise Task7MaterializationError("non-child table has parent offsets")
        parent_offsets = None
    else:
        if real_parent_count is None or padded_parent_width is None:
            raise Task7MaterializationError("child table parent metadata is missing")
        if table.parent_offsets.shape[0] != padded_parent_width + 1:
            raise Task7MaterializationError("padded child parent offsets have wrong width")
        offsets = table.parent_offsets.tolist()
        if offsets[0] != 0 or offsets[-1] != real_count:
            raise Task7MaterializationError("padded child parent offsets have wrong terminal")
        if any(left > right for left, right in zip(offsets, offsets[1:])):
            raise Task7MaterializationError("padded child parent offsets are not monotonic")
        # Parent rows beyond the source width must all be empty child spans.
        if any(value != real_count for value in offsets[real_parent_count + 1:]):
            raise Task7MaterializationError("padded parent row has a nonempty child span")
        parent_offsets = table.parent_offsets[:real_parent_count + 1]
    return dataclasses.replace(
        table, row_count=real_count, sample_offsets=_offset_tensor((0, real_count)),
        columns=tuple(_unpad_column(column, real_count) for column in table.columns),
        parent_offsets=parent_offsets,
        row_mask=runtime.ones((real_count,), dtype=runtime.bool)
    )


# Stable names used by callers that prefer the explicit adapter wording.
decode_task7_materialized_sample = decode_canonical_sample
task7_materialize_tensors = materialize_tensors
canonical_task7_materialization_config_bytes = canonical_configuration_bytes
task7_materialization_config_identity = configuration_identity
task7_u8_limbs = u8_limbs
task7_u16_limbs = u16_limbs
task7_u32_limbs = u32_limbs
task7_u64_limbs = u64_limbs
task7_i32_limbs = i32_limbs
