"""Framework-neutral Phase-6 Task-5 codecs and semantic identities.

This module owns representation only.  It deliberately has no dependency on
an inference backend, an engine host, a checkpoint loader, or a gameplay
runner.  The canonical byte encodings in this file are the implementation of
the frozen Task-5 contract; changing one is a contract migration, not a
transport-format convenience change.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import math
import re
import struct
from typing import Any, Callable, Iterable, Mapping, Optional, Sequence


class CodecError(ValueError):
    """Raised when a Task-5 value is malformed or non-canonical."""


Task5CodecError = CodecError


# Contract and artifact identities.
TASK5_HUMAN_CONTRACT_ID = (
    "ocgforge.phase6.task5.evaluation_execution_contract.v1"
)
TASK5_PLAN_SCHEMA_ID = "ocgforge.phase6.task5_execution_plan.v1"

EVALUATION_IDENTITY_SCHEMA_ID = "ocgforge.phase6.evaluation_identity.v1"
EVALUATION_IDENTITY_DOMAIN = EVALUATION_IDENTITY_SCHEMA_ID
EVALUATION_IDENTITY_PREFIX = "phase6_evaluation.v1."

EVALUATION_CONTRACT_IDENTITY_SCHEMA_ID = (
    "ocgforge.phase6.evaluation_contract_identity.v1"
)
EVALUATION_CONTRACT_IDENTITY_DOMAIN = EVALUATION_CONTRACT_IDENTITY_SCHEMA_ID
EVALUATION_CONTRACT_IDENTITY_PREFIX = "phase6_evaluation_contract.v1."

EVALUATION_JOB_IDENTITY_SCHEMA_ID = "ocgforge.phase6.evaluation_job_identity.v1"
EVALUATION_JOB_IDENTITY_DOMAIN = EVALUATION_JOB_IDENTITY_SCHEMA_ID
EVALUATION_JOB_IDENTITY_PREFIX = "phase6_evaluation_job.v1."

EVALUATION_CORPUS_IDENTITY_SCHEMA_ID = (
    "ocgforge.phase6.evaluation_corpus_identity.v1"
)
EVALUATION_CORPUS_IDENTITY_DOMAIN = EVALUATION_CORPUS_IDENTITY_SCHEMA_ID
EVALUATION_CORPUS_IDENTITY_PREFIX = "phase6_evaluation_corpus.v1."

EVALUATION_MANIFEST_SCHEMA_ID = "ocgforge.phase6.task5.evaluation_manifest.v1"
EVALUATION_MANIFEST_IDENTITY_DOMAIN = EVALUATION_MANIFEST_SCHEMA_ID
EVALUATION_MANIFEST_ID_PREFIX = "phase6_evaluation_manifest.v1."
EVALUATION_SUMMARY_SCHEMA_ID = "ocgforge.phase6.task5.evaluation_summary.v1"
JOB_MANIFEST_SCHEMA_ID = "ocgforge.phase6.task5.evaluation_job_manifest.v1"
EVALUATION_JOB_MANIFEST_SCHEMA_ID = JOB_MANIFEST_SCHEMA_ID
EVALUATION_JOB_MANIFEST_IDENTITY_DOMAIN = JOB_MANIFEST_SCHEMA_ID
JOB_MANIFEST_ID_PREFIX = "phase6_evaluation_job_manifest.v1."
EVALUATION_JOB_SCHEMA_ID = "ocgforge.phase6.task5.evaluation_job.v1"

OFFLINE_METRICS_SCHEMA_ID = "ocgforge.phase6.offline_metrics.v1"
OFFLINE_SLICE_SCHEMA_ID = "ocgforge.phase6.offline_slice.v1"
OFFLINE_SAMPLE_SCHEMA_ID = "ocgforge.phase6.offline_sample.v1"
GAMEPLAY_JOB_RESULT_SCHEMA_ID = "ocgforge.phase6.gameplay_job_result.v1"
GAMEPLAY_SUMMARY_SCHEMA_ID = "ocgforge.phase6.gameplay_summary.v1"
REPLAY_ADMISSION_SUMMARY_SCHEMA_ID = (
    "ocgforge.phase6.task5.replay_admission_summary.v1"
)
FIRST_DIVERGENCE_SCHEMA_ID = "ocgforge.phase6.first_divergence.v1"
FIRST_DIVERGENCE_ID_PREFIX = "phase6_first_divergence.v1."
FIRST_DIVERGENCE_IDENTITY_PREFIX = FIRST_DIVERGENCE_ID_PREFIX
DISTRIBUTION_SHIFT_SCHEMA_ID = "ocgforge.phase6.distribution_shift.v1"
REPORT_SCHEMA_ID = "ocgforge.phase6.task5.report.v1"
REPORT_ID_PREFIX = "phase6_task5_report.v1."

SCORE_VECTOR_SCHEMA_ID = "ocgforge.phase6.score_vector.v1"
SCORE_VECTOR_IDENTITY_DOMAIN = SCORE_VECTOR_SCHEMA_ID
SCORE_VECTOR_ID_PREFIX = "phase6_score_vector.v1."
SCORE_VECTOR_IDENTITY_PREFIX = SCORE_VECTOR_ID_PREFIX
F32_CODEC_ID = "ocgforge.phase6.numeric.f32_ieee754_be.v1"

ORDERED_CANDIDATE_DOMAIN_SCHEMA_ID = (
    "ocgforge.phase6.ordered_candidate_domain.v1"
)
ORDERED_CANDIDATE_DOMAIN_ID_PREFIX = "phase6_ordered_candidate_domain.v1."
CHECKPOINT_SCHEMA_ID = "ocgforge.phase6.checkpoint_manifest.v1"
CHECKPOINT_ID_PREFIX = "phase6_checkpoint.v1."
MODEL_INPUT_ID_PREFIX = "model_input.v1."
BC_SAMPLE_ID_PREFIX = "bc_sample.v1."
PUBLIC_ACTION_IDENTITY_SCHEMA_ID = "ocgforge.public_action_identity.v1"
PUBLIC_ACTION_KEY_PREFIX = "public_action.v1."
PUBLIC_CANDIDATE_DOMAIN_SCHEMA_ID = "ocgforge.public_candidate_domain.v1"


# The accepted Task-5 implementation/acceptance context.  These are semantic
# bindings, never execution provenance.
EVALUATOR_SEMANTIC_VERSION = "ocgforge.phase6.task5.evaluator.v1"

MATCHUP_ID = "ocgforge.matchup.swordsoul_salamangreat.v1"
RULES_BUNDLE_ID = (
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f"
)
FORMAT_ID = "TCG_ADVANCED_2026_05_18"
DUEL_MODE_ID = "DUEL_MODE_MR5"
DUEL_FLAGS = 190464

SWORDSOUL_DECK_ID = "ocgforge.swordsoul_tenyi.ml_v1"
# Keep the spelling used by the review fixture as a compatibility alias.
SWORDSOUl_DECK_ID = SWORDSOUL_DECK_ID
SALAMANGREAT_DECK_ID = "ocgforge.salamangreat.ml_v1"
SWORDSOUL_DECK_SHA256 = (
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7"
)
SALAMANGREAT_DECK_SHA256 = (
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188"
)

SWORDSOUL_TEACHER_PROFILE = (
    "ocgforge.strategy_profile.v1."
    "7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74"
)
SALAMANGREAT_TEACHER_PROFILE = (
    "ocgforge.strategy_profile.v1."
    "3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55"
)
SWORDSOUL_TEACHER_BINDING = (
    "ocgforge.teacher_policy_binding.v1."
    "4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c"
)
SALAMANGREAT_TEACHER_BINDING = (
    "ocgforge.teacher_policy_binding.v1."
    "ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56"
)
SWORDSOUL_TEACHER_ARTIFACT = (
    "policy_artifact.v1."
    "52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d"
)
SALAMANGREAT_TEACHER_ARTIFACT = (
    "policy_artifact.v1."
    "a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527"
)

TEACHER_PRODUCER_ID = "ocgforge.policy.teacher_core.v1"
TEACHER_SAMPLING_ID = (
    "ocgforge.policy.deterministic_lexicographic_argmax.v1"
)
TEACHER_RNG_ID = "ocgforge.no_policy_rng.v1"
LOGICAL_MODEL_INPUT_ID = "ocgforge.model_logical_input.v1"
ENCODED_MODEL_INPUT_ID = "ocgforge.model_encoded_input.v1"
BATCH_LAYOUT_ID = "ocgforge.model_batch_layout.v1"
CARD_VOCABULARY_ID = "ocgforge.model_card_vocabulary.v1"

SMOKE_CHECKPOINT_ID = (
    "phase6_checkpoint.v1."
    "62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327"
)
SMOKE_EVIDENCE_ID = (
    "phase6_task4b_smoke_evidence.v1."
    "f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe"
)

IMPLEMENTATION_ACCEPTANCE_PROFILE = (
    "ocgforge.phase6.task5.evaluation_corpus.implementation_acceptance.v1"
)
MEANINGFUL_FIXED_MATCHUP_PROFILE = (
    "ocgforge.phase6.task5.evaluation_corpus.meaningful_fixed_matchup.v1"
)
IMPLEMENTATION_ACCEPTANCE_KIND = "IMPLEMENTATION_ACCEPTANCE"
MEANINGFUL_FIXED_MATCHUP_KIND = "MEANINGFUL_FIXED_MATCHUP"
GAMEPLAY_JOB_KIND = "GAMEPLAY"

CONTINUATION_OPERATIONS = ("pick", "amount", "finish", "cancel", "bypass")
ACTION_KIND_TOKENS = (
    "idle_command",
    "battle_command",
    "chain",
    "option",
    "card_selection",
    "announcement",
    "place",
    "position",
    "yes_no",
    "pick",
    "finish",
    "cancel",
    "assign_amount",
)
DECISION_KIND_TOKENS = (
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
)
FAILURE_STAGES = (
    "before_public_decision",
    "public_frame_validation",
    "model_input_validation",
    "inference",
    "selection",
    "environment",
    "replay",
    "admission",
)

DIVERGENCE = 0
NO_DIVERGENCE_TERMINAL = 1
FAILURE_BEFORE_DIVERGENCE = 2
FIRST_DIVERGENCE_KINDS = (
    DIVERGENCE,
    NO_DIVERGENCE_TERMINAL,
    FAILURE_BEFORE_DIVERGENCE,
)


def _text(value: str) -> bytes:
    if not isinstance(value, str):
        raise CodecError("canonical string is not text")
    try:
        encoded = value.encode("utf-8", "strict")
    except UnicodeError as error:
        raise CodecError("canonical string is not valid UTF-8") from error
    return encoded


def pack_u8(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFF:
        raise CodecError("u8 is out of range")
    return struct.pack(">B", value)


def pack_u16(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFF:
        raise CodecError("u16 is out of range")
    return struct.pack(">H", value)


def pack_u32(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise CodecError("u32 is out of range")
    return struct.pack(">I", value)


def pack_u64(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFFFFFFFFFF:
        raise CodecError("u64 is out of range")
    return struct.pack(">Q", value)


def pack_i32(value: int) -> bytes:
    if isinstance(value, bool) or not isinstance(value, int) or not -(1 << 31) <= value <= (1 << 31) - 1:
        raise CodecError("i32 is out of range")
    return struct.pack(">I", value & 0xFFFFFFFF)


def pack_bool(value: bool) -> bytes:
    if not isinstance(value, bool):
        raise CodecError("canonical bool is not bool")
    return pack_u8(1 if value else 0)


def pack_string(value: str) -> bytes:
    encoded = _text(value)
    return pack_u32(len(encoded)) + encoded


def pack_vector(values: Iterable[bytes]) -> bytes:
    entries = tuple(values)
    return pack_u32(len(entries)) + b"".join(entries)


def pack_string_vector(values: Sequence[str]) -> bytes:
    return pack_vector(pack_string(value) for value in values)


def pack_optional(value: Any, encoder: Callable[[Any], bytes]) -> bytes:
    return pack_u8(0) if value is None else pack_u8(1) + encoder(value)


def _digest(prefix: str, payload: bytes) -> str:
    return prefix + hashlib.sha256(payload).hexdigest()


def _is_lower_hex(value: str, length: int) -> bool:
    return (
        isinstance(value, str)
        and len(value) == length
        and all(character in "0123456789abcdef" for character in value)
    )


def _validate_digest(value: str, field: str) -> None:
    if not _is_lower_hex(value, 64):
        raise CodecError(f"{field} is not a lowercase SHA-256 digest")


def _validate_prefixed_digest(value: str, prefix: str, field: str) -> None:
    if not isinstance(value, str) or not value.startswith(prefix):
        raise CodecError(f"{field} has the wrong identity prefix")
    _validate_digest(value[len(prefix):], field)


def _validate_identity(value: str, prefix: str, field: str) -> None:
    _validate_prefixed_digest(value, prefix, field)


def _validate_nonzero_identity(value: str, prefix: str, field: str) -> None:
    _validate_identity(value, prefix, field)
    if set(value[len(prefix):]) == {"0"}:
        raise CodecError(f"{field} is a reserved all-zero placeholder")


def _validate_commit(value: str, field: str) -> None:
    if not _is_lower_hex(value, 40):
        raise CodecError(f"{field} is not an immutable lowercase Git commit")


def _validate_u8(value: int, field: str) -> None:
    try:
        pack_u8(value)
    except CodecError as error:
        raise CodecError(f"{field} is not a u8") from error


def _validate_u32(value: int, field: str) -> None:
    try:
        pack_u32(value)
    except CodecError as error:
        raise CodecError(f"{field} is not a u32") from error


def _validate_u64(value: int, field: str) -> None:
    try:
        pack_u64(value)
    except CodecError as error:
        raise CodecError(f"{field} is not a u64") from error


def _validate_string(value: str, field: str, *, nonempty: bool = True) -> None:
    if not isinstance(value, str) or (nonempty and value == ""):
        raise CodecError(f"{field} is not an accepted string")
    _text(value)


def _validate_public_action_key(value: str, field: str) -> None:
    if not isinstance(value, str) or not value.startswith(PUBLIC_ACTION_KEY_PREFIX):
        raise CodecError(f"{field} has the wrong public-action prefix")
    suffix = value[len(PUBLIC_ACTION_KEY_PREFIX):]
    if not suffix or len(suffix) % 2 or not re.fullmatch(r"[0-9a-f]+", suffix):
        raise CodecError(f"{field} is not a lowercase hexadecimal public key")
    try:
        raw = bytes.fromhex(suffix)
    except ValueError as error:
        raise CodecError(f"{field} is not hexadecimal") from error
    if not _is_canonical_public_action_key_bytes(raw):
        raise CodecError(f"{field} is not a canonical public-action identity")


def _validate_any_content_identity(value: str, field: str) -> None:
    if not isinstance(value, str) or not value:
        raise CodecError(f"{field} is not an identity")
    if "." not in value:
        raise CodecError(f"{field} has no versioned identity prefix")
    suffix = value.rsplit(".", 1)[-1]
    if len(suffix) == 64 and _is_lower_hex(suffix, 64):
        return
    # Some accepted public identities are exact semantic tokens rather than
    # content hashes.  They still must be non-empty, versioned strings.
    if not value.endswith(".v1"):
        raise CodecError(f"{field} is not a versioned identity")


def _validate_dataset_identity(value: str, field: str) -> None:
    _validate_digest(value, field)
    if set(value) == {"0"}:
        raise CodecError(f"{field} is a reserved all-zero placeholder")


def _validate_dataset_split_identity(value: str, field: str) -> None:
    _validate_nonzero_identity(value, "phase6_dataset_split.v1.", field)


def _validate_checkpoint_identity(value: str, field: str) -> None:
    _validate_nonzero_identity(value, CHECKPOINT_ID_PREFIX, field)


def _validate_bc_sample_identity(value: str, field: str) -> None:
    _validate_nonzero_identity(value, BC_SAMPLE_ID_PREFIX, field)


_FORBIDDEN_PRIVACY_TERMS = (
    "corehost",
    "raw_engine",
    "raw engine",
    "engine_state",
    "raw_response",
    "response_bytes",
    "submissiontoken",
    "semantic_key",
    "pointer",
    "object_id",
    "objectid",
    "pid",
    "private",
    "omniscient",
    "hidden_card",
    "hidden card",
    "hidden_hand",
    "hidden_deck",
    "persistent_locator",
    "filesystem",
    "wall_time",
    "thread_id",
    "gpu",
    "cuda",
)


def _reject_private_text(value: str, field: str) -> None:
    lowered = value.lower()
    if any(term in lowered for term in _FORBIDDEN_PRIVACY_TERMS):
        raise CodecError(f"{field} contains a private/internal value")


def _reject_private_json_value(value: Any, field: str = "JSON") -> None:
    if isinstance(value, str):
        _reject_private_text(value, field)
        if re.match(r"^(?:[A-Za-z]:[\\/]|/|\\\\)", value):
            raise CodecError(f"{field} contains a filesystem path")
    elif isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                raise CodecError("JSON object key is not a string")
            _reject_private_text(key, f"{field} key")
            _reject_private_json_value(child, f"{field}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _reject_private_json_value(child, f"{field}[{index}]")


def _validate_public_text(value: str, field: str, *, nonempty: bool = True) -> None:
    _validate_string(value, field, nonempty=nonempty)
    _reject_private_text(value, field)


def _is_lower_token(value: str) -> bool:
    return bool(
        isinstance(value, str)
        and value
        and all(
            ("a" <= character <= "z")
            or ("0" <= character <= "9")
            or character == "_"
            for character in value
        )
    )


def _validate_lower_token(value: str, field: str, *, allow_empty: bool = False) -> None:
    if allow_empty and value == "":
        return
    if not _is_lower_token(value):
        raise CodecError(f"{field} is not a canonical lower-case token")


def _validate_action_kind_token(value: str, field: str) -> None:
    _validate_lower_token(value, field)
    if value not in ACTION_KIND_TOKENS:
        raise CodecError(f"{field} is not an accepted EnvironmentActionKind token")


def _validate_decision_kind_token(value: str, field: str) -> None:
    _validate_lower_token(value, field)
    if value not in DECISION_KIND_TOKENS:
        raise CodecError(f"{field} is not an accepted EnvironmentDecisionKind token")


def _is_observation_locator(value: str) -> bool:
    try:
        raw = value.encode("utf-8", "strict")
    except (AttributeError, UnicodeError):
        return False
    return bool(value) and all(byte >= 0x20 and byte != 0x7F for byte in raw)


class _Reader:
    """Bounds-checked reader for the Task-5 primitive binary encoding."""

    def __init__(self, data: bytes):
        if not isinstance(data, (bytes, bytearray)):
            raise CodecError("canonical payload is not bytes")
        self.data = bytes(data)
        self.offset = 0

    def _read(self, count: int) -> bytes:
        if count < 0 or self.offset + count > len(self.data):
            raise CodecError("canonical payload is truncated")
        result = self.data[self.offset:self.offset + count]
        self.offset += count
        return result

    def u8(self) -> int:
        return struct.unpack(">B", self._read(1))[0]

    def u32(self) -> int:
        return struct.unpack(">I", self._read(4))[0]

    def u64(self) -> int:
        return struct.unpack(">Q", self._read(8))[0]

    def i32(self) -> int:
        raw = self.u32()
        return raw - (1 << 32) if raw & (1 << 31) else raw

    def boolean(self) -> bool:
        value = self.u8()
        if value not in (0, 1):
            raise CodecError("canonical bool is not 0 or 1")
        return bool(value)

    def string(self) -> str:
        length = self.u32()
        raw = self._read(length)
        try:
            value = raw.decode("utf-8", "strict")
        except UnicodeError as error:
            raise CodecError("canonical string is not valid UTF-8") from error
        _text(value)
        return value

    def optional(self, reader: Callable[[], Any]) -> Any:
        present = self.u8()
        if present == 0:
            return None
        if present != 1:
            raise CodecError("optional presence is not 0 or 1")
        return reader()

    def vector(self, reader: Callable[[], Any]) -> tuple[Any, ...]:
        return tuple(reader() for _ in range(self.u32()))

    def require_end(self) -> None:
        if self.offset != len(self.data):
            raise CodecError("canonical payload has trailing bytes")


def _reader_identity(reader: _Reader, prefix: str, field: str) -> str:
    value = reader.string()
    _validate_identity(value, prefix, field)
    return value


def _encode_optional_string(value: Optional[str], field: str) -> bytes:
    if value is not None:
        _validate_public_text(value, field)
    return pack_optional(value, pack_string)


def _decode_optional_string(reader: _Reader, field: str) -> Optional[str]:
    value = reader.optional(reader.string)
    if value is not None:
        _validate_public_text(value, field)
    return value


# ---------------------------------------------------------------------------
# Strict canonical JSON / JSONL transport


def _duplicate_rejecting_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CodecError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> Any:
    raise CodecError(f"non-finite JSON constant: {value}")


def _reject_json_floats(value: Any) -> None:
    if isinstance(value, float):
        raise CodecError("authoritative Task-5 JSON cannot contain floats")
    if isinstance(value, dict):
        for key, child in value.items():
            if not isinstance(key, str):
                raise CodecError("JSON object key is not a string")
            _reject_json_floats(child)
    elif isinstance(value, list):
        for child in value:
            _reject_json_floats(child)


def _jsonable(value: Any) -> Any:
    if dataclasses.is_dataclass(value):
        return _jsonable(value.to_dict() if hasattr(value, "to_dict") else dataclasses.asdict(value))
    if isinstance(value, tuple):
        return [_jsonable(child) for child in value]
    if isinstance(value, list):
        return [_jsonable(child) for child in value]
    if isinstance(value, dict):
        return {key: _jsonable(child) for key, child in value.items()}
    return value


def canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    """Return one strict canonical JSON object followed by exactly one LF."""

    if not isinstance(value, dict):
        raise CodecError("canonical JSON artifact is not an object")
    _reject_json_floats(value)
    _reject_private_json_value(value)
    if any(not isinstance(key, str) for key in value):
        raise CodecError("JSON object key is not a string")
    try:
        encoded = json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("ascii", "strict")
    except (TypeError, ValueError, UnicodeError) as error:
        raise CodecError("value is not canonical JSON") from error
    return encoded + b"\n"


def parse_canonical_json(
    data: bytes,
    *,
    required_fields: Optional[Iterable[str]] = None,
    allowed_fields: Optional[Iterable[str]] = None,
) -> dict[str, Any]:
    if not isinstance(data, (bytes, bytearray)):
        raise CodecError("JSON payload is not bytes")
    raw = bytes(data)
    if raw.startswith(b"\xef\xbb\xbf"):
        raise CodecError("JSON payload has a BOM")
    if not raw.endswith(b"\n") or raw.endswith(b"\n\n") or b"\r" in raw[:-1]:
        raise CodecError("JSON payload does not have one LF terminator")
    body = raw[:-1]
    if not body or b"\n" in body:
        raise CodecError("JSON payload is not one physical line")
    try:
        value = json.loads(
            body.decode("utf-8", "strict"),
            object_pairs_hook=_duplicate_rejecting_pairs,
            parse_constant=_reject_json_constant,
        )
    except CodecError:
        raise
    except (UnicodeError, json.JSONDecodeError, TypeError, ValueError) as error:
        raise CodecError("JSON payload is malformed") from error
    if not isinstance(value, dict):
        raise CodecError("JSON payload is not an object")
    _reject_json_floats(value)
    if allowed_fields is not None:
        allowed = set(allowed_fields)
        unknown = set(value) - allowed
        if unknown:
            raise CodecError(f"unknown JSON fields: {sorted(unknown)}")
    if required_fields is not None:
        missing = set(required_fields) - set(value)
        if missing:
            raise CodecError(f"missing JSON fields: {sorted(missing)}")
    if canonical_json_bytes(value) != raw:
        raise CodecError("JSON payload is not canonical")
    return value


def canonical_jsonl_bytes(records: Sequence[Mapping[str, Any]]) -> bytes:
    if not isinstance(records, (list, tuple)):
        raise CodecError("JSONL records are not a sequence")
    return b"".join(canonical_json_bytes(record) for record in records)


def parse_canonical_jsonl(data: bytes) -> list[dict[str, Any]]:
    if not isinstance(data, (bytes, bytearray)):
        raise CodecError("JSONL payload is not bytes")
    raw = bytes(data)
    if not raw:
        return []
    if raw.startswith(b"\xef\xbb\xbf") or not raw.endswith(b"\n"):
        raise CodecError("JSONL payload has invalid framing")
    records: list[dict[str, Any]] = []
    lines = raw.split(b"\n")
    if lines[-1] != b"":
        raise CodecError("JSONL payload has invalid final line")
    for line in lines[:-1]:
        if not line or b"\r" in line:
            raise CodecError("JSONL contains a blank or CR-terminated line")
        records.append(parse_canonical_json(line + b"\n"))
    return records


def _strict_object(
    payload: Any,
    fields: Sequence[str],
    *,
    label: str,
) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise CodecError(f"{label} is not a JSON object")
    expected = set(fields)
    unknown = set(payload) - expected
    missing = expected - set(payload)
    if unknown:
        raise CodecError(f"{label} has unknown fields: {sorted(unknown)}")
    if missing:
        raise CodecError(f"{label} is missing fields: {sorted(missing)}")
    return payload


# ---------------------------------------------------------------------------
# Public nested values and score vectors


@dataclasses.dataclass(frozen=True)
class PublicChoiceV1:
    kind: int
    value: int
    response_index: Optional[int] = None

    def validate(self) -> None:
        _validate_u8(self.kind, "choice.kind")
        _validate_u64(self.value, "choice.value")
        if self.response_index is not None:
            _validate_u32(self.response_index, "choice.response_index")
        if self.kind in (1, 2):
            if self.value > 1 or self.response_index is not None:
                raise CodecError("boolean public choice has invalid value/response index")
        elif self.kind == 3:
            if self.value > 0xFFFFFFFF or self.response_index is not None:
                raise CodecError("effect-choice public choice has invalid value/response index")
        elif self.kind in (4, 5):
            if self.response_index is None:
                raise CodecError("option/announcement choice requires response_index")
        else:
            raise CodecError("choice.kind is not an accepted PublicChoiceKind")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "kind": self.kind,
            "value": self.value,
            "response_index": self.response_index,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "PublicChoiceV1":
        data = _strict_object(payload, ("kind", "value", "response_index"), label="PublicChoiceV1")
        value = cls(data["kind"], data["value"], data["response_index"])
        value.validate()
        return value


def canonical_public_choice_bytes(value: PublicChoiceV1) -> bytes:
    value.validate()
    return b"".join(
        (pack_u8(value.kind), pack_u64(value.value), pack_optional(value.response_index, pack_u32))
    )


def _decode_public_choice(reader: _Reader) -> PublicChoiceV1:
    value = PublicChoiceV1(reader.u8(), reader.u64(), reader.optional(reader.u32))
    value.validate()
    return value


@dataclasses.dataclass(frozen=True)
class PublicReferenceV1:
    reference_kind: int
    public_locator_token: str
    current_entity_ordinal: Optional[int] = None

    def validate(self) -> None:
        _validate_u8(self.reference_kind, "reference.reference_kind")
        if self.reference_kind not in (0, 1):
            raise CodecError("reference.reference_kind is not an accepted PublicCardReferenceKind")
        _validate_public_text(self.public_locator_token, "reference.public_locator_token")
        if not _is_observation_locator(self.public_locator_token):
            raise CodecError("reference.public_locator_token is not a public observation locator")
        if self.current_entity_ordinal is not None:
            _validate_u32(self.current_entity_ordinal, "reference.current_entity_ordinal")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "reference_kind": self.reference_kind,
            "public_locator_token": self.public_locator_token,
            "current_entity_ordinal": self.current_entity_ordinal,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "PublicReferenceV1":
        data = _strict_object(
            payload,
            ("reference_kind", "public_locator_token", "current_entity_ordinal"),
            label="PublicReferenceV1",
        )
        value = cls(
            data["reference_kind"],
            data["public_locator_token"],
            data["current_entity_ordinal"],
        )
        value.validate()
        return value


def canonical_public_reference_bytes(value: PublicReferenceV1) -> bytes:
    value.validate()
    return b"".join(
        (
            pack_u8(value.reference_kind),
            pack_string(value.public_locator_token),
            pack_optional(value.current_entity_ordinal, pack_u32),
        )
    )


def _decode_public_reference(reader: _Reader) -> PublicReferenceV1:
    value = PublicReferenceV1(reader.u8(), reader.string(), reader.optional(reader.u32))
    value.validate()
    return value


@dataclasses.dataclass(frozen=True)
class PublicCandidateDescriptorV1:
    action_kind: str
    choice: Optional[PublicChoiceV1] = None
    source_reference: Optional[PublicReferenceV1] = None
    target_reference: Optional[PublicReferenceV1] = None
    phase: Optional[int] = None
    position: Optional[int] = None
    source_index: Optional[int] = None
    amount: Optional[int] = None
    continuation_operation: str = ""
    submits_engine_response: bool = False

    def validate(self) -> None:
        _validate_public_text(self.action_kind, "candidate.action_kind")
        _validate_action_kind_token(self.action_kind, "candidate.action_kind")
        if self.choice is not None:
            self.choice.validate()
        if self.source_reference is not None:
            self.source_reference.validate()
        if self.target_reference is not None:
            self.target_reference.validate()
        if self.phase is not None:
            _validate_u32(self.phase, "candidate.phase")
        if self.position is not None:
            _validate_u8(self.position, "candidate.position")
        if self.source_index is not None:
            _validate_u32(self.source_index, "candidate.source_index")
        if self.amount is not None:
            if isinstance(self.amount, bool) or not isinstance(self.amount, int) or not -(1 << 31) <= self.amount <= (1 << 31) - 1:
                raise CodecError("candidate.amount is not an i32")
        _validate_public_text(
            self.continuation_operation,
            "candidate.continuation_operation",
            nonempty=False,
        )
        _validate_lower_token(
            self.continuation_operation,
            "candidate.continuation_operation",
            allow_empty=True,
        )
        if self.continuation_operation and self.continuation_operation not in CONTINUATION_OPERATIONS:
            raise CodecError("candidate.continuation_operation is not accepted")
        if not isinstance(self.submits_engine_response, bool):
            raise CodecError("candidate.submits_engine_response is not bool")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "action_kind": self.action_kind,
            "choice": None if self.choice is None else self.choice.to_dict(),
            "source_reference": None if self.source_reference is None else self.source_reference.to_dict(),
            "target_reference": None if self.target_reference is None else self.target_reference.to_dict(),
            "phase": self.phase,
            "position": self.position,
            "source_index": self.source_index,
            "amount": self.amount,
            "continuation_operation": self.continuation_operation,
            "submits_engine_response": self.submits_engine_response,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "PublicCandidateDescriptorV1":
        fields = (
            "action_kind", "choice", "source_reference", "target_reference",
            "phase", "position", "source_index", "amount",
            "continuation_operation", "submits_engine_response",
        )
        data = _strict_object(payload, fields, label="PublicCandidateDescriptorV1")
        value = cls(
            data["action_kind"],
            None if data["choice"] is None else PublicChoiceV1.from_dict(data["choice"]),
            None if data["source_reference"] is None else PublicReferenceV1.from_dict(data["source_reference"]),
            None if data["target_reference"] is None else PublicReferenceV1.from_dict(data["target_reference"]),
            data["phase"], data["position"], data["source_index"], data["amount"],
            data["continuation_operation"], data["submits_engine_response"],
        )
        value.validate()
        return value


def canonical_public_candidate_descriptor_bytes(value: PublicCandidateDescriptorV1) -> bytes:
    value.validate()
    return b"".join(
        (
            pack_string(value.action_kind),
            pack_optional(value.choice, canonical_public_choice_bytes),
            pack_optional(value.source_reference, canonical_public_reference_bytes),
            pack_optional(value.target_reference, canonical_public_reference_bytes),
            pack_optional(value.phase, pack_u32),
            pack_optional(value.position, pack_u8),
            pack_optional(value.source_index, pack_u32),
            pack_optional(value.amount, pack_i32),
            pack_string(value.continuation_operation),
            pack_bool(value.submits_engine_response),
        )
    )


def _decode_public_candidate_descriptor(reader: _Reader) -> PublicCandidateDescriptorV1:
    value = PublicCandidateDescriptorV1(
        reader.string(),
        reader.optional(lambda: _decode_public_choice(reader)),
        reader.optional(lambda: _decode_public_reference(reader)),
        reader.optional(lambda: _decode_public_reference(reader)),
        reader.optional(reader.u32),
        reader.optional(reader.u8),
        reader.optional(reader.u32),
        reader.optional(reader.i32),
        reader.string(),
        reader.boolean(),
    )
    value.validate()
    return value


def _canonical_public_action_reference_bytes(
    value: Optional[PublicReferenceV1],
) -> bytes:
    if value is None:
        return pack_u8(0)
    value.validate()
    return pack_u8(1) + pack_u8(value.reference_kind) + pack_string(value.public_locator_token)


def canonical_public_action_key_bytes(value: PublicCandidateDescriptorV1) -> bytes:
    """Encode the accepted full PublicActionKey descriptor, not a digest alias."""

    if not isinstance(value, PublicCandidateDescriptorV1):
        raise CodecError("public action key input has the wrong DTO type")
    value.validate()
    return b"".join(
        (
            pack_string(PUBLIC_ACTION_IDENTITY_SCHEMA_ID),
            pack_string(PUBLIC_ACTION_IDENTITY_SCHEMA_ID),
            pack_string(value.action_kind),
            pack_optional(value.choice, canonical_public_choice_bytes),
            _canonical_public_action_reference_bytes(value.source_reference),
            _canonical_public_action_reference_bytes(value.target_reference),
            pack_optional(value.phase, pack_u32),
            pack_optional(value.position, pack_u8),
            pack_optional(value.source_index, pack_u32),
            pack_optional(value.amount, pack_i32),
            pack_string(value.continuation_operation),
        )
    )


def public_action_key(value: PublicCandidateDescriptorV1) -> str:
    return PUBLIC_ACTION_KEY_PREFIX + canonical_public_action_key_bytes(value).hex()


def _is_canonical_public_action_key_bytes(raw: bytes) -> bool:
    try:
        reader = _Reader(raw)
        if reader.string() != PUBLIC_ACTION_IDENTITY_SCHEMA_ID:
            return False
        if reader.string() != PUBLIC_ACTION_IDENTITY_SCHEMA_ID:
            return False
        action_kind = reader.string()
        _validate_public_text(action_kind, "public action action_kind")
        _validate_lower_token(action_kind, "public action action_kind")
        reader.optional(lambda: _decode_public_choice(reader))
        for index in range(2):
            present = reader.u8()
            if present == 0:
                continue
            if present != 1:
                return False
            reference_kind = reader.u8()
            locator = reader.string()
            reference = PublicReferenceV1(reference_kind, locator)
            reference.validate()
        for reader_function in (reader.u32, reader.u8, reader.u32, reader.i32):
            reader.optional(reader_function)
        continuation_operation = reader.string()
        _validate_public_text(
            continuation_operation,
            "public action continuation_operation",
            nonempty=False,
        )
        _validate_lower_token(
            continuation_operation,
            "public action continuation_operation",
            allow_empty=True,
        )
        reader.require_end()
        return True
    except CodecError:
        return False


def is_public_action_key(value: str) -> bool:
    if not isinstance(value, str) or not value.startswith(PUBLIC_ACTION_KEY_PREFIX):
        return False
    suffix = value[len(PUBLIC_ACTION_KEY_PREFIX):]
    if not suffix or len(suffix) % 2 or not re.fullmatch(r"[0-9a-f]+", suffix):
        return False
    try:
        return _is_canonical_public_action_key_bytes(bytes.fromhex(suffix))
    except ValueError:
        return False


def validate_public_action_key(value: str) -> None:
    if not is_public_action_key(value):
        raise CodecError("public_action_key is not a canonical public-action identity")


def _validate_ordered_candidate_keys(keys: Sequence[str]) -> tuple[str, ...]:
    if not isinstance(keys, (tuple, list)) or not keys:
        raise CodecError("ordered candidate domain is empty")
    ordered = tuple(keys)
    seen: set[str] = set()
    for index, key in enumerate(ordered):
        _validate_public_action_key(key, f"candidate key {index}")
        if key in seen:
            raise CodecError("ordered candidate domain contains duplicate keys")
        seen.add(key)
    return ordered


def canonical_public_candidate_domain_bytes(
    request_kind: str,
    public_action_keys: Sequence[str],
) -> bytes:
    _validate_public_text(request_kind, "candidate domain request_kind")
    _validate_decision_kind_token(request_kind, "candidate domain request_kind")
    keys = _validate_ordered_candidate_keys(public_action_keys)
    return b"".join(
        (
            pack_string(PUBLIC_CANDIDATE_DOMAIN_SCHEMA_ID),
            pack_string(request_kind),
            pack_u32(len(keys)),
            b"".join(pack_string(key) for key in keys),
        )
    )


def public_candidate_domain_digest(
    request_kind: str,
    public_action_keys: Sequence[str],
) -> str:
    return hashlib.sha256(
        canonical_public_candidate_domain_bytes(request_kind, public_action_keys)
    ).hexdigest()


def canonical_fallback_ordered_candidate_domain_bytes(
    public_action_keys: Sequence[str],
) -> bytes:
    keys = _validate_ordered_candidate_keys(public_action_keys)
    return b"".join(
        (
            pack_string(ORDERED_CANDIDATE_DOMAIN_SCHEMA_ID),
            pack_u32(len(keys)),
            b"".join(pack_string(key) for key in keys),
        )
    )


def fallback_ordered_candidate_domain_identity(
    public_action_keys: Sequence[str],
) -> str:
    return _digest(
        ORDERED_CANDIDATE_DOMAIN_ID_PREFIX,
        canonical_fallback_ordered_candidate_domain_bytes(public_action_keys),
    )


def ordered_candidate_domain_identity(
    public_action_keys: Sequence[str],
    request_kind: Optional[str] = None,
) -> str:
    keys = _validate_ordered_candidate_keys(public_action_keys)
    if request_kind is not None:
        return public_candidate_domain_digest(request_kind, keys)
    return fallback_ordered_candidate_domain_identity(keys)


def validate_ordered_candidate_domain_identity(
    identity: str,
    request_kind_or_keys: Optional[str | Sequence[str]],
    public_action_keys: Optional[Sequence[str]] = None,
) -> None:
    if public_action_keys is None:
        request_kind: Optional[str] = None
        keys = request_kind_or_keys
    else:
        request_kind = request_kind_or_keys
        keys = public_action_keys
    if not isinstance(keys, (tuple, list)):
        raise CodecError("ordered candidate domain keys are required")
    ordered_keys = _validate_ordered_candidate_keys(keys)
    if request_kind is None:
        if not isinstance(identity, str) or not identity.startswith(ORDERED_CANDIDATE_DOMAIN_ID_PREFIX):
            raise CodecError("fallback ordered domain identity requires absent request kind")
        _validate_identity(identity, ORDERED_CANDIDATE_DOMAIN_ID_PREFIX, "ordered_candidate_domain_identity")
        expected = fallback_ordered_candidate_domain_identity(ordered_keys)
    else:
        _validate_decision_kind_token(request_kind, "candidate domain request_kind")
        if isinstance(identity, str) and identity.startswith(ORDERED_CANDIDATE_DOMAIN_ID_PREFIX):
            raise CodecError("request kind requires the recomputed raw Phase-5 domain digest")
        _validate_digest(identity, "ordered_candidate_domain_identity")
        expected = public_candidate_domain_digest(request_kind, ordered_keys)
    if identity != expected:
        raise CodecError("ordered candidate domain identity does not match request/keys")


@dataclasses.dataclass(frozen=True)
class ContinuationContextV1:
    is_continuation: bool
    public_continuation_operation: Optional[str] = None

    def validate(self) -> None:
        if not isinstance(self.is_continuation, bool):
            raise CodecError("continuation.is_continuation is not bool")
        if self.is_continuation:
            if self.public_continuation_operation not in CONTINUATION_OPERATIONS:
                raise CodecError("continuation operation is not accepted")
        elif self.public_continuation_operation is not None:
            raise CodecError("non-continuation has an operation")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "is_continuation": self.is_continuation,
            "public_continuation_operation": self.public_continuation_operation,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "ContinuationContextV1":
        data = _strict_object(
            payload,
            ("is_continuation", "public_continuation_operation"),
            label="ContinuationContextV1",
        )
        value = cls(data["is_continuation"], data["public_continuation_operation"])
        value.validate()
        return value


def canonical_continuation_context_bytes(value: ContinuationContextV1) -> bytes:
    value.validate()
    return pack_bool(value.is_continuation) + pack_optional(value.public_continuation_operation, pack_string)


def _decode_continuation_context(reader: _Reader) -> ContinuationContextV1:
    value = ContinuationContextV1(reader.boolean(), reader.optional(reader.string))
    value.validate()
    return value


@dataclasses.dataclass(frozen=True)
class TerminalOutcomeV1:
    terminal: bool = True
    winner: Optional[int] = None
    win_reason: Optional[int] = None

    def validate(self) -> None:
        if self.terminal is not True:
            raise CodecError("TerminalOutcomeV1.terminal must be true")
        if self.winner is not None:
            _validate_u8(self.winner, "terminal.winner")
        if self.win_reason is not None:
            _validate_u8(self.win_reason, "terminal.win_reason")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "terminal": self.terminal,
            "winner": self.winner,
            "win_reason": self.win_reason,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "TerminalOutcomeV1":
        data = _strict_object(payload, ("terminal", "winner", "win_reason"), label="TerminalOutcomeV1")
        value = cls(data["terminal"], data["winner"], data["win_reason"])
        value.validate()
        return value


def canonical_terminal_outcome_bytes(value: TerminalOutcomeV1) -> bytes:
    value.validate()
    return pack_bool(value.terminal) + pack_optional(value.winner, pack_u8) + pack_optional(value.win_reason, pack_u8)


def _decode_terminal_outcome(reader: _Reader) -> TerminalOutcomeV1:
    value = TerminalOutcomeV1(reader.boolean(), reader.optional(reader.u8), reader.optional(reader.u8))
    value.validate()
    return value


def score_f32_bits(value: float) -> str:
    if isinstance(value, bool):
        raise CodecError("bool is not a score")
    try:
        converted = float(value)
        if not math.isfinite(converted):
            raise CodecError("score is not finite")
        raw = struct.pack(">f", converted)
    except (OverflowError, struct.error, TypeError, ValueError) as error:
        raise CodecError("score is not representable as binary32") from error
    if not math.isfinite(struct.unpack(">f", raw)[0]):
        raise CodecError("score overflows binary32")
    return raw.hex()


def score_f32_bytes(value: Any) -> bytes:
    if isinstance(value, str):
        if not re.fullmatch(r"[0-9a-f]{8}", value):
            raise CodecError("score_f32_bits is not eight lowercase hex characters")
        raw = bytes.fromhex(value)
        if not math.isfinite(struct.unpack(">f", raw)[0]):
            raise CodecError("score bytes are non-finite")
        return raw
    return bytes.fromhex(score_f32_bits(value))


def score_f32_value(bits: str) -> float:
    raw = score_f32_bytes(bits)
    return struct.unpack(">f", raw)[0]


@dataclasses.dataclass(frozen=True)
class ScoreVectorV1:
    public_action_keys: tuple[str, ...]
    score_f32_bits: tuple[str, ...]
    schema_id: str = SCORE_VECTOR_SCHEMA_ID

    def validate(self) -> None:
        if self.schema_id != SCORE_VECTOR_SCHEMA_ID:
            raise CodecError("score vector has the wrong schema")
        if not isinstance(self.public_action_keys, tuple) or not isinstance(self.score_f32_bits, tuple):
            raise CodecError("score vector entries must be tuples")
        if not self.public_action_keys:
            raise CodecError("score vector cannot be empty")
        if len(self.public_action_keys) != len(self.score_f32_bits):
            raise CodecError("score vector key/score counts differ")
        seen: set[str] = set()
        for index, key in enumerate(self.public_action_keys):
            _validate_public_action_key(key, f"score vector key {index}")
            if key in seen:
                raise CodecError("score vector contains a duplicate public_action_key")
            seen.add(key)
            if not isinstance(self.score_f32_bits[index], str):
                raise CodecError("score vector score bits are not strings")
            score_f32_bytes(self.score_f32_bits[index])

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "schema_id": self.schema_id,
            "public_action_keys": list(self.public_action_keys),
            "score_f32_bits": list(self.score_f32_bits),
            "score_vector_identity": score_vector_identity(self),
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "ScoreVectorV1":
        fields = ("schema_id", "public_action_keys", "score_f32_bits", "score_vector_identity")
        data = _strict_object(payload, fields, label="ScoreVectorV1")
        value = cls(
            tuple(data["public_action_keys"]),
            tuple(data["score_f32_bits"]),
            data["schema_id"],
        )
        value.validate()
        expected = score_vector_identity(value)
        if data["score_vector_identity"] != expected:
            raise CodecError("score vector identity does not match its payload")
        return value


def canonical_score_vector_bytes(value: ScoreVectorV1) -> bytes:
    value.validate()
    return b"".join(
        (
            pack_string(SCORE_VECTOR_IDENTITY_DOMAIN),
            pack_string(SCORE_VECTOR_SCHEMA_ID),
            pack_u32(len(value.public_action_keys)),
            b"".join(
                pack_string(key) + score_f32_bytes(bits)
                for key, bits in zip(value.public_action_keys, value.score_f32_bits)
            ),
        )
    )


def score_vector_identity(value: ScoreVectorV1) -> str:
    return _digest(SCORE_VECTOR_ID_PREFIX, canonical_score_vector_bytes(value))


def decode_score_vector_bytes(data: bytes) -> ScoreVectorV1:
    reader = _Reader(data)
    domain = reader.string()
    schema = reader.string()
    if domain != SCORE_VECTOR_IDENTITY_DOMAIN or schema != SCORE_VECTOR_SCHEMA_ID:
        raise CodecError("score vector domain/schema mismatch")
    count = reader.u32()
    keys: list[str] = []
    bits: list[str] = []
    for _ in range(count):
        keys.append(reader.string())
        raw = reader._read(4)
        bits.append(raw.hex())
    reader.require_end()
    value = ScoreVectorV1(tuple(keys), tuple(bits), schema)
    value.validate()
    if canonical_score_vector_bytes(value) != bytes(data):
        raise CodecError("score vector payload is not canonical")
    return value


def encode_score_vector_json(value: ScoreVectorV1) -> bytes:
    return canonical_json_bytes(value.to_dict())


def decode_score_vector_json(data: bytes) -> ScoreVectorV1:
    payload = parse_canonical_json(data)
    return ScoreVectorV1.from_dict(payload)


def select_score_vector(value: ScoreVectorV1) -> int:
    """Select an index without changing the source-order score vector."""

    value.validate()
    best_index = 0
    best_score = score_f32_value(value.score_f32_bits[0])
    for index in range(1, len(value.public_action_keys)):
        candidate_score = score_f32_value(value.score_f32_bits[index])
        if candidate_score > best_score or (
            candidate_score == best_score
            and value.public_action_keys[index].encode("utf-8")
            < value.public_action_keys[best_index].encode("utf-8")
        ):
            best_index = index
            best_score = candidate_score
    return best_index


# ---------------------------------------------------------------------------
# Evaluation contract identity


CONTRACT_IDENTITY_FIELD_NAMES = (
    "identity_domain",
    "identity_schema",
    "human_contract_id",
    "evaluation_manifest_schema_id",
    "evaluation_summary_schema_id",
    "evaluation_job_manifest_schema_id",
    "evaluation_job_schema_id",
    "offline_metrics_schema_id",
    "offline_slice_schema_id",
    "offline_sample_schema_id",
    "score_vector_schema_id",
    "gameplay_job_result_schema_id",
    "gameplay_summary_schema_id",
    "replay_admission_summary_schema_id",
    "first_divergence_schema_id",
    "distribution_shift_schema_id",
    "report_schema_id",
    "public_environment_observation_contract_id",
    "public_safe_state_contract_id",
    "public_action_identity_contract_id",
    "public_candidate_domain_contract_id",
    "public_semantic_decision_identity_contract_id",
    "logical_model_input_contract_id",
    "encoded_model_input_contract_id",
    "batch_layout_contract_id",
    "card_vocabulary_contract_id",
    "model_input_identity_contract_id",
    "supervision_sample_contract_id",
    "model_input_inspection_contract_id",
    "bc_contract_id",
    "bc_candidate_scorer_contract_id",
    "bc_objective_contract_id",
    "inference_tiebreak_contract_id",
    "dataset_membership_contract_id",
    "dataset_split_contract_id",
    "checkpoint_manifest_contract_id",
    "checkpoint_artifact_contract_id",
    "canonical_weight_export_contract_id",
    "inference_request_contract_id",
    "inference_response_contract_id",
    "inference_numeric_contract_id",
    "ordered_candidate_domain_contract_id",
    "f32_codec_id",
    "task4_numeric_projection_contract_id",
    "task4_smoke_corpus_contract_id",
    "task4_corpus_authority_contract_id",
    "task4b_smoke_evidence_contract_id",
    "task4b_recovery_contract_id",
    "policy_provenance_contract_id",
    "teacher_producer_identity",
    "teacher_sampling_contract_identity",
    "teacher_rng_contract_identity",
    "teacher_strategy_profile_identities",
    "teacher_binding_identities",
    "teacher_policy_artifact_identities",
    "gameplay_metrics_identity",
    "fixed_matchup_id",
    "fixed_rules_bundle_id",
    "fixed_format_id",
    "fixed_duel_mode_id",
    "fixed_duel_flags",
    "fixed_deck_role_0_id",
    "fixed_deck_role_0_content_sha256",
    "fixed_deck_role_1_id",
    "fixed_deck_role_1_content_sha256",
    "failure_policy_token",
    "privacy_policy_token",
    "replay_path_contract_ids",
    "population_separation_token",
    "jsonl_stream_policy_token",
)

CONTRACT_VECTOR_FIELDS = frozenset(
    {
        "teacher_strategy_profile_identities",
        "teacher_binding_identities",
        "teacher_policy_artifact_identities",
        "replay_path_contract_ids",
    }
)
CONTRACT_U64_FIELDS = frozenset({"fixed_duel_flags"})


@dataclasses.dataclass(frozen=True)
class EvaluationContractIdentityV1:
    identity_domain: str = EVALUATION_CONTRACT_IDENTITY_DOMAIN
    identity_schema: str = EVALUATION_CONTRACT_IDENTITY_SCHEMA_ID
    human_contract_id: str = TASK5_HUMAN_CONTRACT_ID
    evaluation_manifest_schema_id: str = EVALUATION_MANIFEST_SCHEMA_ID
    evaluation_summary_schema_id: str = EVALUATION_SUMMARY_SCHEMA_ID
    evaluation_job_manifest_schema_id: str = JOB_MANIFEST_SCHEMA_ID
    evaluation_job_schema_id: str = EVALUATION_JOB_SCHEMA_ID
    offline_metrics_schema_id: str = OFFLINE_METRICS_SCHEMA_ID
    offline_slice_schema_id: str = OFFLINE_SLICE_SCHEMA_ID
    offline_sample_schema_id: str = OFFLINE_SAMPLE_SCHEMA_ID
    score_vector_schema_id: str = SCORE_VECTOR_SCHEMA_ID
    gameplay_job_result_schema_id: str = GAMEPLAY_JOB_RESULT_SCHEMA_ID
    gameplay_summary_schema_id: str = GAMEPLAY_SUMMARY_SCHEMA_ID
    replay_admission_summary_schema_id: str = REPLAY_ADMISSION_SUMMARY_SCHEMA_ID
    first_divergence_schema_id: str = FIRST_DIVERGENCE_SCHEMA_ID
    distribution_shift_schema_id: str = DISTRIBUTION_SHIFT_SCHEMA_ID
    report_schema_id: str = REPORT_SCHEMA_ID
    public_environment_observation_contract_id: str = "ocgforge.public_environment_observation.v1"
    public_safe_state_contract_id: str = "ocgforge.public_safe_state.v1"
    public_action_identity_contract_id: str = "ocgforge.public_action_identity.v1"
    public_candidate_domain_contract_id: str = "ocgforge.public_candidate_domain.v1"
    public_semantic_decision_identity_contract_id: str = "ocgforge.public_semantic_decision_identity.v1"
    logical_model_input_contract_id: str = LOGICAL_MODEL_INPUT_ID
    encoded_model_input_contract_id: str = ENCODED_MODEL_INPUT_ID
    batch_layout_contract_id: str = BATCH_LAYOUT_ID
    card_vocabulary_contract_id: str = CARD_VOCABULARY_ID
    model_input_identity_contract_id: str = "ocgforge.model_input_identity.v1"
    supervision_sample_contract_id: str = "ocgforge.model_supervision_sample.v1"
    model_input_inspection_contract_id: str = "ocgforge.phase6.model_input_inspection.v1"
    bc_contract_id: str = "ocgforge.phase6.bc_contract.v1"
    bc_candidate_scorer_contract_id: str = "ocgforge.phase6.bc_candidate_scorer.v1"
    bc_objective_contract_id: str = "ocgforge.phase6.bc_objective.v1"
    inference_tiebreak_contract_id: str = "ocgforge.phase6.bc.inference_tiebreak.v1"
    dataset_membership_contract_id: str = "ocgforge.phase6.dataset_membership.v1"
    dataset_split_contract_id: str = "ocgforge.phase6.dataset_split.v1"
    checkpoint_manifest_contract_id: str = CHECKPOINT_SCHEMA_ID
    checkpoint_artifact_contract_id: str = "ocgforge.phase6.checkpoint_artifact.v1"
    canonical_weight_export_contract_id: str = "ocgforge.phase6.canonical_weight_export.v1"
    inference_request_contract_id: str = "ocgforge.phase6.inference_request.v1"
    inference_response_contract_id: str = "ocgforge.phase6.inference_response.v1"
    inference_numeric_contract_id: str = "ocgforge.phase6.inference_numeric.v1"
    ordered_candidate_domain_contract_id: str = ORDERED_CANDIDATE_DOMAIN_SCHEMA_ID
    f32_codec_id: str = F32_CODEC_ID
    task4_numeric_projection_contract_id: str = "ocgforge.phase6.task4.numeric_projection.v1"
    task4_smoke_corpus_contract_id: str = "ocgforge.phase6.task4.smoke_corpus.v2"
    task4_corpus_authority_contract_id: str = "ocgforge.phase6.task4.corpus_authority.v1"
    task4b_smoke_evidence_contract_id: str = "ocgforge.phase6.task4b.smoke_evidence.v1"
    task4b_recovery_contract_id: str = "ocgforge.phase6.task4b.acceptance_recovery.v1"
    policy_provenance_contract_id: str = "ocgforge.policy_provenance.v1"
    teacher_producer_identity: str = TEACHER_PRODUCER_ID
    teacher_sampling_contract_identity: str = TEACHER_SAMPLING_ID
    teacher_rng_contract_identity: str = TEACHER_RNG_ID
    teacher_strategy_profile_identities: tuple[str, ...] = (
        SWORDSOUL_TEACHER_PROFILE,
        SALAMANGREAT_TEACHER_PROFILE,
    )
    teacher_binding_identities: tuple[str, ...] = (
        SWORDSOUL_TEACHER_BINDING,
        SALAMANGREAT_TEACHER_BINDING,
    )
    teacher_policy_artifact_identities: tuple[str, ...] = (
        SWORDSOUL_TEACHER_ARTIFACT,
        SALAMANGREAT_TEACHER_ARTIFACT,
    )
    gameplay_metrics_identity: str = "ocgforge.phase6.gameplay_metrics.wilson_95.v1"
    fixed_matchup_id: str = MATCHUP_ID
    fixed_rules_bundle_id: str = RULES_BUNDLE_ID
    fixed_format_id: str = FORMAT_ID
    fixed_duel_mode_id: str = DUEL_MODE_ID
    fixed_duel_flags: int = DUEL_FLAGS
    fixed_deck_role_0_id: str = SWORDSOUL_DECK_ID
    fixed_deck_role_0_content_sha256: str = SWORDSOUL_DECK_SHA256
    fixed_deck_role_1_id: str = SALAMANGREAT_DECK_ID
    fixed_deck_role_1_content_sha256: str = SALAMANGREAT_DECK_SHA256
    failure_policy_token: str = "ocgforge.phase6.task5.fail_closed_no_policy_fallback.v1"
    privacy_policy_token: str = "ocgforge.phase6.task5.public_model_audit_boundary.v1"
    replay_path_contract_ids: tuple[str, ...] = (
        "ocgforge.episodic_environment.v2",
        "ocgforge.trusted_trajectory.v1",
        "ocgforge.admission_receipt.v1",
    )
    population_separation_token: str = "ocgforge.phase6.task5.separate_teacher_bc_populations.v1"
    jsonl_stream_policy_token: str = "ocgforge.phase6.task5.canonical_jsonl_stream.v1"

    def validate(self) -> None:
        actual_names = tuple(field.name for field in dataclasses.fields(self))
        if actual_names != CONTRACT_IDENTITY_FIELD_NAMES:
            raise CodecError("contract identity field order is not accepted")
        for name in CONTRACT_IDENTITY_FIELD_NAMES:
            value = getattr(self, name)
            if name in CONTRACT_VECTOR_FIELDS:
                if not isinstance(value, tuple) or not value:
                    raise CodecError(f"{name} is not an ordered vector")
                for entry in value:
                    _validate_string(entry, name)
            elif name in CONTRACT_U64_FIELDS:
                _validate_u64(value, name)
            else:
                _validate_string(value, name)
        if self.identity_domain != EVALUATION_CONTRACT_IDENTITY_DOMAIN or self.identity_schema != EVALUATION_CONTRACT_IDENTITY_SCHEMA_ID:
            raise CodecError("contract identity domain/schema mismatch")
        fixed = _default_contract_scalar_values()
        for name, expected in fixed.items():
            if getattr(self, name) != expected:
                raise CodecError(f"contract field {name} is not accepted")
        if self.teacher_strategy_profile_identities != (
            SWORDSOUL_TEACHER_PROFILE,
            SALAMANGREAT_TEACHER_PROFILE,
        ):
            raise CodecError("Teacher strategy profile order is not accepted")
        if self.teacher_binding_identities != (
            SWORDSOUL_TEACHER_BINDING,
            SALAMANGREAT_TEACHER_BINDING,
        ):
            raise CodecError("Teacher binding order is not accepted")
        if self.teacher_policy_artifact_identities != (
            SWORDSOUL_TEACHER_ARTIFACT,
            SALAMANGREAT_TEACHER_ARTIFACT,
        ):
            raise CodecError("Teacher artifact order is not accepted")
        if self.replay_path_contract_ids != (
            "ocgforge.episodic_environment.v2",
            "ocgforge.trusted_trajectory.v1",
            "ocgforge.admission_receipt.v1",
        ):
            raise CodecError("replay path contract order is not accepted")
        _validate_digest(self.fixed_rules_bundle_id, "fixed_rules_bundle_id")
        _validate_digest(self.fixed_deck_role_0_content_sha256, "fixed_deck_role_0_content_sha256")
        _validate_digest(self.fixed_deck_role_1_content_sha256, "fixed_deck_role_1_content_sha256")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        result: dict[str, Any] = {}
        for name in CONTRACT_IDENTITY_FIELD_NAMES:
            value = getattr(self, name)
            result[name] = list(value) if name in CONTRACT_VECTOR_FIELDS else value
        result["evaluation_contract_identity"] = evaluation_contract_identity(self)
        return result

    @classmethod
    def from_dict(cls, payload: Any) -> "EvaluationContractIdentityV1":
        fields = CONTRACT_IDENTITY_FIELD_NAMES + ("evaluation_contract_identity",)
        data = _strict_object(payload, fields, label="EvaluationContractIdentityV1")
        values = {
            name: tuple(data[name]) if name in CONTRACT_VECTOR_FIELDS else data[name]
            for name in CONTRACT_IDENTITY_FIELD_NAMES
        }
        value = cls(**values)
        value.validate()
        if data["evaluation_contract_identity"] != evaluation_contract_identity(value):
            raise CodecError("contract identity does not match its payload")
        return value


def _default_contract_scalar_values() -> dict[str, Any]:
    default = EvaluationContractIdentityV1()
    return {
        name: getattr(default, name)
        for name in CONTRACT_IDENTITY_FIELD_NAMES
        if name not in CONTRACT_VECTOR_FIELDS
    }


def default_evaluation_contract_identity() -> EvaluationContractIdentityV1:
    return EvaluationContractIdentityV1()


def canonical_evaluation_contract_identity_bytes(
    value: Optional[EvaluationContractIdentityV1] = None,
) -> bytes:
    value = default_evaluation_contract_identity() if value is None else value
    if not isinstance(value, EvaluationContractIdentityV1):
        raise CodecError("contract identity has the wrong DTO type")
    value.validate()
    encoded: list[bytes] = []
    for name in CONTRACT_IDENTITY_FIELD_NAMES:
        field_value = getattr(value, name)
        if name in CONTRACT_VECTOR_FIELDS:
            encoded.append(pack_string_vector(field_value))
        elif name in CONTRACT_U64_FIELDS:
            encoded.append(pack_u64(field_value))
        else:
            encoded.append(pack_string(field_value))
    return b"".join(encoded)


def evaluation_contract_identity(
    value: Optional[EvaluationContractIdentityV1] = None,
) -> str:
    return _digest(
        EVALUATION_CONTRACT_IDENTITY_PREFIX,
        canonical_evaluation_contract_identity_bytes(value),
    )


def encode_evaluation_contract_identity_json(
    value: EvaluationContractIdentityV1,
) -> bytes:
    return canonical_json_bytes(value.to_dict())


def decode_evaluation_contract_identity_json(data: bytes) -> EvaluationContractIdentityV1:
    return EvaluationContractIdentityV1.from_dict(parse_canonical_json(data))


# ---------------------------------------------------------------------------
# Evaluation jobs and the fixed eight-job acceptance schedule


JOB_IDENTITY_FIELD_NAMES = (
    "identity_domain",
    "identity_schema",
    "evaluation_schema_id",
    "evaluation_schema_version",
    "evaluation_contract_identity",
    "corpus_profile_identity",
    "job_kind",
    "matchup_id",
    "rules_bundle_id",
    "format_id",
    "duel_mode_id",
    "duel_flags",
    "seat_0_deck_role_id",
    "seat_0_deck_content_sha256",
    "seat_1_deck_role_id",
    "seat_1_deck_content_sha256",
    "evaluated_policy_checkpoint_identity",
    "evaluated_policy_seat",
    "evaluated_policy_deck_role_id",
    "opponent_policy_seat",
    "opponent_policy_deck_role_id",
    "phase5_logical_model_input_contract_id",
    "phase5_encoded_model_input_contract_id",
    "phase5_batch_layout_contract_id",
    "card_vocabulary_contract_id",
    "teacher_policy_producer_id",
    "teacher_policy_sampling_id",
    "teacher_policy_rng_id",
    "teacher_policy_artifact_role_0_id",
    "teacher_policy_binding_role_0_id",
    "teacher_policy_artifact_role_1_id",
    "teacher_policy_binding_role_1_id",
    "opponent_policy_artifact_id",
    "opponent_policy_binding_id",
    "opponent_policy_role_id",
    "source_dataset_identity",
    "dataset_split_identity",
    "deterministic_seed",
    "starting_player",
    "evaluator_semantic_version",
    "evaluator_semantic_source_commit",
)
JOB_U8_FIELDS = frozenset({"evaluated_policy_seat", "opponent_policy_seat", "starting_player"})
JOB_U64_FIELDS = frozenset({"duel_flags", "deterministic_seed"})
JOB_OPTIONAL_FIELDS = frozenset({"source_dataset_identity", "dataset_split_identity"})


def _deck_values(role: str) -> tuple[str, str]:
    if role == SWORDSOUL_DECK_ID:
        return SWORDSOUL_DECK_ID, SWORDSOUL_DECK_SHA256
    if role == SALAMANGREAT_DECK_ID:
        return SALAMANGREAT_DECK_ID, SALAMANGREAT_DECK_SHA256
    raise CodecError("unknown fixed deck role")


def _teacher_values(role: str) -> tuple[str, str, str]:
    if role == SWORDSOUL_DECK_ID:
        return SWORDSOUL_TEACHER_ARTIFACT, SWORDSOUL_TEACHER_BINDING, SWORDSOUL_TEACHER_PROFILE
    if role == SALAMANGREAT_DECK_ID:
        return SALAMANGREAT_TEACHER_ARTIFACT, SALAMANGREAT_TEACHER_BINDING, SALAMANGREAT_TEACHER_PROFILE
    raise CodecError("unknown fixed Teacher role")


@dataclasses.dataclass(frozen=True)
class EvaluationJobV1:
    identity_domain: str = EVALUATION_JOB_IDENTITY_DOMAIN
    identity_schema: str = EVALUATION_JOB_IDENTITY_SCHEMA_ID
    evaluation_schema_id: str = EVALUATION_JOB_SCHEMA_ID
    evaluation_schema_version: str = "v1"
    evaluation_contract_identity: str = dataclasses.field(
        default_factory=lambda: evaluation_contract_identity()
    )
    corpus_profile_identity: str = IMPLEMENTATION_ACCEPTANCE_PROFILE
    job_kind: str = GAMEPLAY_JOB_KIND
    matchup_id: str = MATCHUP_ID
    rules_bundle_id: str = RULES_BUNDLE_ID
    format_id: str = FORMAT_ID
    duel_mode_id: str = DUEL_MODE_ID
    duel_flags: int = DUEL_FLAGS
    seat_0_deck_role_id: str = SWORDSOUL_DECK_ID
    seat_0_deck_content_sha256: str = SWORDSOUL_DECK_SHA256
    seat_1_deck_role_id: str = SALAMANGREAT_DECK_ID
    seat_1_deck_content_sha256: str = SALAMANGREAT_DECK_SHA256
    evaluated_policy_checkpoint_identity: str = SMOKE_CHECKPOINT_ID
    evaluated_policy_seat: int = 0
    evaluated_policy_deck_role_id: str = SWORDSOUL_DECK_ID
    opponent_policy_seat: int = 1
    opponent_policy_deck_role_id: str = SALAMANGREAT_DECK_ID
    phase5_logical_model_input_contract_id: str = LOGICAL_MODEL_INPUT_ID
    phase5_encoded_model_input_contract_id: str = ENCODED_MODEL_INPUT_ID
    phase5_batch_layout_contract_id: str = BATCH_LAYOUT_ID
    card_vocabulary_contract_id: str = CARD_VOCABULARY_ID
    teacher_policy_producer_id: str = TEACHER_PRODUCER_ID
    teacher_policy_sampling_id: str = TEACHER_SAMPLING_ID
    teacher_policy_rng_id: str = TEACHER_RNG_ID
    teacher_policy_artifact_role_0_id: str = SWORDSOUL_TEACHER_ARTIFACT
    teacher_policy_binding_role_0_id: str = SWORDSOUL_TEACHER_BINDING
    teacher_policy_artifact_role_1_id: str = SALAMANGREAT_TEACHER_ARTIFACT
    teacher_policy_binding_role_1_id: str = SALAMANGREAT_TEACHER_BINDING
    opponent_policy_artifact_id: str = SALAMANGREAT_TEACHER_ARTIFACT
    opponent_policy_binding_id: str = SALAMANGREAT_TEACHER_BINDING
    opponent_policy_role_id: str = SALAMANGREAT_DECK_ID
    source_dataset_identity: Optional[str] = None
    dataset_split_identity: Optional[str] = None
    deterministic_seed: int = 1
    starting_player: int = 0
    evaluator_semantic_version: str = EVALUATOR_SEMANTIC_VERSION
    evaluator_semantic_source_commit: str = ""

    def validate(self) -> None:
        names = tuple(field.name for field in dataclasses.fields(self))
        if names != JOB_IDENTITY_FIELD_NAMES:
            raise CodecError("evaluation job field order is not accepted")
        for name in JOB_IDENTITY_FIELD_NAMES:
            value = getattr(self, name)
            if name in JOB_OPTIONAL_FIELDS:
                if value is not None:
                    if name == "source_dataset_identity":
                        _validate_dataset_identity(value, name)
                    else:
                        _validate_dataset_split_identity(value, name)
            elif name in JOB_U8_FIELDS:
                _validate_u8(value, name)
            elif name in JOB_U64_FIELDS:
                _validate_u64(value, name)
            else:
                _validate_string(value, name)
        if self.identity_domain != EVALUATION_JOB_IDENTITY_DOMAIN or self.identity_schema != EVALUATION_JOB_IDENTITY_SCHEMA_ID:
            raise CodecError("evaluation job identity domain/schema mismatch")
        if self.evaluation_schema_id != EVALUATION_JOB_SCHEMA_ID or self.evaluation_schema_version != "v1":
            raise CodecError("evaluation job schema mismatch")
        if self.evaluation_contract_identity != evaluation_contract_identity():
            raise CodecError("evaluation job is bound to the wrong contract identity")
        if self.corpus_profile_identity not in (
            IMPLEMENTATION_ACCEPTANCE_PROFILE,
            MEANINGFUL_FIXED_MATCHUP_PROFILE,
        ):
            raise CodecError("evaluation job uses an unaccepted corpus profile")
        if self.job_kind != GAMEPLAY_JOB_KIND or self.matchup_id != MATCHUP_ID:
            raise CodecError("evaluation job kind or matchup is not accepted")
        if (
            self.rules_bundle_id != RULES_BUNDLE_ID
            or self.format_id != FORMAT_ID
            or self.duel_mode_id != DUEL_MODE_ID
            or self.duel_flags != DUEL_FLAGS
        ):
            raise CodecError("evaluation job rules/format/mode binding is not accepted")
        role0, sha0 = _deck_values(self.seat_0_deck_role_id)
        role1, sha1 = _deck_values(self.seat_1_deck_role_id)
        if (role0, sha0) != (self.seat_0_deck_role_id, self.seat_0_deck_content_sha256) or (
            role1, sha1
        ) != (self.seat_1_deck_role_id, self.seat_1_deck_content_sha256):
            raise CodecError("evaluation job deck identity does not match its role")
        if {self.seat_0_deck_role_id, self.seat_1_deck_role_id} != {
            SWORDSOUL_DECK_ID,
            SALAMANGREAT_DECK_ID,
        } or self.seat_0_deck_role_id == self.seat_1_deck_role_id:
            raise CodecError("evaluation job must use both fixed deck roles")
        _validate_checkpoint_identity(
            self.evaluated_policy_checkpoint_identity,
            "evaluated_policy_checkpoint_identity",
        )
        if (
            self.corpus_profile_identity == IMPLEMENTATION_ACCEPTANCE_PROFILE
            and self.evaluated_policy_checkpoint_identity != SMOKE_CHECKPOINT_ID
        ):
            raise CodecError("implementation acceptance job is bound to the accepted smoke checkpoint")
        if (
            self.corpus_profile_identity == MEANINGFUL_FIXED_MATCHUP_PROFILE
            and self.evaluated_policy_checkpoint_identity == SMOKE_CHECKPOINT_ID
        ):
            raise CodecError("meaningful fixed-matchup job requires a non-smoke checkpoint")
        if self.evaluated_policy_seat not in (0, 1) or self.opponent_policy_seat not in (0, 1):
            raise CodecError("policy seat is not accepted")
        if self.evaluated_policy_seat == self.opponent_policy_seat:
            raise CodecError("evaluated and opponent seats must differ")
        seat_roles = (self.seat_0_deck_role_id, self.seat_1_deck_role_id)
        if self.evaluated_policy_deck_role_id != seat_roles[self.evaluated_policy_seat]:
            raise CodecError("evaluated policy role does not match its seat")
        if self.opponent_policy_deck_role_id != seat_roles[self.opponent_policy_seat]:
            raise CodecError("opponent policy role does not match its seat")
        if (
            self.phase5_logical_model_input_contract_id != LOGICAL_MODEL_INPUT_ID
            or self.phase5_encoded_model_input_contract_id != ENCODED_MODEL_INPUT_ID
            or self.phase5_batch_layout_contract_id != BATCH_LAYOUT_ID
            or self.card_vocabulary_contract_id != CARD_VOCABULARY_ID
            or self.teacher_policy_producer_id != TEACHER_PRODUCER_ID
            or self.teacher_policy_sampling_id != TEACHER_SAMPLING_ID
            or self.teacher_policy_rng_id != TEACHER_RNG_ID
        ):
            raise CodecError("evaluation job Phase-5/Teacher binding is not accepted")
        role0_teacher = _teacher_values(self.seat_0_deck_role_id)
        role1_teacher = _teacher_values(self.seat_1_deck_role_id)
        if (
            self.teacher_policy_artifact_role_0_id,
            self.teacher_policy_binding_role_0_id,
        ) != role0_teacher[:2] or (
            self.teacher_policy_artifact_role_1_id,
            self.teacher_policy_binding_role_1_id,
        ) != role1_teacher[:2]:
            raise CodecError("Teacher artifact/binding role mapping is not accepted")
        opponent_teacher = _teacher_values(self.opponent_policy_deck_role_id)
        if (
            self.opponent_policy_artifact_id,
            self.opponent_policy_binding_id,
            self.opponent_policy_role_id,
        ) != (opponent_teacher[0], opponent_teacher[1], self.opponent_policy_deck_role_id):
            raise CodecError("opponent policy binding is not accepted")
        if self.source_dataset_identity is not None or self.dataset_split_identity is not None:
            raise CodecError("gameplay job cannot carry offline dataset membership")
        if (
            self.corpus_profile_identity in (
                IMPLEMENTATION_ACCEPTANCE_PROFILE,
                MEANINGFUL_FIXED_MATCHUP_PROFILE,
            )
            and self.deterministic_seed not in (1, 2)
        ):
            raise CodecError("evaluation profile seed is not accepted")
        if self.starting_player not in (0, 1):
            raise CodecError("starting player is not accepted")
        _validate_string(self.evaluator_semantic_version, "evaluator_semantic_version")
        _validate_commit(self.evaluator_semantic_source_commit, "evaluator_semantic_source_commit")

    def identity_input_dict(self) -> dict[str, Any]:
        self.validate()
        result = {name: getattr(self, name) for name in JOB_IDENTITY_FIELD_NAMES}
        result["evaluation_job_identity"] = evaluation_job_identity(self)
        return result


def default_evaluation_job(*, evaluator_semantic_source_commit: str) -> EvaluationJobV1:
    value = EvaluationJobV1(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    value.validate()
    return value


def canonical_evaluation_job_bytes(value: EvaluationJobV1) -> bytes:
    if not isinstance(value, EvaluationJobV1):
        raise CodecError("evaluation job has the wrong DTO type")
    value.validate()
    encoded: list[bytes] = []
    for name in JOB_IDENTITY_FIELD_NAMES:
        field_value = getattr(value, name)
        if name in JOB_OPTIONAL_FIELDS:
            encoded.append(pack_optional(field_value, pack_string))
        elif name in JOB_U8_FIELDS:
            encoded.append(pack_u8(field_value))
        elif name in JOB_U64_FIELDS:
            encoded.append(pack_u64(field_value))
        else:
            encoded.append(pack_string(field_value))
    return b"".join(encoded)


def evaluation_job_identity(value: EvaluationJobV1) -> str:
    return _digest(EVALUATION_JOB_IDENTITY_PREFIX, canonical_evaluation_job_bytes(value))


def _job_for_roles(
    seed: int,
    seat_0_role: str,
    starting_player: int,
    evaluator_semantic_source_commit: str,
) -> EvaluationJobV1:
    return _job_for_placement(
        seed=seed,
        seat_0_role=seat_0_role,
        evaluated_policy_seat=0,
        starting_player=starting_player,
        evaluator_semantic_source_commit=evaluator_semantic_source_commit,
        corpus_profile_identity=IMPLEMENTATION_ACCEPTANCE_PROFILE,
        checkpoint_identity=SMOKE_CHECKPOINT_ID,
    )


def _job_for_placement(
    *,
    seed: int,
    seat_0_role: str,
    evaluated_policy_seat: int,
    starting_player: int,
    evaluator_semantic_source_commit: str,
    corpus_profile_identity: str,
    checkpoint_identity: str,
) -> EvaluationJobV1:
    seat_1_role = SALAMANGREAT_DECK_ID if seat_0_role == SWORDSOUL_DECK_ID else SWORDSOUL_DECK_ID
    _, seat_0_sha = _deck_values(seat_0_role)
    _, seat_1_sha = _deck_values(seat_1_role)
    seat_roles = (seat_0_role, seat_1_role)
    if evaluated_policy_seat not in (0, 1):
        raise CodecError("evaluated policy seat is not accepted")
    opponent_policy_seat = 1 - evaluated_policy_seat
    evaluated_policy_deck_role = seat_roles[evaluated_policy_seat]
    opponent_policy_deck_role = seat_roles[opponent_policy_seat]
    opponent_artifact, opponent_binding, _ = _teacher_values(opponent_policy_deck_role)
    role0_artifact, role0_binding, _ = _teacher_values(seat_0_role)
    role1_artifact, role1_binding, _ = _teacher_values(seat_1_role)
    return EvaluationJobV1(
        seat_0_deck_role_id=seat_0_role,
        seat_0_deck_content_sha256=seat_0_sha,
        seat_1_deck_role_id=seat_1_role,
        seat_1_deck_content_sha256=seat_1_sha,
        corpus_profile_identity=corpus_profile_identity,
        evaluated_policy_checkpoint_identity=checkpoint_identity,
        evaluated_policy_seat=evaluated_policy_seat,
        evaluated_policy_deck_role_id=evaluated_policy_deck_role,
        opponent_policy_seat=opponent_policy_seat,
        opponent_policy_deck_role_id=opponent_policy_deck_role,
        opponent_policy_role_id=opponent_policy_deck_role,
        teacher_policy_artifact_role_0_id=role0_artifact,
        teacher_policy_binding_role_0_id=role0_binding,
        teacher_policy_artifact_role_1_id=role1_artifact,
        teacher_policy_binding_role_1_id=role1_binding,
        opponent_policy_artifact_id=opponent_artifact,
        opponent_policy_binding_id=opponent_binding,
        deterministic_seed=seed,
        starting_player=starting_player,
        evaluator_semantic_source_commit=evaluator_semantic_source_commit,
    )


def implementation_acceptance_jobs(
    *, evaluator_semantic_source_commit: str
) -> tuple[EvaluationJobV1, ...]:
    jobs = tuple(
        _job_for_roles(
            seed,
            seat_0_role,
            starting_player,
            evaluator_semantic_source_commit,
        )
        for seed in (1, 2)
        for seat_0_role in (SWORDSOUL_DECK_ID, SALAMANGREAT_DECK_ID)
        for starting_player in (0, 1)
    )
    for job in jobs:
        job.validate()
    if len({evaluation_job_identity(job) for job in jobs}) != 8:
        raise CodecError("implementation acceptance jobs are not unique")
    return jobs


def meaningful_fixed_matchup_jobs(
    *,
    checkpoint_identity: str,
    evaluator_semantic_source_commit: str,
) -> tuple[EvaluationJobV1, ...]:
    """Build the exact bounded meaningful fixed-matchup schedule.

    This helper owns schedule construction only.  Job/corpus/aggregate identity
    bytes continue to be produced by the existing T5A codecs below.  A real
    checkpoint loader must supply the immutable non-smoke checkpoint identity;
    this function does not load or attest a checkpoint artifact.
    """

    _validate_checkpoint_identity(checkpoint_identity, "checkpoint_identity")
    if checkpoint_identity == SMOKE_CHECKPOINT_ID:
        raise CodecError("meaningful fixed-matchup schedule cannot use the smoke checkpoint")
    jobs = tuple(
        _job_for_placement(
            seed=seed,
            seat_0_role=seat_0_role,
            evaluated_policy_seat=evaluated_policy_seat,
            starting_player=starting_player,
            evaluator_semantic_source_commit=evaluator_semantic_source_commit,
            corpus_profile_identity=MEANINGFUL_FIXED_MATCHUP_PROFILE,
            checkpoint_identity=checkpoint_identity,
        )
        for seed in (1, 2)
        for seat_0_role, evaluated_policy_seat in (
            (SWORDSOUL_DECK_ID, 0),
            (SWORDSOUL_DECK_ID, 1),
            (SALAMANGREAT_DECK_ID, 0),
            (SALAMANGREAT_DECK_ID, 1),
        )
        for starting_player in (0, 1)
    )
    for job in jobs:
        job.validate()
    if len(jobs) != 16 or len({evaluation_job_identity(job) for job in jobs}) != 16:
        raise CodecError("meaningful fixed-matchup jobs are not the exact unique 16-job schedule")
    return jobs


# ---------------------------------------------------------------------------
# Evaluation corpus, job manifest, root identity, and evaluation manifest


CORPUS_IDENTITY_FIELD_NAMES = (
    "identity_domain",
    "identity_schema",
    "evaluation_contract_identity",
    "corpus_profile_identity",
    "corpus_kind",
    "matchup_id",
    "rules_bundle_id",
    "format_id",
    "duel_mode_id",
    "duel_flags",
    "deck_role_0_id",
    "deck_role_0_content_sha256",
    "deck_role_1_id",
    "deck_role_1_content_sha256",
    "checkpoint_identity",
    "source_dataset_identity",
    "dataset_split_identity",
    "evaluation_job_identities",
)


@dataclasses.dataclass(frozen=True)
class EvaluationCorpusV1:
    identity_domain: str = EVALUATION_CORPUS_IDENTITY_DOMAIN
    identity_schema: str = EVALUATION_CORPUS_IDENTITY_SCHEMA_ID
    evaluation_contract_identity: str = dataclasses.field(
        default_factory=lambda: evaluation_contract_identity()
    )
    corpus_profile_identity: str = IMPLEMENTATION_ACCEPTANCE_PROFILE
    corpus_kind: str = IMPLEMENTATION_ACCEPTANCE_KIND
    matchup_id: str = MATCHUP_ID
    rules_bundle_id: str = RULES_BUNDLE_ID
    format_id: str = FORMAT_ID
    duel_mode_id: str = DUEL_MODE_ID
    duel_flags: int = DUEL_FLAGS
    deck_role_0_id: str = SWORDSOUL_DECK_ID
    deck_role_0_content_sha256: str = SWORDSOUL_DECK_SHA256
    deck_role_1_id: str = SALAMANGREAT_DECK_ID
    deck_role_1_content_sha256: str = SALAMANGREAT_DECK_SHA256
    checkpoint_identity: str = SMOKE_CHECKPOINT_ID
    source_dataset_identity: Optional[str] = None
    dataset_split_identity: Optional[str] = None
    evaluation_job_identities: tuple[str, ...] = ()

    def validate(self) -> None:
        names = tuple(field.name for field in dataclasses.fields(self))
        if names != CORPUS_IDENTITY_FIELD_NAMES:
            raise CodecError("evaluation corpus field order is not accepted")
        for name in CORPUS_IDENTITY_FIELD_NAMES:
            value = getattr(self, name)
            if name in ("source_dataset_identity", "dataset_split_identity"):
                if value is not None:
                    if name == "source_dataset_identity":
                        _validate_dataset_identity(value, name)
                    else:
                        _validate_dataset_split_identity(value, name)
            elif name == "evaluation_job_identities":
                if not isinstance(value, tuple) or not value:
                    raise CodecError("evaluation corpus job vector is not ordered")
                seen_jobs: set[str] = set()
                for entry in value:
                    _validate_identity(entry, EVALUATION_JOB_IDENTITY_PREFIX, name)
                    if entry in seen_jobs:
                        raise CodecError("evaluation corpus job vector contains duplicates")
                    seen_jobs.add(entry)
            elif name == "duel_flags":
                _validate_u64(value, name)
            else:
                _validate_string(value, name)
        if self.identity_domain != EVALUATION_CORPUS_IDENTITY_DOMAIN or self.identity_schema != EVALUATION_CORPUS_IDENTITY_SCHEMA_ID:
            raise CodecError("evaluation corpus identity domain/schema mismatch")
        if self.evaluation_contract_identity != evaluation_contract_identity():
            raise CodecError("evaluation corpus is bound to the wrong contract identity")
        if (
            self.corpus_profile_identity not in (
                IMPLEMENTATION_ACCEPTANCE_PROFILE,
                MEANINGFUL_FIXED_MATCHUP_PROFILE,
            )
            or self.corpus_kind not in (
                IMPLEMENTATION_ACCEPTANCE_KIND,
                MEANINGFUL_FIXED_MATCHUP_KIND,
            )
            or (self.corpus_profile_identity == IMPLEMENTATION_ACCEPTANCE_PROFILE) != (
                self.corpus_kind == IMPLEMENTATION_ACCEPTANCE_KIND
            )
            or self.matchup_id != MATCHUP_ID
            or self.rules_bundle_id != RULES_BUNDLE_ID
            or self.format_id != FORMAT_ID
            or self.duel_mode_id != DUEL_MODE_ID
            or self.duel_flags != DUEL_FLAGS
            or self.deck_role_0_id != SWORDSOUL_DECK_ID
            or self.deck_role_0_content_sha256 != SWORDSOUL_DECK_SHA256
            or self.deck_role_1_id != SALAMANGREAT_DECK_ID
            or self.deck_role_1_content_sha256 != SALAMANGREAT_DECK_SHA256
        ):
            raise CodecError("evaluation corpus fixed binding is not accepted")
        _validate_checkpoint_identity(self.checkpoint_identity, "checkpoint_identity")
        if (
            self.corpus_profile_identity == IMPLEMENTATION_ACCEPTANCE_PROFILE
            and self.checkpoint_identity != SMOKE_CHECKPOINT_ID
        ):
            raise CodecError("implementation acceptance corpus is bound to the accepted smoke checkpoint")
        if (
            self.corpus_profile_identity == MEANINGFUL_FIXED_MATCHUP_PROFILE
            and self.checkpoint_identity == SMOKE_CHECKPOINT_ID
        ):
            raise CodecError("meaningful fixed-matchup corpus requires a non-smoke checkpoint")
        if (self.source_dataset_identity is None) != (self.dataset_split_identity is None):
            raise CodecError("evaluation corpus dataset and split optionals are not paired")
        if self.corpus_profile_identity == IMPLEMENTATION_ACCEPTANCE_PROFILE and len(self.evaluation_job_identities) != 8:
            raise CodecError("implementation acceptance corpus must contain eight jobs")
        if self.corpus_profile_identity == MEANINGFUL_FIXED_MATCHUP_PROFILE and len(self.evaluation_job_identities) != 16:
            raise CodecError("meaningful fixed-matchup corpus must contain sixteen jobs")

    def to_dict(self, *, context: EvaluationContextV1) -> dict[str, Any]:
        self.validate()
        _require_aggregate_context(context, corpus=self)
        result = {
            name: (
                list(getattr(self, name))
                if name == "evaluation_job_identities"
                else getattr(self, name)
            )
            for name in CORPUS_IDENTITY_FIELD_NAMES
        }
        result["evaluation_corpus_identity"] = evaluation_corpus_identity(self)
        return result

    @classmethod
    def from_dict(cls, payload: Any) -> "EvaluationCorpusV1":
        fields = CORPUS_IDENTITY_FIELD_NAMES + ("evaluation_corpus_identity",)
        data = _strict_object(payload, fields, label="EvaluationCorpusV1")
        value = cls(
            **{
                name: tuple(data[name]) if name == "evaluation_job_identities" else data[name]
                for name in CORPUS_IDENTITY_FIELD_NAMES
            }
        )
        value.validate()
        if data["evaluation_corpus_identity"] != evaluation_corpus_identity(value):
            raise CodecError("evaluation corpus identity does not match its payload")
        return value


def default_evaluation_corpus(*, evaluator_semantic_source_commit: str) -> EvaluationCorpusV1:
    jobs = implementation_acceptance_jobs(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    value = EvaluationCorpusV1(
        evaluation_job_identities=tuple(evaluation_job_identity(job) for job in jobs)
    )
    value.validate()
    return value


def canonical_evaluation_corpus_bytes(value: EvaluationCorpusV1) -> bytes:
    if not isinstance(value, EvaluationCorpusV1):
        raise CodecError("evaluation corpus has the wrong DTO type")
    value.validate()
    encoded: list[bytes] = []
    for name in CORPUS_IDENTITY_FIELD_NAMES:
        field_value = getattr(value, name)
        if name in ("source_dataset_identity", "dataset_split_identity"):
            encoded.append(pack_optional(field_value, pack_string))
        elif name == "evaluation_job_identities":
            encoded.append(pack_string_vector(field_value))
        elif name == "duel_flags":
            encoded.append(pack_u64(field_value))
        else:
            encoded.append(pack_string(field_value))
    return b"".join(encoded)


def evaluation_corpus_identity(value: EvaluationCorpusV1) -> str:
    return _digest(EVALUATION_CORPUS_IDENTITY_PREFIX, canonical_evaluation_corpus_bytes(value))


def encode_evaluation_corpus_json(
    value: EvaluationCorpusV1,
    *,
    context: EvaluationContextV1,
) -> bytes:
    _require_aggregate_context(context, corpus=value)
    return canonical_json_bytes(value.to_dict(context=context))


def decode_evaluation_corpus_json(data: bytes) -> EvaluationCorpusV1:
    return EvaluationCorpusV1.from_dict(parse_canonical_json(data))


@dataclasses.dataclass(frozen=True)
class EvaluationIdentityV1:
    identity_domain: str = EVALUATION_IDENTITY_DOMAIN
    identity_schema: str = EVALUATION_IDENTITY_SCHEMA_ID
    evaluation_contract_identity: str = dataclasses.field(
        default_factory=lambda: evaluation_contract_identity()
    )
    evaluation_corpus_identity: str = ""
    checkpoint_identity: str = ""
    evaluator_semantic_version: str = EVALUATOR_SEMANTIC_VERSION
    evaluator_semantic_source_commit: str = ""

    def validate(self) -> None:
        if tuple(field.name for field in dataclasses.fields(self)) != (
            "identity_domain",
            "identity_schema",
            "evaluation_contract_identity",
            "evaluation_corpus_identity",
            "checkpoint_identity",
            "evaluator_semantic_version",
            "evaluator_semantic_source_commit",
        ):
            raise CodecError("evaluation identity field order is not accepted")
        _validate_string(self.identity_domain, "identity_domain")
        _validate_string(self.identity_schema, "identity_schema")
        if self.identity_domain != EVALUATION_IDENTITY_DOMAIN or self.identity_schema != EVALUATION_IDENTITY_SCHEMA_ID:
            raise CodecError("evaluation identity domain/schema mismatch")
        if self.evaluation_contract_identity != evaluation_contract_identity():
            raise CodecError("root evaluation identity has the wrong contract binding")
        _validate_identity(
            self.evaluation_corpus_identity,
            EVALUATION_CORPUS_IDENTITY_PREFIX,
            "evaluation_corpus_identity",
        )
        _validate_checkpoint_identity(self.checkpoint_identity, "checkpoint_identity")
        _validate_string(self.evaluator_semantic_version, "evaluator_semantic_version")
        _validate_commit(self.evaluator_semantic_source_commit, "evaluator_semantic_source_commit")

    def to_dict(self, *, context: EvaluationContextV1) -> dict[str, Any]:
        self.validate()
        _require_aggregate_context(context, root=self)
        result = {field.name: getattr(self, field.name) for field in dataclasses.fields(self)}
        result["evaluation_identity"] = evaluation_identity(self)
        return result

    @classmethod
    def from_dict(cls, payload: Any) -> "EvaluationIdentityV1":
        fields = (
            "identity_domain", "identity_schema", "evaluation_contract_identity",
            "evaluation_corpus_identity", "checkpoint_identity",
            "evaluator_semantic_version", "evaluator_semantic_source_commit",
            "evaluation_identity",
        )
        data = _strict_object(payload, fields, label="EvaluationIdentityV1")
        value = cls(**{field: data[field] for field in fields[:-1]})
        value.validate()
        if data["evaluation_identity"] != evaluation_identity(value):
            raise CodecError("evaluation identity does not match its payload")
        return value


def default_evaluation_identity(
    *, evaluator_semantic_source_commit: str
) -> EvaluationIdentityV1:
    corpus = default_evaluation_corpus(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    value = EvaluationIdentityV1(
        evaluation_corpus_identity=evaluation_corpus_identity(corpus),
        checkpoint_identity=SMOKE_CHECKPOINT_ID,
        evaluator_semantic_source_commit=evaluator_semantic_source_commit,
    )
    value.validate()
    return value


def canonical_evaluation_identity_bytes(value: EvaluationIdentityV1) -> bytes:
    if not isinstance(value, EvaluationIdentityV1):
        raise CodecError("evaluation identity has the wrong DTO type")
    value.validate()
    return b"".join(
        (
            pack_string(value.identity_domain),
            pack_string(value.identity_schema),
            pack_string(value.evaluation_contract_identity),
            pack_string(value.evaluation_corpus_identity),
            pack_string(value.checkpoint_identity),
            pack_string(value.evaluator_semantic_version),
            pack_string(value.evaluator_semantic_source_commit),
        )
    )


def evaluation_identity(value: EvaluationIdentityV1) -> str:
    return _digest(EVALUATION_IDENTITY_PREFIX, canonical_evaluation_identity_bytes(value))


def encode_evaluation_identity_json(
    value: EvaluationIdentityV1,
    *,
    context: EvaluationContextV1,
) -> bytes:
    _require_aggregate_context(context, root=value)
    return canonical_json_bytes(value.to_dict(context=context))


def decode_evaluation_identity_json(data: bytes) -> EvaluationIdentityV1:
    return EvaluationIdentityV1.from_dict(parse_canonical_json(data))


JOB_MANIFEST_FIELD_NAMES = (
    "schema_id",
    "evaluation_identity",
    "evaluation_contract_identity",
    "evaluation_corpus_identity",
    "evaluation_job_identities",
)


@dataclasses.dataclass(frozen=True)
class EvaluationJobManifestV1:
    schema_id: str = JOB_MANIFEST_SCHEMA_ID
    evaluation_identity: str = ""
    evaluation_contract_identity: str = ""
    evaluation_corpus_identity: str = ""
    evaluation_job_identities: tuple[str, ...] = ()

    def validate(self) -> None:
        if tuple(field.name for field in dataclasses.fields(self)) != JOB_MANIFEST_FIELD_NAMES:
            raise CodecError("evaluation job manifest field order is not accepted")
        if self.schema_id != JOB_MANIFEST_SCHEMA_ID:
            raise CodecError("evaluation job manifest schema is not accepted")
        _validate_identity(self.evaluation_identity, EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_identity(self.evaluation_contract_identity, EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        _validate_identity(self.evaluation_corpus_identity, EVALUATION_CORPUS_IDENTITY_PREFIX, "evaluation_corpus_identity")
        if self.evaluation_contract_identity != evaluation_contract_identity():
            raise CodecError("job manifest contract identity is not accepted")
        if not isinstance(self.evaluation_job_identities, tuple) or not self.evaluation_job_identities:
            raise CodecError("job manifest vector is not ordered")
        seen: set[str] = set()
        for identity in self.evaluation_job_identities:
            _validate_identity(identity, EVALUATION_JOB_IDENTITY_PREFIX, "evaluation_job_identities")
            if identity in seen:
                raise CodecError("job manifest vector contains duplicates")
            seen.add(identity)

    def to_dict(self, *, context: EvaluationContextV1) -> dict[str, Any]:
        self.validate()
        _require_aggregate_context(context, job_manifest=self)
        result = {
            "schema_id": self.schema_id,
            "evaluation_identity": self.evaluation_identity,
            "evaluation_contract_identity": self.evaluation_contract_identity,
            "evaluation_corpus_identity": self.evaluation_corpus_identity,
            "evaluation_job_identities": list(self.evaluation_job_identities),
            "evaluation_job_manifest_identity": evaluation_job_manifest_identity(self),
        }
        return result

    @classmethod
    def from_dict(cls, payload: Any) -> "EvaluationJobManifestV1":
        fields = JOB_MANIFEST_FIELD_NAMES + ("evaluation_job_manifest_identity",)
        data = _strict_object(payload, fields, label="EvaluationJobManifestV1")
        value = cls(
            schema_id=data["schema_id"],
            evaluation_identity=data["evaluation_identity"],
            evaluation_contract_identity=data["evaluation_contract_identity"],
            evaluation_corpus_identity=data["evaluation_corpus_identity"],
            evaluation_job_identities=tuple(data["evaluation_job_identities"]),
        )
        value.validate()
        if data["evaluation_job_manifest_identity"] != evaluation_job_manifest_identity(value):
            raise CodecError("evaluation job manifest identity does not match its payload")
        return value


def default_evaluation_job_manifest(
    *, evaluator_semantic_source_commit: str
) -> EvaluationJobManifestV1:
    jobs = implementation_acceptance_jobs(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    corpus = default_evaluation_corpus(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    root = default_evaluation_identity(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    value = EvaluationJobManifestV1(
        evaluation_identity=evaluation_identity(root),
        evaluation_contract_identity=evaluation_contract_identity(),
        evaluation_corpus_identity=evaluation_corpus_identity(corpus),
        evaluation_job_identities=tuple(evaluation_job_identity(job) for job in jobs),
    )
    value.validate()
    return value


def canonical_evaluation_job_manifest_bytes(value: EvaluationJobManifestV1) -> bytes:
    if not isinstance(value, EvaluationJobManifestV1):
        raise CodecError("evaluation job manifest has the wrong DTO type")
    value.validate()
    return b"".join(
        (
            pack_string(value.schema_id),
            pack_string(value.schema_id),
            pack_string(value.evaluation_identity),
            pack_string(value.evaluation_contract_identity),
            pack_string(value.evaluation_corpus_identity),
            pack_string_vector(value.evaluation_job_identities),
        )
    )


def evaluation_job_manifest_identity(value: EvaluationJobManifestV1) -> str:
    return _digest(JOB_MANIFEST_ID_PREFIX, canonical_evaluation_job_manifest_bytes(value))


def encode_evaluation_job_manifest_json(
    value: EvaluationJobManifestV1,
    *,
    context: EvaluationContextV1,
) -> bytes:
    _require_aggregate_context(context, job_manifest=value)
    return canonical_json_bytes(value.to_dict(context=context))


def decode_evaluation_job_manifest_json(data: bytes) -> EvaluationJobManifestV1:
    return EvaluationJobManifestV1.from_dict(parse_canonical_json(data))


EVALUATION_MANIFEST_FIELD_NAMES = (
    "schema_id",
    "evaluation_identity",
    "evaluation_contract_identity",
    "evaluation_corpus_identity",
    "evaluation_job_manifest_identity",
    "checkpoint_identity",
    "source_dataset_identity",
    "dataset_split_identity",
    "teacher_state_population_identity",
    "bc_induced_population_identity",
    "evaluator_semantic_version",
    "evaluator_semantic_source_commit",
)
MANIFEST_OPTIONAL_FIELDS = frozenset(
    {
        "source_dataset_identity",
        "dataset_split_identity",
        "teacher_state_population_identity",
        "bc_induced_population_identity",
    }
)


@dataclasses.dataclass(frozen=True)
class EvaluationManifestV1:
    schema_id: str = EVALUATION_MANIFEST_SCHEMA_ID
    evaluation_identity: str = ""
    evaluation_contract_identity: str = ""
    evaluation_corpus_identity: str = ""
    evaluation_job_manifest_identity: str = ""
    checkpoint_identity: str = ""
    source_dataset_identity: Optional[str] = None
    dataset_split_identity: Optional[str] = None
    teacher_state_population_identity: Optional[str] = None
    bc_induced_population_identity: Optional[str] = None
    evaluator_semantic_version: str = EVALUATOR_SEMANTIC_VERSION
    evaluator_semantic_source_commit: str = ""

    def validate(self) -> None:
        if tuple(field.name for field in dataclasses.fields(self)) != EVALUATION_MANIFEST_FIELD_NAMES:
            raise CodecError("evaluation manifest field order is not accepted")
        if self.schema_id != EVALUATION_MANIFEST_SCHEMA_ID:
            raise CodecError("evaluation manifest schema is not accepted")
        _validate_identity(self.evaluation_identity, EVALUATION_IDENTITY_PREFIX, "evaluation_identity")
        _validate_identity(self.evaluation_contract_identity, EVALUATION_CONTRACT_IDENTITY_PREFIX, "evaluation_contract_identity")
        _validate_identity(self.evaluation_corpus_identity, EVALUATION_CORPUS_IDENTITY_PREFIX, "evaluation_corpus_identity")
        _validate_identity(self.evaluation_job_manifest_identity, JOB_MANIFEST_ID_PREFIX, "evaluation_job_manifest_identity")
        if self.evaluation_contract_identity != evaluation_contract_identity():
            raise CodecError("evaluation manifest contract identity is not accepted")
        _validate_checkpoint_identity(self.checkpoint_identity, "checkpoint_identity")
        for name in MANIFEST_OPTIONAL_FIELDS:
            value = getattr(self, name)
            if value is not None:
                if name == "source_dataset_identity":
                    _validate_dataset_identity(value, name)
                elif name == "dataset_split_identity":
                    _validate_dataset_split_identity(value, name)
                else:
                    _validate_any_content_identity(value, name)
        if (self.source_dataset_identity is None) != (self.dataset_split_identity is None):
            raise CodecError("manifest dataset and split optionals are not paired")
        _validate_string(self.evaluator_semantic_version, "evaluator_semantic_version")
        _validate_commit(self.evaluator_semantic_source_commit, "evaluator_semantic_source_commit")
        expected_root = EvaluationIdentityV1(
            evaluation_contract_identity=self.evaluation_contract_identity,
            evaluation_corpus_identity=self.evaluation_corpus_identity,
            checkpoint_identity=self.checkpoint_identity,
            evaluator_semantic_version=self.evaluator_semantic_version,
            evaluator_semantic_source_commit=self.evaluator_semantic_source_commit,
        )
        if self.evaluation_identity != evaluation_identity(expected_root):
            raise CodecError("evaluation manifest root does not bind its evaluator source")

    def to_dict(self, *, context: EvaluationContextV1) -> dict[str, Any]:
        self.validate()
        _require_aggregate_context(context, manifest=self)
        result = {name: getattr(self, name) for name in EVALUATION_MANIFEST_FIELD_NAMES}
        result["evaluation_manifest_identity"] = evaluation_manifest_identity(self)
        return result

    @classmethod
    def from_dict(cls, payload: Any) -> "EvaluationManifestV1":
        fields = EVALUATION_MANIFEST_FIELD_NAMES + ("evaluation_manifest_identity",)
        data = _strict_object(payload, fields, label="EvaluationManifestV1")
        value = cls(**{name: data[name] for name in EVALUATION_MANIFEST_FIELD_NAMES})
        value.validate()
        if data["evaluation_manifest_identity"] != evaluation_manifest_identity(value):
            raise CodecError("evaluation manifest identity does not match its payload")
        return value


def default_evaluation_manifest(
    *, evaluator_semantic_source_commit: str
) -> EvaluationManifestV1:
    root = default_evaluation_identity(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    corpus = default_evaluation_corpus(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    job_manifest = default_evaluation_job_manifest(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    value = EvaluationManifestV1(
        evaluation_identity=evaluation_identity(root),
        evaluation_contract_identity=evaluation_contract_identity(),
        evaluation_corpus_identity=evaluation_corpus_identity(corpus),
        evaluation_job_manifest_identity=evaluation_job_manifest_identity(job_manifest),
        checkpoint_identity=SMOKE_CHECKPOINT_ID,
        evaluator_semantic_source_commit=evaluator_semantic_source_commit,
    )
    value.validate()
    return value


def canonical_evaluation_manifest_bytes(value: EvaluationManifestV1) -> bytes:
    if not isinstance(value, EvaluationManifestV1):
        raise CodecError("evaluation manifest has the wrong DTO type")
    value.validate()
    encoded: list[bytes] = [pack_string(value.schema_id), pack_string(value.schema_id)]
    for name in EVALUATION_MANIFEST_FIELD_NAMES[1:]:
        field_value = getattr(value, name)
        if name in MANIFEST_OPTIONAL_FIELDS:
            encoded.append(pack_optional(field_value, pack_string))
        else:
            encoded.append(pack_string(field_value) if isinstance(field_value, str) else pack_string(str(field_value)))
    return b"".join(encoded)


def evaluation_manifest_identity(value: EvaluationManifestV1) -> str:
    return _digest(EVALUATION_MANIFEST_ID_PREFIX, canonical_evaluation_manifest_bytes(value))


def encode_evaluation_manifest_json(
    value: EvaluationManifestV1,
    *,
    context: EvaluationContextV1,
) -> bytes:
    _require_aggregate_context(context, manifest=value)
    return canonical_json_bytes(value.to_dict(context=context))


def decode_evaluation_manifest_json(data: bytes) -> EvaluationManifestV1:
    return EvaluationManifestV1.from_dict(parse_canonical_json(data))


def validate_evaluation_context(
    root: EvaluationIdentityV1,
    manifest: EvaluationManifestV1,
    corpus: EvaluationCorpusV1,
    job_manifest: EvaluationJobManifestV1,
    jobs: Sequence[EvaluationJobV1],
) -> None:
    """Fail-closed aggregate validation required before manifest issuance.

    Individual DTO validation checks shape and local invariants.  This
    function proves the cross-artifact graph: every identity is recomputed
    from its payload, the job vector is the same ordered vector at each
    aggregate, and evaluator/checkpoint context is consistent throughout.
    """

    for value, expected_type in (
        (root, EvaluationIdentityV1),
        (manifest, EvaluationManifestV1),
        (corpus, EvaluationCorpusV1),
        (job_manifest, EvaluationJobManifestV1),
    ):
        if not isinstance(value, expected_type):
            raise CodecError("evaluation context contains the wrong DTO type")
        value.validate()
    if not isinstance(jobs, (tuple, list)) or not jobs:
        raise CodecError("evaluation context requires an ordered non-empty job vector")
    ordered_jobs = tuple(jobs)
    for job in ordered_jobs:
        if not isinstance(job, EvaluationJobV1):
            raise CodecError("evaluation context job vector contains the wrong DTO type")
        job.validate()

    source_commits = {job.evaluator_semantic_source_commit for job in ordered_jobs}
    versions = {job.evaluator_semantic_version for job in ordered_jobs}
    if source_commits != {root.evaluator_semantic_source_commit}:
        raise CodecError("job evaluator source commits do not match root context")
    if versions != {root.evaluator_semantic_version}:
        raise CodecError("job evaluator versions do not match root context")
    if root.evaluation_contract_identity != corpus.evaluation_contract_identity:
        raise CodecError("root and corpus contract identities differ")
    if root.evaluation_contract_identity != manifest.evaluation_contract_identity:
        raise CodecError("root and manifest contract identities differ")
    if root.evaluation_contract_identity != job_manifest.evaluation_contract_identity:
        raise CodecError("root and job-manifest contract identities differ")
    if any(job.evaluation_contract_identity != root.evaluation_contract_identity for job in ordered_jobs):
        raise CodecError("job contract identity differs from aggregate context")
    if any(job.corpus_profile_identity != corpus.corpus_profile_identity for job in ordered_jobs):
        raise CodecError("job corpus profile differs from aggregate context")
    if any(job.evaluated_policy_checkpoint_identity != corpus.checkpoint_identity for job in ordered_jobs):
        raise CodecError("job checkpoint identity differs from corpus context")
    if root.checkpoint_identity != corpus.checkpoint_identity or manifest.checkpoint_identity != corpus.checkpoint_identity:
        raise CodecError("checkpoint identity differs across evaluation context")
    if manifest.evaluator_semantic_version != root.evaluator_semantic_version or manifest.evaluator_semantic_source_commit != root.evaluator_semantic_source_commit:
        raise CodecError("manifest evaluator source context differs from root")
    if job_manifest.evaluation_identity != evaluation_identity(root):
        raise CodecError("job manifest root identity differs from root context")
    if (manifest.source_dataset_identity, manifest.dataset_split_identity) != (
        corpus.source_dataset_identity,
        corpus.dataset_split_identity,
    ):
        raise CodecError("manifest dataset context differs from corpus")

    job_identities = tuple(evaluation_job_identity(job) for job in ordered_jobs)
    if corpus.evaluation_job_identities != job_identities:
        raise CodecError("corpus job vector is not the ordered job input vector")
    if job_manifest.evaluation_job_identities != job_identities:
        raise CodecError("job manifest vector is not the ordered job input vector")
    if evaluation_corpus_identity(corpus) != root.evaluation_corpus_identity:
        raise CodecError("root corpus identity does not match corpus payload")
    if evaluation_corpus_identity(corpus) != manifest.evaluation_corpus_identity:
        raise CodecError("manifest corpus identity does not match corpus payload")
    if evaluation_corpus_identity(corpus) != job_manifest.evaluation_corpus_identity:
        raise CodecError("job manifest corpus identity does not match corpus payload")
    if evaluation_identity(root) != manifest.evaluation_identity:
        raise CodecError("manifest root identity does not match root payload")
    if evaluation_job_manifest_identity(job_manifest) != manifest.evaluation_job_manifest_identity:
        raise CodecError("manifest job-manifest identity does not match payload")

    if corpus.corpus_profile_identity == IMPLEMENTATION_ACCEPTANCE_PROFILE:
        expected_jobs = implementation_acceptance_jobs(
            evaluator_semantic_source_commit=root.evaluator_semantic_source_commit
        )
        if ordered_jobs != expected_jobs:
            raise CodecError("implementation acceptance context is not the exact frozen eight-job schedule")
    elif corpus.corpus_profile_identity == MEANINGFUL_FIXED_MATCHUP_PROFILE:
        expected_jobs = meaningful_fixed_matchup_jobs(
            checkpoint_identity=corpus.checkpoint_identity,
            evaluator_semantic_source_commit=root.evaluator_semantic_source_commit,
        )
        if ordered_jobs != expected_jobs:
            raise CodecError("meaningful fixed-matchup context is not the exact frozen sixteen-job schedule")


@dataclasses.dataclass(frozen=True)
class EvaluationContextV1:
    """The complete typed context required to issue aggregate artifacts."""

    root: EvaluationIdentityV1
    manifest: EvaluationManifestV1
    corpus: EvaluationCorpusV1
    job_manifest: EvaluationJobManifestV1
    jobs: tuple[EvaluationJobV1, ...]

    def validate(self) -> None:
        validate_evaluation_context(
            self.root,
            self.manifest,
            self.corpus,
            self.job_manifest,
            self.jobs,
        )


def _require_aggregate_context(
    context: EvaluationContextV1,
    **expected: Any,
) -> None:
    if not isinstance(context, EvaluationContextV1):
        raise CodecError("aggregate evaluation context is required before issuance")
    context.validate()
    for name, value in expected.items():
        if getattr(context, name) != value:
            raise CodecError("artifact is not the value validated by aggregate context")


def default_evaluation_context(
    *, evaluator_semantic_source_commit: str
) -> EvaluationContextV1:
    jobs = implementation_acceptance_jobs(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    corpus = default_evaluation_corpus(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    root = default_evaluation_identity(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    job_manifest = default_evaluation_job_manifest(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    manifest = default_evaluation_manifest(
        evaluator_semantic_source_commit=evaluator_semantic_source_commit
    )
    context = EvaluationContextV1(root, manifest, corpus, job_manifest, jobs)
    context.validate()
    return context


# ---------------------------------------------------------------------------
# Public-safe FirstDivergenceV1 tagged union


@dataclasses.dataclass(frozen=True)
class FailureBeforeDivergenceV1:
    failure_stage: str
    error_code: str
    failed_decision_ordinal: int

    def validate(self) -> None:
        if self.failure_stage not in FAILURE_STAGES:
            raise CodecError("failure stage is not accepted")
        _validate_public_text(self.error_code, "failure.error_code")
        _validate_u64(self.failed_decision_ordinal, "failure.failed_decision_ordinal")
        if self.failure_stage == "before_public_decision" and self.failed_decision_ordinal != 0:
            raise CodecError("before-public-decision failure must use ordinal zero")

    def to_dict(self) -> dict[str, Any]:
        self.validate()
        return {
            "failure_stage": self.failure_stage,
            "error_code": self.error_code,
            "failed_decision_ordinal": self.failed_decision_ordinal,
        }

    @classmethod
    def from_dict(cls, payload: Any) -> "FailureBeforeDivergenceV1":
        data = _strict_object(
            payload,
            ("failure_stage", "error_code", "failed_decision_ordinal"),
            label="FailureBeforeDivergenceV1",
        )
        value = cls(data["failure_stage"], data["error_code"], data["failed_decision_ordinal"])
        value.validate()
        return value


def canonical_failure_before_divergence_bytes(value: FailureBeforeDivergenceV1) -> bytes:
    value.validate()
    return b"".join(
        (
            pack_string(value.failure_stage),
            pack_string(value.error_code),
            pack_u64(value.failed_decision_ordinal),
        )
    )


def _decode_failure_before_divergence(reader: _Reader) -> FailureBeforeDivergenceV1:
    value = FailureBeforeDivergenceV1(reader.string(), reader.string(), reader.u64())
    value.validate()
    return value


FIRST_DIVERGENCE_TAIL_FIELD_NAMES = (
    "semantic_decision_identity",
    "public_observation_digest",
    "model_input_identity",
    "ordered_candidate_domain_identity",
    "candidate_count",
    "candidate_public_action_keys",
    "candidate_descriptors",
    "score_vector_identity",
    "score_f32_bits",
    "teacher_selected_public_action_key",
    "model_selected_public_action_key",
    "decision_request_family",
    "continuation_context",
    "first_divergence_ordinal",
    "terminal_outcome",
    "failure_before_divergence",
)

FIRST_DIVERGENCE_FRAME_FIELDS = (
    "semantic_decision_identity",
    "public_observation_digest",
    "ordered_candidate_domain_identity",
    "candidate_count",
    "candidate_public_action_keys",
    "candidate_descriptors",
    "decision_request_family",
    "continuation_context",
)
FIRST_DIVERGENCE_SCORE_FIELDS = ("score_vector_identity", "score_f32_bits")
FIRST_DIVERGENCE_SELECTION_FIELDS = (
    "teacher_selected_public_action_key",
    "model_selected_public_action_key",
)


def _validate_public_identity_or_digest(value: str, field: str) -> None:
    _validate_digest(value, field)


def _validate_candidate_bundle(record: "FirstDivergenceV1") -> None:
    values = (
        record.candidate_count,
        record.candidate_public_action_keys,
        record.candidate_descriptors,
    )
    present = tuple(value is not None for value in values)
    if any(present) and not all(present):
        raise CodecError("candidate count/keys/descriptors must be an all-or-nothing bundle")
    if not all(present):
        return
    _validate_u32(record.candidate_count, "candidate_count")
    if record.candidate_count == 0:
        raise CodecError("candidate domain cannot be empty")
    if not isinstance(record.candidate_public_action_keys, tuple) or not isinstance(record.candidate_descriptors, tuple):
        raise CodecError("candidate domain values must be ordered tuples")
    if len(record.candidate_public_action_keys) != record.candidate_count or len(record.candidate_descriptors) != record.candidate_count:
        raise CodecError("candidate domain bundle count mismatch")
    seen: set[str] = set()
    for index, key in enumerate(record.candidate_public_action_keys):
        _validate_public_action_key(key, f"candidate_public_action_keys[{index}]")
        if key in seen:
            raise CodecError("candidate domain contains a duplicate public_action_key")
        seen.add(key)
    for index, descriptor in enumerate(record.candidate_descriptors):
        if not isinstance(descriptor, PublicCandidateDescriptorV1):
            raise CodecError("candidate descriptor has the wrong DTO type")
        descriptor.validate()
        if public_action_key(descriptor) != record.candidate_public_action_keys[index]:
            raise CodecError("candidate descriptor and public_action_key pairing differs")


def _validate_score_bundle(record: "FirstDivergenceV1") -> None:
    identity_present = record.score_vector_identity is not None
    bits_present = record.score_f32_bits is not None
    if identity_present != bits_present:
        raise CodecError("score identity and score bits must be present together")
    if not identity_present:
        return
    _validate_identity(record.score_vector_identity, SCORE_VECTOR_ID_PREFIX, "score_vector_identity")
    if not isinstance(record.score_f32_bits, tuple):
        raise CodecError("score_f32_bits must be an ordered tuple")
    if record.candidate_public_action_keys is None or record.candidate_count is None:
        raise CodecError("score bundle requires a complete candidate bundle")
    if len(record.score_f32_bits) != record.candidate_count:
        raise CodecError("score vector count differs from candidate count")
    score_vector = ScoreVectorV1(
        record.candidate_public_action_keys,
        record.score_f32_bits,
    )
    if score_vector_identity(score_vector) != record.score_vector_identity:
        raise CodecError("score vector identity does not match score bits")


@dataclasses.dataclass(frozen=True)
class FirstDivergenceV1:
    schema_id: str = FIRST_DIVERGENCE_SCHEMA_ID
    record_kind: int = DIVERGENCE
    evaluation_job_identity: str = ""
    observed_public_decision_count: int = 0
    semantic_decision_identity: Optional[str] = None
    public_observation_digest: Optional[str] = None
    model_input_identity: Optional[str] = None
    ordered_candidate_domain_identity: Optional[str] = None
    candidate_count: Optional[int] = None
    candidate_public_action_keys: Optional[tuple[str, ...]] = None
    candidate_descriptors: Optional[tuple[PublicCandidateDescriptorV1, ...]] = None
    score_vector_identity: Optional[str] = None
    score_f32_bits: Optional[tuple[str, ...]] = None
    teacher_selected_public_action_key: Optional[str] = None
    model_selected_public_action_key: Optional[str] = None
    decision_request_family: Optional[str] = None
    continuation_context: Optional[ContinuationContextV1] = None
    first_divergence_ordinal: Optional[int] = None
    terminal_outcome: Optional[TerminalOutcomeV1] = None
    failure_before_divergence: Optional[FailureBeforeDivergenceV1] = None

    def validate(self) -> None:
        if self.schema_id != FIRST_DIVERGENCE_SCHEMA_ID:
            raise CodecError("FirstDivergenceV1 schema is not accepted")
        _validate_u8(self.record_kind, "record_kind")
        if self.record_kind not in FIRST_DIVERGENCE_KINDS:
            raise CodecError("FirstDivergenceV1 record kind is not accepted")
        _validate_identity(self.evaluation_job_identity, EVALUATION_JOB_IDENTITY_PREFIX, "evaluation_job_identity")
        _validate_u64(self.observed_public_decision_count, "observed_public_decision_count")

        for field in ("semantic_decision_identity", "public_observation_digest"):
            value = getattr(self, field)
            if value is not None:
                _validate_public_identity_or_digest(value, field)
        if self.model_input_identity is not None:
            _validate_identity(self.model_input_identity, MODEL_INPUT_ID_PREFIX, "model_input_identity")
        if self.decision_request_family is not None:
            _validate_public_text(self.decision_request_family, "decision_request_family")
            _validate_decision_kind_token(self.decision_request_family, "decision_request_family")
        for field in FIRST_DIVERGENCE_SELECTION_FIELDS:
            value = getattr(self, field)
            if value is not None:
                _validate_public_action_key(value, field)
        if self.continuation_context is not None:
            if not isinstance(self.continuation_context, ContinuationContextV1):
                raise CodecError("continuation_context has the wrong DTO type")
            self.continuation_context.validate()
        if self.first_divergence_ordinal is not None:
            _validate_u64(self.first_divergence_ordinal, "first_divergence_ordinal")
        if self.terminal_outcome is not None:
            if not isinstance(self.terminal_outcome, TerminalOutcomeV1):
                raise CodecError("terminal_outcome has the wrong DTO type")
            self.terminal_outcome.validate()
        if self.failure_before_divergence is not None:
            if not isinstance(self.failure_before_divergence, FailureBeforeDivergenceV1):
                raise CodecError("failure_before_divergence has the wrong DTO type")
            self.failure_before_divergence.validate()

        _validate_candidate_bundle(self)
        if self.ordered_candidate_domain_identity is not None and self.decision_request_family is not None and self.candidate_public_action_keys is not None:
            validate_ordered_candidate_domain_identity(
                self.ordered_candidate_domain_identity,
                self.decision_request_family,
                self.candidate_public_action_keys,
            )
        _validate_score_bundle(self)
        if self.teacher_selected_public_action_key is not None:
            if self.candidate_public_action_keys is None or self.teacher_selected_public_action_key not in self.candidate_public_action_keys:
                raise CodecError("Teacher-selected key is outside the candidate domain")
        if self.model_selected_public_action_key is not None:
            if self.score_vector_identity is None or self.score_f32_bits is None:
                raise CodecError("model-selected key requires a complete score bundle")
            if self.candidate_public_action_keys is None or self.model_selected_public_action_key not in self.candidate_public_action_keys:
                raise CodecError("model-selected key is outside the candidate domain")

        if self.record_kind == DIVERGENCE:
            required = FIRST_DIVERGENCE_FRAME_FIELDS + ("model_input_identity",) + FIRST_DIVERGENCE_SCORE_FIELDS + FIRST_DIVERGENCE_SELECTION_FIELDS + (
                "first_divergence_ordinal",
            )
            for field in required:
                if getattr(self, field) is None:
                    raise CodecError(f"DIVERGENCE requires {field}")
            if self.terminal_outcome is not None or self.failure_before_divergence is not None:
                raise CodecError("DIVERGENCE cannot carry terminal or failure payload")
        elif self.record_kind == NO_DIVERGENCE_TERMINAL:
            for field in FIRST_DIVERGENCE_TAIL_FIELD_NAMES:
                if field != "terminal_outcome" and getattr(self, field) is not None:
                    raise CodecError(f"NO_DIVERGENCE_TERMINAL cannot carry {field}")
            if self.terminal_outcome is None:
                raise CodecError("NO_DIVERGENCE_TERMINAL requires terminal_outcome")
        else:
            if self.terminal_outcome is not None or self.first_divergence_ordinal is not None:
                raise CodecError("FAILURE_BEFORE_DIVERGENCE cannot carry terminal/ordinal payload")
            if self.failure_before_divergence is None:
                raise CodecError("FAILURE_BEFORE_DIVERGENCE requires failure payload")
            self._validate_failure_profile(self.failure_before_divergence.failure_stage)

    def _validate_failure_profile(self, stage: str) -> None:
        if stage == "before_public_decision":
            fields = FIRST_DIVERGENCE_FRAME_FIELDS + ("model_input_identity",) + FIRST_DIVERGENCE_SCORE_FIELDS + FIRST_DIVERGENCE_SELECTION_FIELDS
            if self.observed_public_decision_count != 0:
                raise CodecError("before-public-decision failure must observe zero decisions")
            for field in fields:
                if getattr(self, field) is not None:
                    raise CodecError(f"early failure cannot carry {field}")
            return
        if stage in ("public_frame_validation", "model_input_validation"):
            for field in FIRST_DIVERGENCE_FRAME_FIELDS:
                if getattr(self, field) is None:
                    raise CodecError(f"{stage} requires {field}")
            for field in ("model_input_identity",) + FIRST_DIVERGENCE_SCORE_FIELDS + FIRST_DIVERGENCE_SELECTION_FIELDS:
                if getattr(self, field) is not None:
                    raise CodecError(f"{stage} cannot carry {field}")
            return
        for field in FIRST_DIVERGENCE_FRAME_FIELDS + ("model_input_identity",):
            if getattr(self, field) is None:
                raise CodecError(f"{stage} requires {field}")
        if stage == "inference":
            for field in FIRST_DIVERGENCE_SCORE_FIELDS + ("model_selected_public_action_key",):
                if getattr(self, field) is not None:
                    raise CodecError("inference failure cannot carry score/model selection")
            return
        for field in FIRST_DIVERGENCE_SCORE_FIELDS:
            if getattr(self, field) is None:
                raise CodecError(f"{stage} requires {field}")
        if stage == "selection":
            if self.model_selected_public_action_key is not None:
                raise CodecError("selection failure cannot carry model selection")
            return
        if self.teacher_selected_public_action_key is None or self.model_selected_public_action_key is None:
            raise CodecError(f"{stage} requires both selected keys")

    def to_field_dict(self) -> dict[str, Any]:
        self.validate()
        result: dict[str, Any] = {
            "schema_id": self.schema_id,
            "record_kind": self.record_kind,
            "evaluation_job_identity": self.evaluation_job_identity,
            "observed_public_decision_count": self.observed_public_decision_count,
        }
        for field in FIRST_DIVERGENCE_TAIL_FIELD_NAMES:
            value = getattr(self, field)
            if isinstance(value, tuple):
                if field == "candidate_descriptors":
                    result[field] = [descriptor.to_dict() for descriptor in value]
                else:
                    result[field] = list(value)
            elif hasattr(value, "to_dict"):
                result[field] = value.to_dict()
            else:
                result[field] = value
        return result


def _pack_optional_candidate_keys(value: Optional[tuple[str, ...]]) -> bytes:
    if value is None:
        return pack_u8(0)
    return pack_u8(1) + pack_string_vector(value)


def _pack_optional_descriptors(value: Optional[tuple[PublicCandidateDescriptorV1, ...]]) -> bytes:
    if value is None:
        return pack_u8(0)
    return pack_u8(1) + pack_vector(canonical_public_candidate_descriptor_bytes(item) for item in value)


def _pack_optional_score_bits(value: Optional[tuple[str, ...]]) -> bytes:
    if value is None:
        return pack_u8(0)
    return pack_u8(1) + pack_string_vector(value)


def canonical_first_divergence_field_bytes(value: FirstDivergenceV1) -> bytes:
    if not isinstance(value, FirstDivergenceV1):
        raise CodecError("FirstDivergenceV1 has the wrong DTO type")
    value.validate()
    return b"".join(
        (
            pack_string(value.schema_id),
            pack_u8(value.record_kind),
            pack_string(value.evaluation_job_identity),
            pack_u64(value.observed_public_decision_count),
            pack_optional(value.semantic_decision_identity, pack_string),
            pack_optional(value.public_observation_digest, pack_string),
            pack_optional(value.model_input_identity, pack_string),
            pack_optional(value.ordered_candidate_domain_identity, pack_string),
            pack_optional(value.candidate_count, pack_u32),
            _pack_optional_candidate_keys(value.candidate_public_action_keys),
            _pack_optional_descriptors(value.candidate_descriptors),
            pack_optional(value.score_vector_identity, pack_string),
            _pack_optional_score_bits(value.score_f32_bits),
            pack_optional(value.teacher_selected_public_action_key, pack_string),
            pack_optional(value.model_selected_public_action_key, pack_string),
            pack_optional(value.decision_request_family, pack_string),
            pack_optional(value.continuation_context, canonical_continuation_context_bytes),
            pack_optional(value.first_divergence_ordinal, pack_u64),
            pack_optional(value.terminal_outcome, canonical_terminal_outcome_bytes),
            pack_optional(value.failure_before_divergence, canonical_failure_before_divergence_bytes),
        )
    )


def decode_first_divergence_field_bytes(data: bytes) -> FirstDivergenceV1:
    reader = _Reader(data)
    record = FirstDivergenceV1(
        schema_id=reader.string(),
        record_kind=reader.u8(),
        evaluation_job_identity=reader.string(),
        observed_public_decision_count=reader.u64(),
        semantic_decision_identity=reader.optional(reader.string),
        public_observation_digest=reader.optional(reader.string),
        model_input_identity=reader.optional(reader.string),
        ordered_candidate_domain_identity=reader.optional(reader.string),
        candidate_count=reader.optional(reader.u32),
        candidate_public_action_keys=reader.optional(lambda: reader.vector(reader.string)),
        candidate_descriptors=reader.optional(lambda: reader.vector(lambda: _decode_public_candidate_descriptor(reader))),
        score_vector_identity=reader.optional(reader.string),
        score_f32_bits=reader.optional(lambda: reader.vector(reader.string)),
        teacher_selected_public_action_key=reader.optional(reader.string),
        model_selected_public_action_key=reader.optional(reader.string),
        decision_request_family=reader.optional(reader.string),
        continuation_context=reader.optional(lambda: _decode_continuation_context(reader)),
        first_divergence_ordinal=reader.optional(reader.u64),
        terminal_outcome=reader.optional(lambda: _decode_terminal_outcome(reader)),
        failure_before_divergence=reader.optional(lambda: _decode_failure_before_divergence(reader)),
    )
    reader.require_end()
    record.validate()
    if canonical_first_divergence_field_bytes(record) != bytes(data):
        raise CodecError("FirstDivergenceV1 payload is not canonical")
    return record


# ---------------------------------------------------------------------------
# Canonical JSONL stream order validators


def _record_identity(record: Mapping[str, Any], field: str) -> str:
    if not isinstance(record, dict) or field not in record:
        raise CodecError(f"JSONL record is missing {field}")
    value = record[field]
    _validate_string(value, field)
    return value


def validate_gameplay_job_order(
    records: Sequence[Mapping[str, Any]],
    expected_job_identities: Sequence[str],
) -> None:
    actual = tuple(_record_identity(record, "evaluation_job_identity") for record in records)
    expected = tuple(expected_job_identities)
    for identity in expected:
        _validate_identity(identity, EVALUATION_JOB_IDENTITY_PREFIX, "expected evaluation_job_identity")
    if actual != expected:
        raise CodecError("gameplay job JSONL is not in evaluation-job-manifest order")


def validate_first_divergence_order(
    records: Sequence[Mapping[str, Any]],
    expected_job_identities: Sequence[str],
) -> None:
    expected = tuple(expected_job_identities)
    position = {identity: index for index, identity in enumerate(expected)}
    if len(position) != len(expected):
        raise CodecError("expected evaluation job vector contains duplicates")
    actual: list[str] = []
    for record in records:
        identity = _record_identity(record, "evaluation_job_identity")
        _validate_identity(identity, EVALUATION_JOB_IDENTITY_PREFIX, "evaluation_job_identity")
        if identity not in position:
            raise CodecError("divergence JSONL contains an unknown job")
        if identity in actual:
            raise CodecError("divergence JSONL contains a duplicate job")
        actual.append(identity)
    if any(position[left] >= position[right] for left, right in zip(actual, actual[1:])):
        raise CodecError("divergence JSONL is not in job-manifest order")


def validate_offline_sample_order(records: Sequence[Mapping[str, Any]]) -> None:
    """Validate the frozen validation-then-test, unsigned-UTF-8 order."""

    previous_partition = "validation"
    previous_identity: Optional[bytes] = None
    seen: set[tuple[str, str]] = set()
    for record in records:
        partition = _record_identity(record, "partition")
        if partition not in ("validation", "test"):
            raise CodecError("offline sample has an invalid partition")
        identity = _record_identity(record, "bc_sample_identity")
        _validate_bc_sample_identity(identity, "bc_sample_identity")
        if partition == "validation" and previous_partition == "test":
            raise CodecError("offline sample validation record follows a test record")
        if partition != previous_partition:
            previous_identity = None
        key = (partition, identity)
        if key in seen:
            raise CodecError("offline sample stream contains a duplicate sample")
        seen.add(key)
        encoded_identity = identity.encode("utf-8")
        if previous_identity is not None and encoded_identity <= previous_identity:
            raise CodecError("offline sample identities are not ascending unsigned UTF-8")
        previous_partition = partition
        previous_identity = encoded_identity


# Backwards-friendly aliases for callers that use the artifact terminology.
EvaluationJobManifest = EvaluationJobManifestV1
EvaluationManifest = EvaluationManifestV1
EvaluationJob = EvaluationJobV1
EvaluationCorpus = EvaluationCorpusV1
ScoreVector = ScoreVectorV1
FirstDivergence = FirstDivergenceV1


__all__ = [
    "CodecError",
    "Task5CodecError",
    "pack_u8",
    "pack_u16",
    "pack_u32",
    "pack_u64",
    "pack_i32",
    "pack_bool",
    "pack_string",
    "pack_vector",
    "pack_string_vector",
    "pack_optional",
    "canonical_json_bytes",
    "parse_canonical_json",
    "canonical_jsonl_bytes",
    "parse_canonical_jsonl",
    "score_f32_bits",
    "score_f32_bytes",
    "score_f32_value",
    "PublicChoiceV1",
    "PublicReferenceV1",
    "PublicCandidateDescriptorV1",
    "canonical_public_action_key_bytes",
    "public_action_key",
    "is_public_action_key",
    "validate_public_action_key",
    "canonical_public_candidate_domain_bytes",
    "public_candidate_domain_digest",
    "canonical_fallback_ordered_candidate_domain_bytes",
    "fallback_ordered_candidate_domain_identity",
    "ordered_candidate_domain_identity",
    "validate_ordered_candidate_domain_identity",
    "ContinuationContextV1",
    "TerminalOutcomeV1",
    "FailureBeforeDivergenceV1",
    "ScoreVectorV1",
    "EvaluationContractIdentityV1",
    "EvaluationJobV1",
    "EvaluationCorpusV1",
    "EvaluationIdentityV1",
    "EvaluationJobManifestV1",
    "EvaluationManifestV1",
    "EvaluationContextV1",
    "FirstDivergenceV1",
    "canonical_public_choice_bytes",
    "canonical_public_reference_bytes",
    "canonical_public_candidate_descriptor_bytes",
    "canonical_continuation_context_bytes",
    "canonical_terminal_outcome_bytes",
    "canonical_failure_before_divergence_bytes",
    "canonical_score_vector_bytes",
    "decode_score_vector_bytes",
    "score_vector_identity",
    "canonical_evaluation_contract_identity_bytes",
    "evaluation_contract_identity",
    "canonical_evaluation_job_bytes",
    "evaluation_job_identity",
    "canonical_evaluation_corpus_bytes",
    "evaluation_corpus_identity",
    "canonical_evaluation_identity_bytes",
    "evaluation_identity",
    "canonical_evaluation_job_manifest_bytes",
    "evaluation_job_manifest_identity",
    "canonical_evaluation_manifest_bytes",
    "evaluation_manifest_identity",
    "canonical_first_divergence_field_bytes",
    "decode_first_divergence_field_bytes",
    "default_evaluation_job",
    "default_evaluation_corpus",
    "default_evaluation_identity",
    "default_evaluation_contract_identity",
    "default_evaluation_job_manifest",
    "default_evaluation_manifest",
    "default_evaluation_context",
    "implementation_acceptance_jobs",
    "meaningful_fixed_matchup_jobs",
    "encode_score_vector_json",
    "decode_score_vector_json",
    "encode_evaluation_corpus_json",
    "decode_evaluation_corpus_json",
    "encode_evaluation_identity_json",
    "decode_evaluation_identity_json",
    "encode_evaluation_job_manifest_json",
    "decode_evaluation_job_manifest_json",
    "encode_evaluation_manifest_json",
    "decode_evaluation_manifest_json",
    "encode_evaluation_contract_identity_json",
    "decode_evaluation_contract_identity_json",
    "select_score_vector",
    "validate_evaluation_context",
    "validate_gameplay_job_order",
    "validate_first_divergence_order",
    "validate_offline_sample_order",
    "DIVERGENCE",
    "NO_DIVERGENCE_TERMINAL",
    "FAILURE_BEFORE_DIVERGENCE",
]
