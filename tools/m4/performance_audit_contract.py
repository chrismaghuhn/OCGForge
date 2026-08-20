"""Additive M4.2 observation-path audit contract.

This module is intentionally separate from the M4 worker protocol and
throughput benchmark contract.  It describes optional audit data for a later
audit runner without changing production worker-result compatibility.
"""

from __future__ import annotations

from typing import Any, Mapping


UINT64_MAX = (1 << 64) - 1


class PerformanceAuditContractError(ValueError):
    """Raised when an M4.2 audit telemetry value is invalid."""


OBSERVATION_TIMING_KEYS = (
    "observation_query_field",
    "observation_query_location",
    "observation_query_individual",
    "observation_query_decode",
    "observation_zone_projection",
    "observation_entity_projection",
    "observation_relationship_projection",
    "observation_visibility_privacy",
    "observation_candidate_consistency",
    "observation_canonical_serialization",
    "observation_hash",
    "observation_other",
)
OBSERVATION_COUNTER_KEYS = (
    "observations",
    "query_field_calls",
    "query_location_calls",
    "query_individual_calls",
    "entities_projected",
    "identity_known_entities",
    "redacted_entities",
    "static_card_data_lookups",
    "current_property_projections",
    "relationship_objects",
    "allocation_copy_events",
    "script_loads",
)
COORDINATOR_TIMING_KEYS = (
    "worker_compute_wait",
    "pipe_read_write_cpu",
    "json_encode_decode_cpu",
    "dispatch_queue_overhead",
    "other",
)
AUDIT_TELEMETRY_KEYS = frozenset(
    {
        "observation_timing_us",
        "observation_counters",
        "coordinator_timing_us",
    }
)


def _zero_object(keys: tuple[str, ...]) -> dict[str, int]:
    return {key: 0 for key in keys}


def default_observation_timing_us() -> dict[str, int]:
    """Return a fresh zero-valued observation timing group."""

    return _zero_object(OBSERVATION_TIMING_KEYS)


def default_observation_counters() -> dict[str, int]:
    """Return a fresh zero-valued observation counter group."""

    return _zero_object(OBSERVATION_COUNTER_KEYS)


def default_coordinator_timing_us() -> dict[str, int]:
    """Return a fresh zero-valued coordinator timing group."""

    return _zero_object(COORDINATOR_TIMING_KEYS)


def default_audit_telemetry() -> dict[str, dict[str, int]]:
    """Return a fresh default-valid M4.2 telemetry bundle."""

    return {
        "observation_timing_us": default_observation_timing_us(),
        "observation_counters": default_observation_counters(),
        "coordinator_timing_us": default_coordinator_timing_us(),
    }


def _require_nonnegative_uint64(value: Any, key: str) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value < 0
        or value > UINT64_MAX
    ):
        raise PerformanceAuditContractError(
            f"{key} must be a nonnegative unsigned integer"
        )
    return value


def _validate_group(
    value: Any,
    name: str,
    keys: tuple[str, ...],
) -> dict[str, int]:
    if not isinstance(value, Mapping) or set(value) != set(keys):
        raise PerformanceAuditContractError(f"{name} has the wrong keys")
    validated = {
        key: _require_nonnegative_uint64(value[key], f"{name}.{key}")
        for key in keys
    }
    return validated


def _validate_bundle(
    telemetry: Mapping[str, Any],
    *,
    outer_observation_us: Any = None,
) -> None:
    if set(telemetry) != AUDIT_TELEMETRY_KEYS:
        raise PerformanceAuditContractError("audit telemetry has the wrong keys")

    observation_timing = _validate_group(
        telemetry["observation_timing_us"],
        "observation_timing_us",
        OBSERVATION_TIMING_KEYS,
    )
    _validate_group(
        telemetry["observation_counters"],
        "observation_counters",
        OBSERVATION_COUNTER_KEYS,
    )
    _validate_group(
        telemetry["coordinator_timing_us"],
        "coordinator_timing_us",
        COORDINATOR_TIMING_KEYS,
    )

    if outer_observation_us is not None:
        outer = _require_nonnegative_uint64(
            outer_observation_us,
            "timing_us.observation",
        )
        if sum(observation_timing.values()) > outer:
            raise PerformanceAuditContractError(
                "nested observation timing exceeds outer observation timing"
            )


def validate_audit_telemetry(
    report: Mapping[str, Any],
    *,
    require: bool = False,
    outer_observation_us: Any = None,
) -> None:
    """Validate optional audit telemetry without validating M4 primary fields.

    Missing telemetry is compatible with existing M4 reports by default.  A
    caller validating an audit report must pass ``require=True``; a partial or
    malformed bundle is rejected whenever any audit group is present.
    """

    if not isinstance(report, Mapping):
        raise PerformanceAuditContractError("audit report must be an object")
    if not isinstance(require, bool):
        raise PerformanceAuditContractError("require must be a boolean")

    present = set(report).intersection(AUDIT_TELEMETRY_KEYS)
    if not present:
        if require:
            raise PerformanceAuditContractError("audit telemetry is missing")
        return
    if present != AUDIT_TELEMETRY_KEYS:
        raise PerformanceAuditContractError("audit telemetry is incomplete")

    if outer_observation_us is None:
        timing = report.get("timing_us")
        if isinstance(timing, Mapping) and "observation" in timing:
            outer_observation_us = timing["observation"]

    _validate_bundle(
        {key: report[key] for key in AUDIT_TELEMETRY_KEYS},
        outer_observation_us=outer_observation_us,
    )


def validate_audit_report(report: Mapping[str, Any]) -> None:
    """Validate a report as an explicit audit report with required telemetry."""

    validate_audit_telemetry(report, require=True)


__all__ = [
    "AUDIT_TELEMETRY_KEYS",
    "COORDINATOR_TIMING_KEYS",
    "OBSERVATION_COUNTER_KEYS",
    "OBSERVATION_TIMING_KEYS",
    "PerformanceAuditContractError",
    "default_audit_telemetry",
    "default_coordinator_timing_us",
    "default_observation_counters",
    "default_observation_timing_us",
    "validate_audit_report",
    "validate_audit_telemetry",
]
