"""Python-side validation and JSONL helpers for the native M4 worker.

The native worker remains authoritative for gameplay.  This module only
validates value messages at the coordinator boundary and converts Python job
values to the exact request shape accepted by the native JSONL parser.
"""

from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path
import re
from typing import Any, Mapping

from .worker_protocol_contract import (
    CANONICAL_DECK_HASHES,
    CANONICAL_PATCHSET_SHA256,
    CANONICAL_RULES_BUNDLE_ID,
    PROTOCOL_SCHEMA,
    PROTOCOL_VERSION,
    WORKER_IDENTITY,
    ProtocolContractError,
    parse_json_line,
    recover_job_id,
    validate_ready as _validate_contract_ready,
    validate_result as _validate_contract_result,
)


class ProtocolValidationError(ValueError):
    """Raised when a coordinator-bound worker message is not trustworthy."""


@dataclass(frozen=True)
class HandshakeExpectation:
    """Exact immutable identity expected from every native worker."""

    protocol_schema: str = PROTOCOL_SCHEMA
    protocol_version: str = PROTOCOL_VERSION
    worker_identity: str = WORKER_IDENTITY
    rules_bundle_id: str = CANONICAL_RULES_BUNDLE_ID
    patchset_sha256: str = CANONICAL_PATCHSET_SHA256
    deck_hashes: tuple[str, str] = CANONICAL_DECK_HASHES
    format_id: str = "TCG_ADVANCED_2026_05_18"
    duel_mode: str = "DUEL_MODE_MR5"
    duel_flags: int = 190464
    compiler_identity: str | None = None
    build_type: str | None = None

    @classmethod
    def canonical(cls) -> "HandshakeExpectation":
        return cls()


def _expected_value(expected: HandshakeExpectation | Mapping[str, Any], name: str) -> Any:
    if isinstance(expected, Mapping):
        aliases = {
            "protocol_schema": ("protocol_schema", "schema"),
            "protocol_version": ("protocol_version",),
            "worker_identity": ("worker_identity",),
            "rules_bundle_id": ("rules_bundle_id",),
            "patchset_sha256": ("patchset_sha256", "core_patchset_sha256"),
            "deck_hashes": ("deck_hashes",),
            "format_id": ("format_id",),
            "duel_mode": ("duel_mode", "duel_mode_name"),
            "duel_flags": ("duel_flags",),
            "compiler_identity": ("compiler_identity",),
            "build_type": ("build_type",),
        }
        for alias in aliases[name]:
            if alias in expected:
                return expected[alias]
        return getattr(HandshakeExpectation.canonical(), name)
    return getattr(expected, name)


def _raise_protocol(error: Exception) -> ProtocolValidationError:
    if isinstance(error, ProtocolValidationError):
        return error
    return ProtocolValidationError(str(error))


def validate_ready(
    message: Mapping[str, Any],
    expected: HandshakeExpectation | Mapping[str, Any] | None = None,
) -> None:
    """Validate a worker ready message against the exact expected identity."""

    selected = HandshakeExpectation.canonical() if expected is None else expected
    try:
        candidate = dict(message)
        _validate_contract_ready(candidate)
        checks = {
            "schema": _expected_value(selected, "protocol_schema"),
            "protocol_version": _expected_value(selected, "protocol_version"),
            "worker_identity": _expected_value(selected, "worker_identity"),
            "rules_bundle_id": _expected_value(selected, "rules_bundle_id"),
            "core_patchset_sha256": _expected_value(selected, "patchset_sha256"),
            "deck_hashes": list(_expected_value(selected, "deck_hashes")),
            "format_id": _expected_value(selected, "format_id"),
            "duel_mode_name": _expected_value(selected, "duel_mode"),
            "duel_flags": _expected_value(selected, "duel_flags"),
        }
        for key, value in checks.items():
            if candidate.get(key) != value:
                raise ProtocolValidationError(f"worker ready identity mismatch: {key}")
        for key in ("compiler_identity", "build_type"):
            expected_value = _expected_value(selected, key)
            if expected_value is not None and candidate.get(key) != expected_value:
                raise ProtocolValidationError(f"worker ready identity mismatch: {key}")
    except ProtocolValidationError:
        raise
    except (KeyError, TypeError, ProtocolContractError, ValueError) as error:
        raise _raise_protocol(error) from error


_COORDINATOR_RESULT_FIELDS = {
    "coordinator",
    "coordinator_errors",
    "coordinator_elapsed_us",
}


def validate_result(message: Mapping[str, Any], expected_job_id: str) -> None:
    """Validate one result against the exact native worker wire contract."""

    try:
        _validate_contract_result(dict(message), expected_job_id=expected_job_id)
    except ProtocolValidationError:
        raise
    except (KeyError, TypeError, ProtocolContractError, ValueError) as error:
        raise _raise_protocol(error) from error


