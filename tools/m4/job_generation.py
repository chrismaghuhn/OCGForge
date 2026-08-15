"""Deterministic M4 simulation-job derivation.

The job mapping is intentionally pure: only the master seed and numeric job
index affect a job's seed and balanced seat/starting-player partition.
"""

from __future__ import annotations

from typing import Any, Dict, List


MASK64 = (1 << 64) - 1
GOLDEN = 0x9E3779B97F4A7C15
PARTITIONS = (
    ("normal", 0),
    ("mirror", 0),
    ("normal", 1),
    ("mirror", 1),
)
RULES_BUNDLE_ID = "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f"

_MODE_VALUES = frozenset(("conformance", "throughput"))
_OBSERVATION_MODE_VALUES = frozenset(("full", "off_diagnostic"))


def splitmix64(value: int) -> int:
    """Return the specified SplitMix64 mix of a value using modulo 2**64."""

    value = (value + GOLDEN) & MASK64
    value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
    return (value ^ (value >> 31)) & MASK64


def _require_integer(value: int, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{name} must be an integer")
    return value


def _require_nonnegative(value: int, name: str) -> int:
    value = _require_integer(value, name)
    if value < 0:
        raise ValueError(f"{name} must be nonnegative")
    return value


def _validate_execution_options(
    mode: str,
    observation_mode: str,
) -> None:
    if mode not in _MODE_VALUES:
        raise ValueError(f"unsupported mode: {mode!r}")
    if observation_mode not in _OBSERVATION_MODE_VALUES:
        raise ValueError(f"unsupported observation mode: {observation_mode!r}")


def derive_job(master_seed: int, index: int, max_steps: int = 2200) -> Dict[str, Any]:
    """Derive one value-only job from ``(master_seed, index)``.

    The default execution settings describe the primary throughput contract.
    Use :func:`configure_job` to create a conformance or diagnostic variant
    without changing any identity, seed, seat, or step-limit field.
    """

    index = _require_nonnegative(index, "index")
    max_steps = _require_nonnegative(max_steps, "max_steps")
    master_seed = _require_integer(master_seed, "master_seed")
    seat_assignment, starting_player = PARTITIONS[index % len(PARTITIONS)]
    return {
        "job_id": f"m4-{index:06d}",
        "seed": splitmix64((master_seed + index * GOLDEN) & MASK64),
        "seat_assignment": seat_assignment,
        "starting_player": starting_player,
        "max_steps": max_steps,
        "canonical_rules_id": RULES_BUNDLE_ID,
        "mode": "throughput",
        "observation_mode": "full",
        "instrumentation": False,
        "persist_trace": False,
    }


def derive_jobs(master_seed: int, job_count: int, max_steps: int = 2200) -> List[Dict[str, Any]]:
    """Derive a stable, index-ordered list of jobs.

    A zero count returns an empty list. Negative counts are rejected rather
    than silently producing a partial workload.
    """

    master_seed = _require_integer(master_seed, "master_seed")
    job_count = _require_nonnegative(job_count, "job_count")
    jobs = [derive_job(master_seed, index, max_steps) for index in range(job_count)]
    job_ids = [job["job_id"] for job in jobs]
    if len(job_ids) != len(set(job_ids)):
        raise AssertionError("deterministic job generation produced duplicate IDs")
    return jobs


def configure_job(
    job: Dict[str, Any],
    *,
    mode: str | None = None,
    observation_mode: str | None = None,
    instrumentation: bool | None = None,
    persist_trace: bool | None = None,
) -> Dict[str, Any]:
    """Return a job copy with only execution-policy fields changed."""

    configured = dict(job)
    selected_mode = configured.get("mode") if mode is None else mode
    selected_observation_mode = (
        configured.get("observation_mode")
        if observation_mode is None
        else observation_mode
    )
    _validate_execution_options(selected_mode, selected_observation_mode)
    if instrumentation is not None and not isinstance(instrumentation, bool):
        raise TypeError("instrumentation must be a boolean")
    if persist_trace is not None and not isinstance(persist_trace, bool):
        raise TypeError("persist_trace must be a boolean")

    if mode is not None:
        configured["mode"] = mode
    if observation_mode is not None:
        configured["observation_mode"] = observation_mode
    if instrumentation is not None:
        configured["instrumentation"] = instrumentation
    if persist_trace is not None:
        configured["persist_trace"] = persist_trace
    return configured


def derive_job_with_options(
    master_seed: int,
    index: int,
    max_steps: int = 2200,
    *,
    mode: str = "throughput",
    observation_mode: str = "full",
    instrumentation: bool = False,
    persist_trace: bool = False,
) -> Dict[str, Any]:
    """Derive one job and apply only the supported execution options."""

    return configure_job(
        derive_job(master_seed, index, max_steps),
        mode=mode,
        observation_mode=observation_mode,
        instrumentation=instrumentation,
        persist_trace=persist_trace,
    )
