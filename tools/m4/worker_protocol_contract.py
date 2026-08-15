"""Pure validation helpers for the M4 worker JSONL contract.

This module deliberately contains no process, scheduling, or simulation code.
It is shared by protocol-focused tests as an executable description of the
wire contract; the native worker remains the protocol implementation.
"""

from __future__ import annotations

import json
from typing import Any


PROTOCOL_SCHEMA = "ocgforge.m4.worker.v1"
PROTOCOL_VERSION = PROTOCOL_SCHEMA
CANONICAL_RULES_BUNDLE_ID = (
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f"
)
CANONICAL_PATCHSET_SHA256 = (
    "6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd"
)
CANONICAL_DECK_HASHES = (
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
)


class ProtocolContractError(ValueError):
    """Raised when a JSONL message violates the M4 worker contract."""


def _duplicate_key_check(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolContractError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def parse_json_line(line: str) -> dict[str, Any]:
    """Parse one strict JSON object line without accepting duplicate keys."""

    try:
        value = json.loads(
            line,
            object_pairs_hook=_duplicate_key_check,
            parse_constant=lambda value: (_ for _ in ()).throw(
                ProtocolContractError(f"non-finite JSON constant: {value}")
            ),
        )
    except ProtocolContractError:
        raise
    except (TypeError, json.JSONDecodeError) as error:
        raise ProtocolContractError(f"malformed JSON: {error}") from error
    if not isinstance(value, dict):
        raise ProtocolContractError("protocol message must be a JSON object")
    return value


def recover_job_id(line: str) -> str | None:
    """Recover a string job ID only when the complete JSON object is valid."""

    try:
        value = parse_json_line(line)
    except ProtocolContractError:
        return None
    job_id = value.get("job_id")
    return job_id if isinstance(job_id, str) and job_id else None


def _require_keys(message: dict[str, Any], required: set[str]) -> None:
    actual = set(message)
    if actual != required:
        missing = required.difference(actual)
        extra = actual.difference(required)
        raise ProtocolContractError(
            f"wrong keys: missing={sorted(missing)}, extra={sorted(extra)}"
        )


def _require_type(message: dict[str, Any], key: str, expected: type[Any]) -> Any:
    value = message[key]
    if expected is int and (isinstance(value, bool) or not isinstance(value, int)):
        raise ProtocolContractError(f"{key} must be an unsigned integer")
    if expected is not int and not isinstance(value, expected):
        raise ProtocolContractError(f"{key} has the wrong JSON type")
    return value


def _require_unsigned(message: dict[str, Any], key: str) -> int:
    value = _require_type(message, key, int)
    if value < 0:
        raise ProtocolContractError(f"{key} must not be negative")
    return value


def _require_string(message: dict[str, Any], key: str) -> str:
    return _require_type(message, key, str)


def validate_ready(message: dict[str, Any]) -> None:
    required = {
        "schema",
        "type",
        "protocol_version",
        "pid",
        "rules_bundle_id",
        "core_patchset_sha256",
        "deck_hashes",
        "format_id",
        "duel_mode_name",
        "duel_flags",
        "compiler_identity",
        "build_type",
        "worker_identity",
    }
    _require_keys(message, required)
    if message["schema"] != PROTOCOL_SCHEMA or message["type"] != "ready":
        raise ProtocolContractError("invalid ready schema or type")
    if message["protocol_version"] != PROTOCOL_VERSION:
        raise ProtocolContractError("wrong worker protocol version")
    if _require_unsigned(message, "pid") == 0:
        raise ProtocolContractError("worker PID must be nonzero")
    if message["rules_bundle_id"] != CANONICAL_RULES_BUNDLE_ID:
        raise ProtocolContractError("wrong rules bundle identity")
    if message["core_patchset_sha256"] != CANONICAL_PATCHSET_SHA256:
        raise ProtocolContractError("wrong core patchset identity")
    if message["deck_hashes"] != list(CANONICAL_DECK_HASHES):
        raise ProtocolContractError("wrong ordered locked deck hashes")
    if message["format_id"] != "TCG_ADVANCED_2026_05_18":
        raise ProtocolContractError("wrong canonical format")
    if message["duel_mode_name"] != "DUEL_MODE_MR5":
        raise ProtocolContractError("wrong canonical duel mode")
    if message["duel_flags"] != 190464:
        raise ProtocolContractError("wrong canonical duel flags")
    _require_string(message, "compiler_identity")
    _require_string(message, "build_type")
    _require_string(message, "worker_identity")


_ERROR_KEYS = {
    "retries",
    "unsupported",
    "automatic",
    "truncated",
    "core_errors",
    "worker_errors",
}
_TIMING_KEYS = {
    "core_process",
    "protocol_candidate",
    "continuation",
    "observation",
    "trace_hash",
    "serialization",
    "other",
    "trace_persistence",
}
_COUNTER_KEYS = {
    "ocg_duel_process",
    "ocg_duel_query",
    "ocg_duel_query_location",
    "ocg_duel_query_field",
    "ocg_duel_query_count",
    "script_reader_requests",
    "script_loads",
    "observations",
    "entities_projected",
    "candidate_sets",
    "candidate_total",
    "candidate_max",
    "semantic_hashes",
    "trace_bytes_serialized",
}


def _validate_unsigned_object(message: dict[str, Any], key: str, keys: set[str]) -> None:
    if not isinstance(message, dict):
        raise ProtocolContractError(f"{key} must be an object")
    if set(message) != keys:
        raise ProtocolContractError(f"wrong keys in {key}")
    for child_key in keys:
        _require_unsigned(message, child_key)


def validate_result(message: dict[str, Any], *, expected_job_id: str) -> None:
    required = {
        "schema",
        "type",
        "status",
        "job_id",
        "terminal",
        "winner",
        "win_reason",
        "engine_steps",
        "interactive_decisions",
        "semantic_action_count",
        "gameplay_hash",
        "trace_hash",
        "simulation_elapsed_us",
        "coordinator_elapsed_us",
        "errors",
        "timing_us",
        "counters",
        "worker",
        "failure_code",
        "error_message",
    }
    _require_keys(message, required)
    if message["schema"] != PROTOCOL_SCHEMA or message["type"] != "result":
        raise ProtocolContractError("invalid result schema or type")
    if message["status"] not in {"passed", "failed"}:
        raise ProtocolContractError("invalid result status")
    if _require_string(message, "job_id") != expected_job_id:
        raise ProtocolContractError("result job ID does not match request")
    if not isinstance(message["terminal"], bool):
        raise ProtocolContractError("terminal must be a boolean")
    for key in ("winner", "win_reason"):
        if message[key] is not None and (
            isinstance(message[key], bool)
            or not isinstance(message[key], int)
            or message[key] < 0
            or message[key] > 255
        ):
            raise ProtocolContractError(f"{key} must be an unsigned integer or null")
    for key in (
        "engine_steps",
        "interactive_decisions",
        "semantic_action_count",
        "simulation_elapsed_us",
    ):
        _require_unsigned(message, key)
    if message["coordinator_elapsed_us"] is not None:
        raise ProtocolContractError("worker result must leave coordinator_elapsed_us null")
    for key in ("gameplay_hash", "trace_hash"):
        if message[key] is not None:
            _require_string(message, key)
    _validate_unsigned_object(message["errors"], "errors", _ERROR_KEYS)
    _validate_unsigned_object(message["timing_us"], "timing_us", _TIMING_KEYS)
    _validate_unsigned_object(message["counters"], "counters", _COUNTER_KEYS)
    if message["status"] == "passed":
        if not message["terminal"] or message["gameplay_hash"] is None:
            raise ProtocolContractError("passed result lacks terminal gameplay evidence")
        if any(message["errors"].get(key) != 0 for key in _ERROR_KEYS):
            raise ProtocolContractError("passed result has nonzero integrity counters")
        if message["failure_code"] is not None or message["error_message"] is not None:
            raise ProtocolContractError("passed result contains failure details")
    else:
        if message["terminal"] or message["winner"] is not None or message["win_reason"] is not None:
            raise ProtocolContractError("failed result exposes terminal winner data")
        if message["gameplay_hash"] is not None or message["trace_hash"] is not None:
            raise ProtocolContractError("failed result exposes gameplay hashes")
        if not isinstance(message["failure_code"], str) or not message["failure_code"]:
            raise ProtocolContractError("failed result lacks failure_code")
        if not isinstance(message["error_message"], str) or not message["error_message"]:
            raise ProtocolContractError("failed result lacks error_message")
    worker = message["worker"]
    if not isinstance(worker, dict):
        raise ProtocolContractError("worker metadata must be an object")
    if set(worker) != {"pid", "restart_index", "crashed", "restarted"}:
        raise ProtocolContractError("wrong worker metadata keys")
    if _require_unsigned(worker, "pid") == 0:
        raise ProtocolContractError("worker PID must be nonzero")
    _require_unsigned(worker, "restart_index")
    if not isinstance(worker["crashed"], bool) or not isinstance(worker["restarted"], bool):
        raise ProtocolContractError("worker crash metadata must be boolean")