def assert_primary_integrity(result: Mapping[str, Any]) -> None:
    """Fail closed unless a result is valid primary benchmark evidence."""

    try:
        job_id = result.get("job_id")
        if not isinstance(job_id, str) or not job_id:
            raise ProtocolValidationError("primary result has no job ID")
        native_result = dict(result)
        coordinator_elapsed_us = native_result.get("coordinator_elapsed_us")
        if coordinator_elapsed_us is not None and (
            isinstance(coordinator_elapsed_us, bool)
            or not isinstance(coordinator_elapsed_us, int)
            or coordinator_elapsed_us < 0
        ):
            raise ProtocolValidationError(
                "coordinator_elapsed_us must be a nonnegative integer or null"
            )
        for key in _COORDINATOR_RESULT_FIELDS:
            native_result.pop(key, None)
        # The coordinator owns this field after receipt; restore the native
        # wire value before applying the exact worker contract.
        native_result["coordinator_elapsed_us"] = None
        validate_result(native_result, job_id)
        if result.get("status") != "passed" or result.get("terminal") is not True:
            raise ProtocolValidationError("primary result is not a passed terminal game")
        errors = result.get("errors")
        if not isinstance(errors, Mapping):
            raise ProtocolValidationError("primary result has no error counters")
        if any(errors.get(key) != 0 for key in (
            "retries",
            "unsupported",
            "automatic",
            "truncated",
            "core_errors",
            "worker_errors",
        )):
            raise ProtocolValidationError("primary result has a nonzero integrity counter")
        required_coordinator_errors = {
            "retries",
            "handshake",
            "malformed_protocol",
            "failed_games",
            "worker_crashes",
            "worker_restarts",
        }
        coordinator_errors = result.get("coordinator_errors")
        if not isinstance(coordinator_errors, Mapping) or set(coordinator_errors) != required_coordinator_errors:
            raise ProtocolValidationError("primary result has malformed coordinator counters")
        if any(
            isinstance(value, bool) or not isinstance(value, int) or value != 0
            for value in coordinator_errors.values()
        ):
            raise ProtocolValidationError("primary result has a coordinator integrity failure")
        coordinator = result.get("coordinator")
        if not isinstance(coordinator, Mapping):
            raise ProtocolValidationError("primary result has no coordinator metadata")
        if (
            coordinator.get("worker_crashed") is not False
            or coordinator.get("worker_restarted") is not False
        ):
            raise ProtocolValidationError("primary result was affected by worker failure")
        worker = result.get("worker")
        if not isinstance(worker, Mapping) or worker.get("crashed") or worker.get("restarted"):
            raise ProtocolValidationError("primary result was affected by worker lifecycle failure")
    except ProtocolValidationError:
        raise
    except (KeyError, TypeError, ValueError) as error:
        raise _raise_protocol(error) from error


def job_to_message(job: Mapping[str, Any]) -> dict[str, Any]:
    """Convert a value job to the exact native worker request envelope."""

    required = {
        "job_id": job.get("job_id"),
        "seed": job.get("seed"),
        "seat_assignment": job.get("seat_assignment", "normal"),
        "starting_player": job.get("starting_player", 0),
        "max_steps": job.get("max_steps", 2200),
        "canonical_rules_id": job.get("canonical_rules_id", CANONICAL_RULES_BUNDLE_ID),
        "mode": job.get("mode", "throughput"),
        "observation_mode": job.get("observation_mode", "full"),
        "instrumentation": job.get("instrumentation", False),
        "persist_trace": job.get("persist_trace", False),
        "replay_actions": list(job.get("replay_actions", [])),
        "focus_codes": list(job.get("focus_codes", [])),
        "setup_script": str(job.get("setup_script", "")),
        "force_unsupported": job.get("force_unsupported", False),
    }
    if not isinstance(required["job_id"], str) or not required["job_id"]:
        raise ValueError("job_id must be a nonempty string")
    if isinstance(required["seed"], bool) or not isinstance(required["seed"], int) or required["seed"] < 0:
        raise ValueError("seed must be a nonnegative integer")
    for key in ("starting_player", "max_steps"):
        value = required[key]
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(f"{key} must be a nonnegative integer")
    if required["seat_assignment"] not in {"normal", "mirror"}:
        raise ValueError("seat_assignment must be normal or mirror")
    if required["mode"] not in {"conformance", "throughput"}:
        raise ValueError("mode must be conformance or throughput")
    if required["observation_mode"] not in {"full", "off_diagnostic"}:
        raise ValueError("observation_mode has an unsupported value")
    for key in ("instrumentation", "persist_trace", "force_unsupported"):
        if not isinstance(required[key], bool):
            raise ValueError(f"{key} must be boolean")
    message = {
        "schema": PROTOCOL_SCHEMA,
        "type": "job",
        **required,
    }
    trace_output = job.get("trace_output")
    if trace_output:
        message["trace_output"] = str(Path(trace_output))
    return message


def encode_job(job: Mapping[str, Any]) -> str:
    """Serialize a job as one deterministic UTF-8 JSONL line."""

    return json.dumps(job_to_message(job), ensure_ascii=False, separators=(",", ":"))


def decode_line(line: str) -> dict[str, Any]:
    """Parse one strict JSONL line and translate errors to the coordinator type."""

    try:
        return parse_json_line(line)
    except ProtocolContractError as error:
        raise ProtocolValidationError(str(error)) from error


def recover_line_job_id(line: str) -> str | None:
    return recover_job_id(line)


def is_sha256(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{64}", value) is not None


__all__ = [
    "CANONICAL_DECK_HASHES",
    "CANONICAL_PATCHSET_SHA256",
    "CANONICAL_RULES_BUNDLE_ID",
    "HandshakeExpectation",
    "PROTOCOL_SCHEMA",
    "PROTOCOL_VERSION",
    "ProtocolValidationError",
    "WORKER_IDENTITY",
    "assert_primary_integrity",
    "decode_line",
    "encode_job",
    "is_sha256",
    "job_to_message",
    "recover_line_job_id",
    "validate_ready",
    "validate_result",
]
