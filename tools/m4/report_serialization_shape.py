"""Aggregate the M4.3.4 Release serialization-shape characterization.

This script consumes the deterministic run produced by
``compare_serialization_shape.py``.  It reports scalar telemetry only; the
per-observation records remain in the ignored run artifact so lifecycle IDs
can be audited without making the checked-in report needlessly large.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
from typing import Any, Iterable, Mapping


PHASES = [
    "preparation_copy",
    "sorting",
    "rendering",
    "escaping",
    "final_extraction",
]
SECTIONS = [
    "schema_basic_header",
    "globals",
    "zones",
    "entities",
    "relationships",
    "chain",
    "visible_events",
    "decision_context",
    "match_context",
]
COPY_KINDS = [
    "top_level_zones",
    "top_level_entities",
    "top_level_relationships",
    "top_level_visible_events",
    "chain_targets",
    "event_targets",
    "decision_references",
    "link_markers",
    "counters",
    "own_deck",
    "opponent_deck",
]
SORT_KINDS = [
    "zones",
    "entities",
    "relationships",
    "visible_events",
    "chain_targets",
    "event_targets",
    "decision_references",
    "link_markers",
    "counters",
    "decks",
]
AUDIT_TIMING_BUCKETS = [
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
]
LIFECYCLE_FIELDS = [
    "serialize_without_hash_calls",
    "serialize_without_hash_bytes",
    "sha256_calls",
    "canonical_serialize_calls",
    "canonical_serialize_bytes",
    "same_mutation_epoch_duplicate_calls",
]
GATE_NAMES = [
    "ctests",
    "repository_python_tests",
    "m3_python_tests",
    "m4_python_tests",
    "privacy_tests",
    "candidate_observation_consistency",
    "worker_count_semantic_gate",
]
IDENTITY_EXCLUDED_PATHS = {
    "docs/m4/M4_3_4_SERIALIZATION_SHAPE_AUDIT.md",
    "docs/m4/m4_3_4_serialization_shape_audit.json",
}


def _load(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _current_repository_identity() -> dict[str, Any] | None:
    repository_root = Path(__file__).resolve().parents[2]

    def git(*arguments: str) -> bytes:
        completed = subprocess.run(
            ["git", *arguments],
            cwd=repository_root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return completed.stdout

    try:
        raw_status_lines = git("status", "--porcelain=v1", "--untracked-files=all").decode(
            "utf-8", errors="strict"
        ).splitlines()
        status_lines = [
            line
            for line in raw_status_lines
            if line[3:].replace("\\", "/") not in IDENTITY_EXCLUDED_PATHS
        ]
        status = ("\n".join(status_lines) + ("\n" if status_lines else "")).encode("utf-8")
        tracked_diff = git("diff", "--binary", "HEAD", "--")
        untracked_files = []
        for raw_path in git("ls-files", "--others", "--exclude-standard").splitlines():
            relative_path = raw_path.decode("utf-8", errors="strict")
            if relative_path.replace("\\", "/") in IDENTITY_EXCLUDED_PATHS:
                continue
            path = repository_root / relative_path
            if path.is_file():
                untracked_files.append(
                    {
                        "path": relative_path,
                        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                        "bytes": path.stat().st_size,
                    }
                )
        return {
            "repository_root": str(repository_root.resolve()),
            "git_head": git("rev-parse", "HEAD").decode("ascii").strip(),
            "tracked_diff_sha256": hashlib.sha256(tracked_diff).hexdigest(),
            "status_sha256": hashlib.sha256(status).hexdigest(),
            "status_lines": status_lines,
            "untracked_files": untracked_files,
        }
    except (OSError, subprocess.CalledProcessError, UnicodeError):
        return None


def _sum(values: Iterable[Any]) -> int:
    return sum(int(value or 0) for value in values)


def _mean(total: int, calls: int) -> float:
    return round(total / calls, 6) if calls else 0.0


def _percent(value: int | float, total: int | float) -> float:
    return round((float(value) * 100.0 / float(total)), 6) if total else 0.0


def _nearest_rank(values: list[int], fraction: float) -> int | None:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, math.ceil(fraction * len(ordered)) - 1))
    return ordered[index]


def _correlation(rows: list[Mapping[str, Any]], left: str, right: str) -> float | None:
    pairs = [(float(row[left]), float(row[right])) for row in rows if row.get(left) is not None and row.get(right) is not None]
    if len(pairs) < 2:
        return None
    left_mean = sum(pair[0] for pair in pairs) / len(pairs)
    right_mean = sum(pair[1] for pair in pairs) / len(pairs)
    numerator = sum((x - left_mean) * (y - right_mean) for x, y in pairs)
    left_norm = math.sqrt(sum((x - left_mean) ** 2 for x, _ in pairs))
    right_norm = math.sqrt(sum((y - right_mean) ** 2 for _, y in pairs))
    if left_norm == 0 or right_norm == 0:
        return None
    return round(numerator / (left_norm * right_norm), 6)


def _aggregate_named_stats(
    sidecars: list[Mapping[str, Any]],
    key: str,
    names: list[str],
    fields: list[str],
) -> dict[str, dict[str, int | float]]:
    result: dict[str, dict[str, int | float]] = {}
    for name in names:
        values = [sidecar.get(key, {}).get(name, {}) for sidecar in sidecars]
        row: dict[str, int | float] = {}
        for field in fields:
            total = _sum(value.get(field, 0) for value in values)
            row[field] = total
        if "calls" in row and "total_us" in row:
            row["mean_us_per_call"] = _mean(int(row["total_us"]), int(row["calls"]))
        result[name] = row
    return result


def _aggregate_audit(sidecars: list[Mapping[str, Any]]) -> dict[str, Any]:
    timing: dict[str, dict[str, int | float]] = {}
    for name in AUDIT_TIMING_BUCKETS:
        rows = [sidecar.get("observation_timing_us", {}).get(name, {}) for sidecar in sidecars]
        calls = _sum(row.get("calls", 0) for row in rows)
        total_us = _sum(row.get("total_us", 0) for row in rows)
        timing[name] = {
            "calls": calls,
            "total_us": total_us,
            "mean_us_per_call": _mean(total_us, calls),
        }
    observation_total_us = _sum(sidecar.get("observation_total_us", 0) for sidecar in sidecars)
    counters: dict[str, int] = {}
    for name in [
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
    ]:
        counters[name] = _sum(sidecar.get("observation_counters", {}).get(name, 0) for sidecar in sidecars)
    return {
        "observation_total_us": observation_total_us,
        "observation_timing_us": timing,
        "counters": counters,
    }


def _aggregate_lifecycle(
    sidecars: list[Mapping[str, Any]],
    expected_shape_keys: set[tuple[str, int]] | None = None,
) -> dict[str, Any]:
    totals = {field: _sum(sidecar.get(field, 0) for sidecar in sidecars) for field in LIFECYCLE_FIELDS}
    records: list[tuple[str, Mapping[str, Any]]] = []
    for sidecar in sidecars:
        job_id = str(sidecar.get("job_id"))
        for record in sidecar.get("lifecycle_records", []):
            records.append((job_id, record))
    keys = [(job_id, int(record.get("lifecycle_id", 0))) for job_id, record in records]
    if any(lifecycle_id <= 0 for _, lifecycle_id in keys):
        raise ValueError("serialization lifecycle sidecar contains a missing or zero lifecycle ID")
    unique_keys = set(keys)
    if len(keys) != len(unique_keys):
        raise ValueError("duplicate job-scoped lifecycle IDs in lifecycle sidecars")
    if expected_shape_keys is not None and unique_keys != expected_shape_keys:
        raise ValueError("shape records and lifecycle records do not cover the same lifecycles")
    calls_ok = all(int(record.get("serialize_without_hash_calls", 0)) == 1 for _, record in records)
    hashes_ok = all(int(record.get("sha256_calls", 0)) == 1 for _, record in records)
    return {
        **totals,
        "lifecycle_records": len(records),
        "unique_job_lifecycle_ids": len(unique_keys),
        "duplicate_job_lifecycle_ids": len(keys) - len(unique_keys),
        "one_serialize_without_hash_per_lifecycle": calls_ok,
        "one_sha256_per_lifecycle": hashes_ok,
        "same_mutation_epoch_duplicate_calls_zero": totals["same_mutation_epoch_duplicate_calls"] == 0,
        "canonical_serialize_consumed_in_throughput": totals["canonical_serialize_calls"] != 0,
        "canonical_serialize_calls_zero_in_throughput": totals["canonical_serialize_calls"] == 0,
        "shape_and_lifecycle_keys_match": expected_shape_keys is None or unique_keys == expected_shape_keys,
    }


def _build_identity(
    worker: Path, compile_commands: Path | None, ready: Mapping[str, Any] | None
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "worker": str(worker.resolve()),
        "worker_sha256": hashlib.sha256(worker.read_bytes()).hexdigest(),
    }
    cache = worker.parent / "CMakeCache.txt"
    cache_values: dict[str, str] = {}
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.startswith("#") or ":" not in line or "=" not in line:
                continue
            left, value = line.split("=", 1)
            key = left.split(":", 1)[0]
            cache_values[key] = value
    for key in [
        "CMAKE_BUILD_TYPE",
        "CMAKE_CXX_COMPILER",
        "CMAKE_CXX_COMPILER_ID",
        "CMAKE_CXX_COMPILER_VERSION",
        "CMAKE_CXX_FLAGS",
        "CMAKE_CXX_FLAGS_RELEASE",
        "CMAKE_TOOLCHAIN_FILE",
    ]:
        if key in cache_values:
            result[key] = cache_values[key]
    if ready is not None:
        result["compiler_identity"] = ready.get("compiler_identity")
        result["build_type"] = ready.get("build_type")
        result["worker_ready_identity"] = {
            key: ready.get(key)
            for key in [
                "compiler_identity",
                "build_type",
                "core_patchset_sha256",
                "deck_hashes",
                "rules_bundle_id",
                "format_id",
                "duel_flags",
                "duel_mode_name",
                "protocol_version",
                "worker_identity",
            ]
        }
    if compile_commands is not None and compile_commands.exists():
        entries = _load(compile_commands)
        relevant: list[dict[str, Any]] = []
        for entry in entries:
            file_name = str(entry.get("file", ""))
            if file_name.endswith("src/observation/serialization.cpp") or file_name.endswith(
                "src\\observation\\serialization.cpp"
            ):
                relevant.append(
                    {
                        "file": file_name,
                        "command": entry.get("command"),
                        "arguments": entry.get("arguments"),
                    }
                )
        result["serialization_compile_commands"] = relevant
    return result


def _records_and_shape(run: Mapping[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    throughput = run["throughput_run"]
    sidecars = list(throughput["sidecars"])
    records: list[dict[str, Any]] = []
    for sidecar in sidecars:
        if not sidecar.get("lifecycle_context_complete", False):
            raise ValueError(f"missing lifecycle context in shape sidecar {sidecar.get('job_id')}")
        if not sidecar.get("visible_events", {}).get("records_complete", False):
            raise ValueError(f"incomplete serialization records in {sidecar.get('job_id')}")
        if not sidecar.get("visible_events", {}).get("unique_event_tracking_complete", False):
            raise ValueError(f"incomplete visible-event uniqueness tracking in {sidecar.get('job_id')}")
        for record in sidecar.get("records", []):
            item = dict(record)
            item["job_id"] = str(sidecar["job_id"])
            if int(item.get("lifecycle_id", 0)) <= 0:
                raise ValueError(f"missing lifecycle ID in shape record for {item['job_id']}")
            if "rendering_residual_clamped" not in item:
                raise ValueError(f"missing residual-underflow flag in shape record for {item['job_id']}")
            if bool(item["rendering_residual_clamped"]):
                raise ValueError(f"negative residual timing was clamped for {item['job_id']}")
            if sum(int(item["section_bytes"].get(name, 0)) for name in SECTIONS) != int(item["canonical_bytes"]):
                raise ValueError(f"section byte sum mismatch for {item['job_id']} lifecycle {item['lifecycle_id']}")
            phase_sum = sum(
                int(item[name])
                for name in [
                    "preparation_copy_us",
                    "sorting_us",
                    "rendering_us",
                    "escaping_us",
                    "final_extraction_us",
                ]
            )
            if phase_sum != int(item["total_us"]):
                raise ValueError(f"serialization phase sum mismatch for {item['job_id']} lifecycle {item['lifecycle_id']}")
            records.append(item)
    calls = _sum(sidecar.get("serialize_without_hash", {}).get("calls", 0) for sidecar in sidecars)
    bytes_total = _sum(sidecar.get("serialize_without_hash", {}).get("bytes", 0) for sidecar in sidecars)
    total_us = _sum(sidecar.get("serialize_without_hash", {}).get("total_us", 0) for sidecar in sidecars)
    residual_underflow_count = _sum(
        sidecar.get("serialize_without_hash", {}).get("residual_underflow_count", 0)
        for sidecar in sidecars
    )
    if any("residual_underflow_count" not in sidecar.get("serialize_without_hash", {}) for sidecar in sidecars):
        raise ValueError("missing residual-underflow count in shape sidecar")
    if residual_underflow_count != 0:
        raise ValueError("serialization residual timing underflow was observed")
    if len(records) != calls or sum(int(record["canonical_bytes"]) for record in records) != bytes_total:
        raise ValueError("shape record totals do not match shape sidecar totals")
    if sum(int(record["total_us"]) for record in records) != total_us:
        raise ValueError("shape record timing does not match shape sidecar totals")
    composite_ids = [(record["job_id"], int(record["lifecycle_id"])) for record in records]
    if len(composite_ids) != len(set(composite_ids)):
        raise ValueError("duplicate diagnostic lifecycle IDs in serialization shape records")
    phase_timing: dict[str, dict[str, int | float]] = {}
    for name in PHASES:
        rows = [sidecar.get("phase_timing_us", {}).get(name, {}) for sidecar in sidecars]
        phase_calls = _sum(row.get("calls", 0) for row in rows)
        phase_total = _sum(row.get("total_us", 0) for row in rows)
        phase_timing[name] = {
            "calls": phase_calls,
            "total_us": phase_total,
            "mean_us_per_call": _mean(phase_total, phase_calls),
            "fraction_of_serialize_percent": _percent(phase_total, total_us),
        }
    phase_sum_us = sum(int(row["total_us"]) for row in phase_timing.values())
    shape = {
        "serialize_without_hash": {
            "calls": calls,
            "bytes": bytes_total,
            "total_us": total_us,
            "mean_bytes_per_call": _mean(bytes_total, calls),
            "mean_us_per_call": _mean(total_us, calls),
            "residual_underflow_count": residual_underflow_count,
        },
        "phase_timing_us": phase_timing,
        "phase_sum_us": phase_sum_us,
        "phase_sum_matches_serialize_total": phase_sum_us == total_us,
        "copy_stats": _aggregate_named_stats(sidecars, "copy_stats", COPY_KINDS,
                                               ["calls", "elements", "approximate_bytes", "total_us"]),
        "sort_stats": _aggregate_named_stats(sidecars, "sort_stats", SORT_KINDS,
                                              ["calls", "elements", "total_us"]),
        "formatting": {
            name: _sum(sidecar.get("formatting", {}).get(name, 0) for sidecar in sidecars)
            for name in [
                "json_escape_calls",
                "json_escape_input_bytes",
                "json_escape_output_bytes",
                "numeric_values",
                "boolean_values",
                "null_values",
            ]
        },
        "visible_events": {
            "serialized_instances": _sum(
                sidecar.get("visible_events", {}).get("serialized_instances", 0) for sidecar in sidecars
            ),
            "unique_events_seen": _sum(
                sidecar.get("visible_events", {}).get("unique_events_seen", 0) for sidecar in sidecars
            ),
        },
        "lifecycle_id_proof": {
            "records": len(records),
            "unique_job_lifecycle_ids": len(set(composite_ids)),
            "duplicate_job_lifecycle_ids": 0,
            "first": {"job_id": records[0]["job_id"], "lifecycle_id": records[0]["lifecycle_id"]},
            "last": {"job_id": records[-1]["job_id"], "lifecycle_id": records[-1]["lifecycle_id"]},
        },
        "lifecycle_context_complete": all(
            bool(sidecar.get("lifecycle_context_complete", False)) for sidecar in sidecars
        ),
        "residual_underflow_count": residual_underflow_count,
    }
    return records, shape


def _size_report(records: list[Mapping[str, Any]]) -> dict[str, Any]:
    sizes = [int(record["canonical_bytes"]) for record in records]
    return {
        "minimum_bytes": min(sizes),
        "mean_bytes": round(sum(sizes) / len(sizes), 6),
        "median_bytes": _nearest_rank(sizes, 0.50),
        "p95_bytes": _nearest_rank(sizes, 0.95),
        "p99_bytes": _nearest_rank(sizes, 0.99),
        "maximum_bytes": max(sizes),
        "percentile_definition": "nearest rank; rank = ceil(p * n)",
    }


def _section_bytes(records: list[Mapping[str, Any]], total_bytes: int) -> dict[str, Any]:
    result = {}
    for name in SECTIONS:
        total = _sum(record["section_bytes"].get(name, 0) for record in records)
        result[name] = {"bytes": total, "percent_of_canonical_bytes": _percent(total, total_bytes)}
    return {
        "sections": result,
        "sum_bytes": sum(row["bytes"] for row in result.values()),
        "complete_sum_matches_canonical_bytes": sum(row["bytes"] for row in result.values()) == total_bytes,
        "delimiter_assignment": (
            "Each section span includes its leading comma/key and its emitted value; "
            "the schema/basic header starts at byte 0. The spans are contiguous and "
            "there is no unassigned delimiter remainder."
        ),
    }


def _visible_growth(records: list[dict[str, Any]], unique_total: int) -> dict[str, Any]:
    ordered = sorted(records, key=lambda row: (row["job_id"], int(row["lifecycle_id"])))
    counts = [int(record["visible_event_instances"]) for record in records]
    bytes_values = [int(record["visible_event_bytes"]) for record in records]
    per_job: list[dict[str, Any]] = []
    for job_id in sorted({record["job_id"] for record in records}):
        rows = [record for record in ordered if record["job_id"] == job_id]
        job_counts = [int(row["visible_event_instances"]) for row in rows]
        job_bytes = [int(row["visible_event_bytes"]) for row in rows]
        per_job.append(
            {
                "job_id": job_id,
                "observations": len(rows),
                "first": {"event_count": job_counts[0], "bytes": job_bytes[0]},
                "median_event_count": _nearest_rank(job_counts, 0.5),
                "p95_event_count": _nearest_rank(job_counts, 0.95),
                "final": {"event_count": job_counts[-1], "bytes": job_bytes[-1]},
                "maximum": {
                    "event_count": max(job_counts),
                    "bytes": max(job_bytes),
                },
            }
        )
    instances = sum(counts)
    return {
        "cumulative_history_observed": True,
        "workload_first": {
            "job_id": ordered[0]["job_id"],
            "event_count": int(ordered[0]["visible_event_instances"]),
            "bytes": int(ordered[0]["visible_event_bytes"]),
        },
        "workload_median_event_count": _nearest_rank(counts, 0.5),
        "workload_median_bytes": _nearest_rank(bytes_values, 0.5),
        "workload_p95_event_count": _nearest_rank(counts, 0.95),
        "workload_p95_bytes": _nearest_rank(bytes_values, 0.95),
        "workload_final": {
            "job_id": ordered[-1]["job_id"],
            "event_count": int(ordered[-1]["visible_event_instances"]),
            "bytes": int(ordered[-1]["visible_event_bytes"]),
        },
        "workload_maximum": {"event_count": max(counts), "bytes": max(bytes_values)},
        "serialized_event_instances": instances,
        "unique_visible_event_identities_seen": unique_total,
        "repetition_factor_lower_bound": round(instances / unique_total, 6) if unique_total else None,
        "identity_scope": "per-job diagnostic proxy tuple (perspective, event_index, engine_step_index, kind)",
        "identity_is_durable_canonical": False,
        "tuple_collisions_can_undercount_unique_events": True,
        "same_historical_event_can_appear_in_later_observations": instances > unique_total,
        "generation_identity_basis": (
            "The canonical simulation constructs one ObservationSession per perspective and never calls clear(); "
            "ObservationSession::ingest assigns monotonically increasing event_index values. The uniqueness set is "
            "therefore a per-job diagnostic proxy for generated-event identity in this workload, not a durable or "
            "canonical event ID. ObservationSession::clear resets that index, and tuple collisions could undercount "
            "unique events; both are outside or conservative caveats for this controlled workload."
        ),
        "per_job": per_job,
    }


def _entity_report(records: list[Mapping[str, Any]]) -> dict[str, Any]:
    totals = {
        name: _sum(record[name] for record in records)
        for name in [
            "entity_instances",
            "entity_bytes",
            "printed_property_objects",
            "printed_property_bytes",
            "current_property_objects",
            "current_property_bytes",
            "counter_instances",
            "counter_bytes",
            "link_marker_instances",
            "link_marker_bytes",
        ]
    }
    return {
        **totals,
        "mean_bytes_per_entity": _mean(totals["entity_bytes"], totals["entity_instances"]),
        "printed_and_current_property_bytes": totals["printed_property_bytes"] + totals["current_property_bytes"],
        "printed_current_bytes_fraction_of_entity_bytes": _percent(
            totals["printed_property_bytes"] + totals["current_property_bytes"], totals["entity_bytes"]
        ),
        "printed_property_bytes_fraction_of_entity_bytes": _percent(
            totals["printed_property_bytes"], totals["entity_bytes"]
        ),
        "current_property_bytes_fraction_of_entity_bytes": _percent(
            totals["current_property_bytes"], totals["entity_bytes"]
        ),
    }


def _static_context_report(records: list[Mapping[str, Any]], total_bytes: int) -> dict[str, Any]:
    totals = {
        name: _sum(record[name] for record in records)
        for name in ["own_deck_bytes", "opponent_deck_bytes", "other_match_context_bytes"]
    }
    return {
        **totals,
        "total_static_match_context_bytes": sum(totals.values()),
        "total_static_match_context_percent_of_canonical_bytes": _percent(sum(totals.values()), total_bytes),
        "mean_bytes_per_observation": {
            name: round(value / len(records), 6) for name, value in totals.items()
        },
        "cumulative_bytes_over_workload": sum(totals.values()),
    }


def _copy_sort_report(shape: Mapping[str, Any], total_us: int) -> dict[str, Any]:
    copy_stats = shape["copy_stats"]
    sort_stats = shape["sort_stats"]
    copy_total = sum(int(row["total_us"]) for row in copy_stats.values())
    sort_total = sum(int(row["total_us"]) for row in sort_stats.values())
    return {
        "copy_stats": copy_stats,
        "sort_stats": sort_stats,
        "copy_total_us": copy_total,
        "sort_total_us": sort_total,
        "copy_fraction_of_serialize_percent": _percent(copy_total, total_us),
        "sort_fraction_of_serialize_percent": _percent(sort_total, total_us),
        "copy_sort_time_matches_phase_totals": (
            copy_total == int(shape["phase_timing_us"]["preparation_copy"]["total_us"])
            and sort_total == int(shape["phase_timing_us"]["sorting"]["total_us"])
        ),
    }


def _workload_runtime(run: Mapping[str, Any]) -> dict[str, Any]:
    jobs = list(run["throughput_run"]["jobs"])
    runtime_us = _sum(job.get("simulation_elapsed_us", 0) for job in jobs)
    games = len(jobs)
    return {
        "games": games,
        "worker_local_simulation_runtime_us": runtime_us,
        "worker_local_simulation_runtime_seconds": round(runtime_us / 1_000_000, 6),
        "games_per_second": round(games / (runtime_us / 1_000_000), 9) if runtime_us else 0.0,
        "max_steps": run["workload"]["max_steps"],
        "worker_count": run["workload"]["workers"],
    }


def _reference_comparison(
    run: Mapping[str, Any],
    reference_path: Path | None,
    shape: Mapping[str, Any],
    control_path: Path | None,
    control_worker: Path | None,
) -> dict[str, Any]:
    current_audit = _aggregate_audit(list(run["throughput_run"].get("performance_sidecars", [])))
    current_runtime = _workload_runtime(run)
    result: dict[str, Any] = {
        "current_shape_worker_audit": current_audit,
        "current_runtime": current_runtime,
        "reference_available": reference_path is not None and reference_path.exists() if reference_path else False,
        "instrumentation_overhead_check": {
            "status": "NOT_MEASURED",
            "method": "Compare the same Release 16-game workload against the frozen M4.3.3 Release audit. This is a workload-level overhead signal, not an isolated microbenchmark.",
        },
    }
    if reference_path is None or not reference_path.exists():
        reference_path = None

    current_serialization = int(current_audit["observation_timing_us"]["observation_canonical_serialization"]["total_us"])
    current_observation = int(current_audit["observation_total_us"])

    def compare_reference(path: Path) -> dict[str, Any]:
        reference = _load(path)
        full = reference["samples"]["full"]
        reference_timing = full["timing_us"]
        reference_runtime = full["runtime"]
        reference_serialization = int(
            reference_timing["observation"]["observation_canonical_serialization"]["total_us"]
        )
        reference_observation = int(reference_timing["outer_observation"]["total_us"])
        reference_worker_runtime = int(reference_runtime["worker_local_simulation_elapsed_total_us"])
        return {
            "audit_path": str(path.resolve()),
            "worker_local_simulation_runtime_us": reference_worker_runtime,
            "games_per_second": round(full["primary"]["games_completed"] / (reference_worker_runtime / 1_000_000), 9),
            "observation_canonical_serialization_us": reference_serialization,
            "outer_observation_us": reference_observation,
        }

    if reference_path is not None:
        result["frozen_m4_3_3_release_reference"] = compare_reference(reference_path)

    if control_path is not None and control_path.exists():
        control = compare_reference(control_path)
        result["same_head_release_no_shape_control"] = control
        if control_worker is not None and control_worker.exists():
            result["same_head_release_no_shape_control"]["worker_identity"] = {
                "worker": str(control_worker.resolve()),
                "worker_sha256": hashlib.sha256(control_worker.read_bytes()).hexdigest(),
            }
        control_serialization = control["observation_canonical_serialization_us"]
        control_observation = control["outer_observation_us"]
        control_runtime = control["worker_local_simulation_runtime_us"]
        result["instrumentation_overhead_check"] = {
            "status": "MEASURED_SAME_HEAD_CONTROL",
            "method": "Compare the shape-enabled Release worker against a same-source, same-toolchain Release control compiled with YGO_M4_PERFORMANCE_AUDIT but without YGO_M4_SERIALIZATION_SHAPE_AUDIT. This is a workload-level overhead signal, not an isolated microbenchmark.",
            "shape_worker_vs_no_shape_control": {
                "observation_canonical_serialization_us": {
                    "shape_worker": current_serialization,
                    "no_shape_control": control_serialization,
                    "delta_us": current_serialization - control_serialization,
                    "delta_percent": _percent(current_serialization - control_serialization, control_serialization),
                },
                "outer_observation_us": {
                    "shape_worker": current_observation,
                    "no_shape_control": control_observation,
                    "delta_us": current_observation - control_observation,
                    "delta_percent": _percent(current_observation - control_observation, control_observation),
                },
                "worker_local_runtime_us": {
                    "shape_worker": current_runtime["worker_local_simulation_runtime_us"],
                    "no_shape_control": control_runtime,
                    "delta_us": current_runtime["worker_local_simulation_runtime_us"] - control_runtime,
                    "delta_percent": _percent(
                        current_runtime["worker_local_simulation_runtime_us"] - control_runtime, control_runtime
                    ),
                },
            },
            "shape_serializer_internal_total_us": int(shape["serialize_without_hash"]["total_us"]),
            "interpretation": "The delta measures the effect of the optional shape probe plus run-to-run machine variation. It is diagnostic evidence only and is not a throughput claim or an optimization authorization.",
        }
    elif reference_path is not None and reference_path.exists():
        frozen = result["frozen_m4_3_3_release_reference"]
        result["instrumentation_overhead_check"] = {
            "status": "MEASURED_WORKLOAD_DELTA",
            "method": result["instrumentation_overhead_check"]["method"],
            "shape_worker_vs_frozen_release": {
                "observation_canonical_serialization_us": {
                    "shape_worker": current_serialization,
                    "frozen_release": frozen["observation_canonical_serialization_us"],
                    "delta_us": current_serialization - frozen["observation_canonical_serialization_us"],
                    "delta_percent": _percent(current_serialization - frozen["observation_canonical_serialization_us"], frozen["observation_canonical_serialization_us"]),
                },
                "outer_observation_us": {
                    "shape_worker": current_observation,
                    "frozen_release": frozen["outer_observation_us"],
                    "delta_us": current_observation - frozen["outer_observation_us"],
                    "delta_percent": _percent(current_observation - frozen["outer_observation_us"], frozen["outer_observation_us"]),
                },
                "worker_local_runtime_us": {
                    "shape_worker": current_runtime["worker_local_simulation_runtime_us"],
                    "frozen_release": frozen["worker_local_simulation_runtime_us"],
                    "delta_us": current_runtime["worker_local_simulation_runtime_us"] - frozen["worker_local_simulation_runtime_us"],
                    "delta_percent": _percent(current_runtime["worker_local_simulation_runtime_us"] - frozen["worker_local_simulation_runtime_us"], frozen["worker_local_simulation_runtime_us"]),
                },
            },
            "shape_serializer_internal_total_us": int(shape["serialize_without_hash"]["total_us"]),
            "interpretation": "The delta is evidence that the diagnostic probe has a measurable workload effect or that the machine run varied. It is not used as a throughput claim and does not authorize optimization.",
        }
    return result


def _candidate_report(
    shape: Mapping[str, Any], sections: Mapping[str, Any], visible: Mapping[str, Any], total_bytes: int
) -> tuple[list[dict[str, Any]], list[str], dict[str, Any]]:
    total_us = int(shape["serialize_without_hash"]["total_us"])
    phase = shape["phase_timing_us"]
    section = sections["sections"]
    visible_fraction = float(section["visible_events"]["percent_of_canonical_bytes"]) / 100.0
    static_bytes = section["match_context"]["bytes"]
    classes: list[str] = []
    if float(phase["rendering"]["fraction_of_serialize_percent"]) >= 50:
        classes.append("RENDERING_DOMINANT")
    if float(phase["preparation_copy"]["fraction_of_serialize_percent"]) >= 25:
        classes.append("COPY_DOMINANT")
    if float(phase["sorting"]["fraction_of_serialize_percent"]) >= 25:
        classes.append("SORT_DOMINANT")
    if float(phase["escaping"]["fraction_of_serialize_percent"]) >= 25:
        classes.append("ESCAPING_DOMINANT")
    if (
        visible_fraction >= 50
        and visible["repetition_factor_lower_bound"]
        and visible["repetition_factor_lower_bound"] > 1
    ):
        classes.append("HISTORY_GROWTH_DOMINANT")
    if _percent(static_bytes, total_bytes) >= 10:
        classes.append("STATIC_CONTEXT_REPETITION_MATERIAL")
    if not classes:
        classes.append("MIXED")

    candidates = [
        {
            "name": "rendering_output_stream",
            "measured_runtime_fraction_percent": phase["rendering"]["fraction_of_serialize_percent"],
            "measured_bytes_fraction_percent": 100.0,
            "evidence": {
                "rendering_us": phase["rendering"]["total_us"],
                "rendering_calls": phase["rendering"]["calls"],
                "serialize_total_us": total_us,
                "measurement_kind": "residual_partition_after_copy_sort_escape_extraction",
                "unmeasured_residual_components": "remaining stream formatting, primitive formatting, output construction, and probe overhead",
            },
            "suspected_redundant_work": "std::ostringstream performs much of the remaining stream formatting and output construction for every observation; this phase is a residual partition, not an isolated ostringstream timer.",
            "proposed_optimization_concept": "A/B test a reserve-backed append implementation while preserving the exact existing field order and escaping.",
            "semantic_privacy_risk": "High byte-contract risk; any delimiter, number, escape, ordering, or redaction drift is unacceptable.",
            "required_equivalence_test": "All 9,908 canonical-without-hash byte strings and observation hashes, trace hashes, privacy fixtures, and candidate consistency must match.",
            "expected_affected_bucket": "observation_canonical_serialization / observation_other",
            "implementation_complexity": "HIGH",
            "time_measurement_direct": False,
            "measurement_kind": "residual_partition",
            "measurement_kind": "residual_partition",
        },
        {
            "name": "visible_event_history_growth",
            "measured_runtime_fraction_percent": None,
            "measured_bytes_fraction_percent": section["visible_events"]["percent_of_canonical_bytes"],
            "evidence": {
                "visible_event_bytes": section["visible_events"]["bytes"],
                "visible_event_bytes_percent": section["visible_events"]["percent_of_canonical_bytes"],
                "serialized_event_instances": visible["serialized_event_instances"],
                "unique_visible_event_identities_seen": visible["unique_visible_event_identities_seen"],
                "repetition_factor_lower_bound": visible["repetition_factor_lower_bound"],
            },
            "suspected_redundant_work": "Historical visible events are copied and rendered again in later cumulative observations.",
            "proposed_optimization_concept": "First isolate event-history representation or serialization cost in a byte-preserving experiment; do not use deltas or truncation in M4.3.4.",
            "semantic_privacy_risk": "Very high; event history order, perspective filtering, and privacy visibility are contract data.",
            "required_equivalence_test": "Per-observation visible-event arrays, canonical bytes, hashes, paired-world privacy, and trace equivalence.",
            "expected_affected_bucket": "observation_canonical_serialization / observation_visibility_privacy",
            "implementation_complexity": "HIGH",
            "time_measurement_direct": False,
        },
        {
            "name": "entity_property_representation",
            "measured_runtime_fraction_percent": None,
            "measured_bytes_fraction_percent": section["entities"]["percent_of_canonical_bytes"],
            "evidence": {
                "entity_bytes": section["entities"]["bytes"],
                "entity_bytes_percent": section["entities"]["percent_of_canonical_bytes"],
                "printed_current_property_bytes": "see entity audit",
            },
            "suspected_redundant_work": "Printed and current property objects are reconstructed and rendered for each projected entity.",
            "proposed_optimization_concept": "Measure a representation-level separation of immutable printed metadata before proposing reuse.",
            "semantic_privacy_risk": "High; redaction and current-versus-printed semantics must remain identical.",
            "required_equivalence_test": "Per-entity canonical bytes, identity-known/redacted fixtures, hashes, and privacy tests.",
            "expected_affected_bucket": "observation_entity_projection / observation_canonical_serialization",
            "implementation_complexity": "MEDIUM",
            "time_measurement_direct": False,
        },
        {
            "name": "static_match_context_repetition",
            "measured_runtime_fraction_percent": None,
            "measured_bytes_fraction_percent": section["match_context"]["percent_of_canonical_bytes"],
            "evidence": {
                "match_context_bytes": static_bytes,
                "match_context_bytes_percent": section["match_context"]["percent_of_canonical_bytes"],
            },
            "suspected_redundant_work": "Immutable deck and match-context fields are emitted in every observation.",
            "proposed_optimization_concept": "Evaluate model-facing separation only after a public observation-contract design; do not remove fields here.",
            "semantic_privacy_risk": "High; deck knowledge and public/private context are part of the observation contract.",
            "required_equivalence_test": "Exact final observations for both perspectives and all deck-knowledge/privacy fixtures.",
            "expected_affected_bucket": "observation_canonical_serialization",
            "implementation_complexity": "MEDIUM",
            "time_measurement_direct": False,
        },
        {
            "name": "copy_and_sort_preparation",
            "measured_runtime_fraction_percent": round(
                float(phase["preparation_copy"]["fraction_of_serialize_percent"])
                + float(phase["sorting"]["fraction_of_serialize_percent"]),
                6,
            ),
            "measured_bytes_fraction_percent": None,
            "evidence": {
                "copy_us": phase["preparation_copy"]["total_us"],
                "sort_us": phase["sorting"]["total_us"],
                "copy_and_sort_percent": round(
                    float(phase["preparation_copy"]["fraction_of_serialize_percent"])
                    + float(phase["sorting"]["fraction_of_serialize_percent"]),
                    6,
                ),
            },
            "suspected_redundant_work": "Multiple vectors are copied and sorted before every rendering pass.",
            "proposed_optimization_concept": "A later experiment could compare sorted views or indexes, but only after proving byte and stability equivalence.",
            "semantic_privacy_risk": "Medium-high; order is canonical and in-place mutation is forbidden without proof.",
            "required_equivalence_test": "Exact canonical bytes/hashes, stable ordering, privacy, and trace equivalence.",
            "expected_affected_bucket": "observation_canonical_serialization",
            "implementation_complexity": "MEDIUM",
            "time_measurement_direct": True,
        },
        {
            "name": "json_escaping",
            "measured_runtime_fraction_percent": phase["escaping"]["fraction_of_serialize_percent"],
            "measured_bytes_fraction_percent": None,
            "evidence": {
                "escaping_us": phase["escaping"]["total_us"],
                "json_escape_calls": shape["formatting"]["json_escape_calls"],
                "input_bytes": shape["formatting"]["json_escape_input_bytes"],
                "output_bytes": shape["formatting"]["json_escape_output_bytes"],
            },
            "suspected_redundant_work": "String values are escaped into temporary ostringstream instances during every observation.",
            "proposed_optimization_concept": "Characterize a narrowly scoped escaping implementation only after output-construction results; no rewrite in this audit.",
            "semantic_privacy_risk": "High; escaping changes canonical bytes and can expose contract drift.",
            "required_equivalence_test": "Escaping fixtures plus all final canonical byte/hash/privacy gates.",
            "expected_affected_bucket": "observation_canonical_serialization",
            "implementation_complexity": "MEDIUM",
            "time_measurement_direct": True,
        },
    ]
    # Measured Release runtime fraction is the primary ordering key. Byte-only
    # candidates are ordered after measured-time candidates by emitted bytes.
    candidates.sort(
        key=lambda candidate: (
            0 if candidate["measured_runtime_fraction_percent"] is not None else 1,
            -float(candidate["measured_runtime_fraction_percent"] or 0.0),
            -float(candidate["measured_bytes_fraction_percent"] or 0.0),
        )
    )
    for index, candidate in enumerate(candidates, start=1):
        candidate["rank"] = index
    return candidates, classes, {
        "first_optimization_experiment": {
            "candidate": candidates[0]["name"],
            "concept": "Isolated A/B output-construction experiment targeting the rendering residual, with a reserve-backed builder evaluated only against exact byte/hash/privacy equivalence.",
            "reason": "Rendering is the largest measured residual Release serialization partition; copy, sorting, and escaping are much smaller in this workload.",
            "scope_boundary": "Recommendation only. No serializer, event history, static context, hash, ocgcore, or M5 change is implemented by M4.3.4.",
        }
    }


def _parse_gates(values: list[str]) -> dict[str, str]:
    gates = {name: "NOT_RUN" for name in GATE_NAMES}
    for value in values:
        if "=" not in value:
            raise ValueError(f"gate must be NAME=STATUS: {value}")
        name, status = value.split("=", 1)
        if name not in gates:
            raise ValueError(f"unknown gate: {name}")
        if status not in {"PASS", "FAIL", "NOT_RUN", "SKIPPED", "BLOCKED"}:
            raise ValueError(f"invalid gate status: {status}")
        gates[name] = status
    return gates


def _parse_gate_evidence(values: list[str]) -> dict[str, str]:
    evidence: dict[str, str] = {}
    for value in values:
        if "=" not in value:
            raise ValueError(f"gate evidence must be NAME=TEXT: {value}")
        name, text = value.split("=", 1)
        if name not in GATE_NAMES:
            raise ValueError(f"unknown gate evidence name: {name}")
        if not text.strip():
            raise ValueError(f"empty gate evidence for: {name}")
        if name in evidence:
            raise ValueError(f"duplicate gate evidence: {name}")
        evidence[name] = text
    return evidence


def _validate_gate_manifest(
    path: Path | None,
    gate_statuses: Mapping[str, str],
) -> tuple[bool, dict[str, Any]]:
    if path is None or not path.exists():
        return False, {"status": "MISSING", "manifest_path": str(path) if path else None}
    manifest = _load(path)
    if manifest.get("schema") != "ocgforge.m4.m4_3_4_gate_evidence.v1":
        return False, {"status": "INVALID_SCHEMA", "manifest_path": str(path.resolve())}
    records = manifest.get("gates")
    if not isinstance(records, dict):
        return False, {"status": "INVALID_RECORDS", "manifest_path": str(path.resolve())}
    evidence: dict[str, Any] = {}
    valid = bool(manifest.get("all_exit_codes_zero"))
    for name in GATE_NAMES:
        if gate_statuses.get(name) != "PASS":
            continue
        record = records.get(name)
        if not isinstance(record, dict):
            valid = False
            continue
        stdout_path = Path(str(record.get("stdout_path", "")))
        stderr_path = Path(str(record.get("stderr_path", "")))
        paths_valid = stdout_path.is_file() and stderr_path.is_file()
        hashes_valid = paths_valid and (
            hashlib.sha256(stdout_path.read_bytes()).hexdigest() == record.get("stdout_sha256")
            and hashlib.sha256(stderr_path.read_bytes()).hexdigest() == record.get("stderr_sha256")
            and stdout_path.stat().st_size == int(record.get("stdout_bytes", -1))
            and stderr_path.stat().st_size == int(record.get("stderr_bytes", -1))
        )
        record_valid = (
            paths_valid
            and hashes_valid
            and record.get("returncode") == 0
            and isinstance(record.get("command"), list)
            and all(isinstance(argument, str) for argument in record["command"])
        )
        valid = valid and record_valid
        evidence[name] = record
    return valid, {
        "status": "PASS" if valid else "INVALID",
        "manifest_path": str(path.resolve()),
        "manifest_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "evidence": evidence,
    }


def build_report(
    run: Mapping[str, Any],
    *,
    run_path: Path,
    worker: Path,
    reference_audit: Path | None,
    compile_commands: Path | None,
    control_audit: Path | None,
    control_worker: Path | None,
    gates: dict[str, str],
    gate_evidence: dict[str, str],
    gate_manifest: Path | None,
) -> dict[str, Any]:
    records, shape = _records_and_shape(run)
    total_bytes = int(shape["serialize_without_hash"]["bytes"])
    sections = _section_bytes(records, total_bytes)
    visible = _visible_growth(records, int(shape["visible_events"]["unique_events_seen"]))
    entity = _entity_report(records)
    static_context = _static_context_report(records, total_bytes)
    copy_sort = _copy_sort_report(shape, int(shape["serialize_without_hash"]["total_us"]))
    candidates, classifications, recommendation = _candidate_report(shape, sections, visible, total_bytes)
    shape_lifecycle_keys = {
        (str(record["job_id"]), int(record["lifecycle_id"])) for record in records
    }
    lifecycle = _aggregate_lifecycle(
        list(run["throughput_run"].get("lifecycle_sidecars", [])),
        expected_shape_keys=shape_lifecycle_keys,
    )
    lifecycle["shape_and_lifecycle_calls_match"] = lifecycle["serialize_without_hash_calls"] == shape["serialize_without_hash"]["calls"]
    lifecycle["shape_and_lifecycle_bytes_match"] = lifecycle["serialize_without_hash_bytes"] == total_bytes
    lifecycle["sha256_calls_match_observations"] = lifecycle["sha256_calls"] == len(records)
    lifecycle["canonical_serialize_calls_zero_in_throughput"] = lifecycle["canonical_serialize_calls"] == 0
    audit = _aggregate_audit(list(run["throughput_run"].get("performance_sidecars", [])))
    runtime = _workload_runtime(run)
    throughput_execution_identity = run.get("throughput_run", {}).get("execution_identity", {})
    semantic_execution_identity = run.get("semantic_run", {}).get("execution_identity", {})
    measured_worker_sha256 = run.get("throughput_run", {}).get("worker_sha256")
    reported_worker_sha256 = hashlib.sha256(worker.read_bytes()).hexdigest()
    captured_source_identity = throughput_execution_identity.get("source")
    current_source_identity = _current_repository_identity()
    source_identity_match = (
        current_source_identity is not None
        and captured_source_identity == current_source_identity
    )
    run_binding_ok = (
        bool(measured_worker_sha256)
        and measured_worker_sha256 == reported_worker_sha256
        and throughput_execution_identity == semantic_execution_identity
        and bool((captured_source_identity or {}).get("git_head"))
        and "tracked_diff_sha256" in (captured_source_identity or {})
        and source_identity_match
    )
    correlations = {
        "canonical_bytes_vs_decision_index": _correlation(records, "canonical_bytes", "decision_index"),
        "canonical_bytes_vs_engine_step_index": _correlation(records, "canonical_bytes", "engine_step_index"),
        "canonical_bytes_vs_entity_count": _correlation(records, "canonical_bytes", "entity_instances"),
        "canonical_bytes_vs_visible_event_count": _correlation(records, "canonical_bytes", "visible_event_instances"),
        "canonical_bytes_vs_chain_length": _correlation(records, "canonical_bytes", "chain_length"),
        "interpretation": "Correlations are descriptive only and do not establish causation.",
    }
    reference = _reference_comparison(run, reference_audit, shape, control_audit, control_worker)
    semantic = run.get("semantic_equivalence", {})
    focused_equivalence = run.get("focused_shape_equivalence", {})
    gate_report = {
        "semantic_trace_equivalence": "PASS" if semantic.get("pass") else "FAIL",
        "focused_shape_equivalence": "PASS" if focused_equivalence.get("pass") else "FAIL",
        "measured_run_binary_identity": "PASS" if run_binding_ok else "FAIL",
        "canonical_section_byte_integrity": "PASS" if sections["complete_sum_matches_canonical_bytes"] else "FAIL",
        "residual_timing_integrity": "PASS" if shape["residual_underflow_count"] == 0 else "FAIL",
        "shape_record_completeness": "PASS"
        if shape.get("lifecycle_context_complete") and lifecycle.get("shape_and_lifecycle_keys_match")
        else "FAIL",
        **gates,
    }
    manifest_valid, manifest_provenance = _validate_gate_manifest(gate_manifest, gates)
    if gate_manifest is not None:
        gate_evidence_complete = manifest_valid
    else:
        gate_evidence_complete = all(
            gate_report[name] != "PASS" or bool(gate_evidence.get(name)) for name in GATE_NAMES
        )
    gate_provenance = {
        "mode": (
            "PASS statuses are bound to an immutable gate-evidence manifest with exit codes and stdout/stderr hashes; "
            "internal gates are derived from the run and report invariants."
            if gate_manifest is not None
            else "PASS statuses require explicit verification evidence supplied to the report generator; internal gates are derived from the run and report invariants."
        ),
        "evidence_complete": gate_evidence_complete,
        "evidence": {
            **{
                "semantic_trace_equivalence": "derived from frozen-reference trace, observation-hash, gameplay-hash, and step equality",
                "focused_shape_equivalence": "derived from six-fixture shape/no-shape byte/hash test marker",
                "measured_run_binary_identity": "derived from run-time worker SHA-256 and source identity binding",
                "canonical_section_byte_integrity": "derived from exact emitted section-span sum",
                "shape_record_completeness": "derived from sidecar completeness and shape/lifecycle key join",
                "residual_timing_integrity": "derived from explicit per-record residual-underflow flags and zero aggregate underflows",
                "gate_evidence_complete": "derived from non-empty evidence for every externally declared PASS gate",
            },
            **gate_evidence,
        },
    }
    if gate_manifest is not None:
        gate_provenance["manifest"] = manifest_provenance
        gate_provenance["evidence"].update(manifest_provenance.get("evidence", {}))
    gate_report["gate_evidence_complete"] = "PASS" if gate_evidence_complete else "FAIL"
    all_required_pass = all(gate_report[name] == "PASS" for name in gate_report)
    status = "M4.3.4 SERIALIZATION CHARACTERIZATION PASS" if all_required_pass else "M4.3.4 SERIALIZATION CHARACTERIZATION INCOMPLETE"
    return {
        "schema": "ocgforge.m4.m4_3_4_serialization_shape_audit.v1",
        "status": status,
        "scope": {
            "optimization_implemented": False,
            "m5_started": False,
            "ocgcore_modified": False,
            "card_scripts_modified": False,
            "canonical_format_changed": False,
            "hash_algorithm_changed": False,
            "event_history_semantics_changed": False,
            "static_match_context_removed": False,
        },
        "workload": run["workload"],
        "build_identity": _build_identity(
            worker,
            compile_commands,
            run.get("throughput_run", {}).get("ready"),
        ),
        "measured_run_identity": {
            "source": run.get("throughput_run", {}).get("execution_identity", {}).get("source"),
            "current_source": current_source_identity,
            "source_matches_current": source_identity_match,
            "worker": run.get("throughput_run", {}).get("execution_identity", {}).get("worker"),
            "throughput_worker_sha256": run.get("throughput_run", {}).get("worker_sha256"),
            "reported_worker_matches_measured_run": run_binding_ok,
        },
        "semantic_equivalence": semantic,
        "focused_shape_equivalence": focused_equivalence,
        "integrity": {
            "observation_count": len(records),
            "canonical_bytes": total_bytes,
            "sections_sum_exact": sections["complete_sum_matches_canonical_bytes"],
            "lifecycle": lifecycle,
            "trace_reference_artifact": run.get("semantic_equivalence", {}).get("reference_trace_dir"),
        },
        "serialization": shape,
        "canonical_byte_composition": sections,
        "size_distribution": _size_report(records),
        "size_correlations": correlations,
        "visible_event_growth": visible,
        "entity_serialization": entity,
        "static_match_context": static_context,
        "copy_boundary": copy_sort,
        "runtime": runtime,
        "performance_audit": audit,
        "m4_3_3_comparison_and_instrumentation_overhead": reference,
        "classifications": classifications,
        "optimization_candidates": candidates,
        "recommendation": recommendation,
        "gates": gate_report,
        "gate_provenance": gate_provenance,
        "raw_artifact": {
            "shape_workload": str(run_path.resolve()),
            "sidecar_record_policy": "Checked-in report contains aggregate scalars and lifecycle proof; raw per-observation shape records remain in the ignored workload artifact.",
        },
    }


def _fmt(value: Any) -> str:
    if isinstance(value, float):
        return f"{value:,.6f}"
    if isinstance(value, int):
        return f"{value:,}"
    return str(value)


def render_markdown(report: Mapping[str, Any]) -> str:
    shape = report["serialization"]
    phases = shape["phase_timing_us"]
    sections = report["canonical_byte_composition"]["sections"]
    entity = report["entity_serialization"]
    visible = report["visible_event_growth"]
    copy_boundary = report["copy_boundary"]
    runtime = report["runtime"]
    comparison = report["m4_3_3_comparison_and_instrumentation_overhead"]
    lines = [
        "# M4.3.4 — Canonical Serialization Shape, Copy, and Growth Audit",
        "",
        f"**Status:** `{report['status']}`  ",
        "",
        "This is a Release-build characterization only. No optimization, format change, event-history change, static-context removal, hash change, ocgcore change, or M5 work was implemented.",
        "",
        "## Workload and identity",
        "",
        f"- Matchup: `{report['workload']['matchup']}`; master seed `{report['workload']['master_seed']}`; games `{report['workload']['games']}`; one worker; max steps `{report['workload']['max_steps']}`.",
        f"- Observation mode: `{report['workload']['observation_mode']}`; throughput mode; trace persistence off.",
        f"- Compiler/build: `{report['build_identity'].get('compiler_identity', report['build_identity'].get('CMAKE_CXX_COMPILER_ID', 'NOT_RECORDED'))}` / `{report['build_identity'].get('CMAKE_BUILD_TYPE', 'NOT_RECORDED')}`; worker SHA-256 `{report['build_identity']['worker_sha256']}`.",
        f"- Measured-run binding: source HEAD `{report['measured_run_identity'].get('source', {}).get('git_head', 'NOT_RECORDED')}`; measured worker SHA-256 `{report['measured_run_identity'].get('throughput_worker_sha256', 'NOT_RECORDED')}`; matches reported worker `{report['measured_run_identity'].get('reported_worker_matches_measured_run')}`.",
        f"- Observations: `{_fmt(report['integrity']['observation_count'])}`; canonical-without-hash bytes: `{_fmt(report['integrity']['canonical_bytes'])}`.",
        "",
        "## Lifecycle and byte integrity",
        "",
        f"The shape records contain `{_fmt(report['integrity']['lifecycle']['lifecycle_records'])}` unique job-scoped lifecycle IDs, with `{_fmt(report['integrity']['lifecycle']['duplicate_job_lifecycle_ids'])}` duplicate IDs. Each lifecycle has one serialization and one SHA-256 call; same-mutation-epoch duplicate calls are `{_fmt(report['integrity']['lifecycle']['same_mutation_epoch_duplicate_calls'])}`.",
        f"`serialize_without_hash` calls/bytes: `{_fmt(report['integrity']['lifecycle']['serialize_without_hash_calls'])}` / `{_fmt(report['integrity']['lifecycle']['serialize_without_hash_bytes'])}`; SHA-256 calls: `{_fmt(report['integrity']['lifecycle']['sha256_calls'])}`; `canonical_serialize` calls in THROUGHPUT: `{_fmt(report['integrity']['lifecycle']['canonical_serialize_calls'])}`.",
        f"Canonical section byte sum exact: `{report['integrity']['sections_sum_exact']}`. Semantic trace/observation equivalence against the frozen M4.3.3 Release traces: `{report['gates']['semantic_trace_equivalence']}`.",
        f"Focused shape/no-shape fixture equivalence (six visible, hidden, paired-world, perspective, and terminal fixtures): `{report['gates']['focused_shape_equivalence']}`; timing from that check is diagnostic overhead evidence only.",
        "",
        "## Non-overlapping serialization phases",
        "",
        "| Phase | Calls | Total µs | Mean µs/call | % of serialize time |",
        "|---|---:|---:|---:|---:|",
    ]
    for name in PHASES:
        row = phases[name]
        lines.append(f"| `{name}` | {_fmt(row['calls'])} | {_fmt(row['total_us'])} | {_fmt(row['mean_us_per_call'])} | {_fmt(row['fraction_of_serialize_percent'])}% |")
    lines += [
        "",
        f"Total serializer time: `{_fmt(shape['serialize_without_hash']['total_us'])}` µs; phase sum: `{_fmt(shape['phase_sum_us'])}` µs; exact non-overlap reconciliation: `{shape['phase_sum_matches_serialize_total']}`. Rendering is the largest measured residual partition, not an independently isolated timer; it includes remaining stream formatting, primitive formatting, output construction, and probe overhead after copy, sorting, escaping, and final extraction.",
        "",
        "## Exact canonical byte composition",
        "",
        "| Top-level section | Bytes | % of canonical bytes |",
        "|---|---:|---:|",
    ]
    for name in SECTIONS:
        row = sections[name]
        lines.append(f"| `{name}` | {_fmt(row['bytes'])} | {_fmt(row['percent_of_canonical_bytes'])}% |")
    lines += [
        "",
        f"Section total: `{_fmt(report['canonical_byte_composition']['sum_bytes'])}` bytes; exact match: `{report['canonical_byte_composition']['complete_sum_matches_canonical_bytes']}`. Section spans include their emitted key/value delimiters under the documented contiguous-span policy.",
        "",
        "## Observation-size distribution",
        "",
        "| Statistic | Canonical bytes |",
        "|---|---:|",
    ]
    for key, label in [
        ("minimum_bytes", "minimum"),
        ("mean_bytes", "mean"),
        ("median_bytes", "median"),
        ("p95_bytes", "p95"),
        ("p99_bytes", "p99"),
        ("maximum_bytes", "maximum"),
    ]:
        lines.append(f"| {label} | {_fmt(report['size_distribution'][key])} |")
    lines += [
        "",
        "Correlations with decision index, engine step, entity count, visible-event count, and chain length are in the JSON report. They are descriptive and do not establish causation.",
        "",
        "## Visible-event growth",
        "",
        f"Cumulative history observed: `{visible['cumulative_history_observed']}`. Workload first observation: `{_fmt(visible['workload_first']['event_count'])}` events / `{_fmt(visible['workload_first']['bytes'])}` bytes; median: `{_fmt(visible['workload_median_event_count'])}` / `{_fmt(visible['workload_median_bytes'])}`; p95: `{_fmt(visible['workload_p95_event_count'])}` / `{_fmt(visible['workload_p95_bytes'])}`; final: `{_fmt(visible['workload_final']['event_count'])}` / `{_fmt(visible['workload_final']['bytes'])}`; maximum: `{_fmt(visible['workload_maximum']['event_count'])}` / `{_fmt(visible['workload_maximum']['bytes'])}`.",
        f"Serialized event instances: `{_fmt(visible['serialized_event_instances'])}`; unique identities observed through the per-job diagnostic proxy: `{_fmt(visible['unique_visible_event_identities_seen'])}`; repetition-factor lower bound: `{_fmt(visible['repetition_factor_lower_bound'])}`. The serializer therefore re-emits historical event identities in later observations; event semantics were not changed.",
        "The canonical simulation creates one ObservationSession per perspective and does not call `clear()`; `ingest()` assigns monotonic event indices. The uniqueness count is a per-job diagnostic proxy, not a durable/canonical event ID. Tuple collisions can undercount unique events, so the reported repetition factor is a lower bound; the `clear()` reset caveat is retained in the JSON evidence.",
        "",
        "## Entity serialization",
        "",
        f"Entities serialized: `{_fmt(entity['entity_instances'])}`; entity bytes: `{_fmt(entity['entity_bytes'])}`; mean bytes/entity: `{_fmt(entity['mean_bytes_per_entity'])}`. Printed property bytes: `{_fmt(entity['printed_property_bytes'])}`; current property bytes: `{_fmt(entity['current_property_bytes'])}`; combined printed/current fraction of entity bytes: `{_fmt(entity['printed_current_bytes_fraction_of_entity_bytes'])}`%.",
        f"Counters: `{_fmt(entity['counter_instances'])}` instances / `{_fmt(entity['counter_bytes'])}` bytes. Link markers: `{_fmt(entity['link_marker_instances'])}` instances / `{_fmt(entity['link_marker_bytes'])}` bytes.",
        "",
        "## Static match-context repetition",
        "",
        f"Own deck bytes: `{_fmt(report['static_match_context']['own_deck_bytes'])}`; opponent deck bytes: `{_fmt(report['static_match_context']['opponent_deck_bytes'])}`; other immutable match-context bytes: `{_fmt(report['static_match_context']['other_match_context_bytes'])}`. Total: `{_fmt(report['static_match_context']['total_static_match_context_bytes'])}` bytes / `{_fmt(report['static_match_context']['total_static_match_context_percent_of_canonical_bytes'])}`% of canonical output.",
        "",
        "## Copy, sorting, and formatting",
        "",
        f"Copy time: `{_fmt(copy_boundary['copy_total_us'])}` µs ({_fmt(copy_boundary['copy_fraction_of_serialize_percent'])}%); sorting time: `{_fmt(copy_boundary['sort_total_us'])}` µs ({_fmt(copy_boundary['sort_fraction_of_serialize_percent'])}%). Copy/sort phase totals reconcile: `{copy_boundary['copy_sort_time_matches_phase_totals']}`.",
        f"`json_escape` calls: `{_fmt(shape['formatting']['json_escape_calls'])}`; input bytes: `{_fmt(shape['formatting']['json_escape_input_bytes'])}`; escaped output bytes: `{_fmt(shape['formatting']['json_escape_output_bytes'])}`; numeric values: `{_fmt(shape['formatting']['numeric_values'])}`; booleans: `{_fmt(shape['formatting']['boolean_values'])}`; nulls: `{_fmt(shape['formatting']['null_values'])}`. Allocator-growth counts: `NOT_MEASURED` (no global allocation hook was added).",
        "",
        "Per-copy-kind and per-sort-kind calls, elements, approximate copied bytes, and timing are recorded in the JSON report. Approximate copy bytes describe copied object/vector storage; canonical emitted bytes are measured separately.",
        "",
        "## Runtime and instrumentation check",
        "",
        f"Shape workload worker-local runtime: `{_fmt(runtime['worker_local_simulation_runtime_us'])}` µs; throughput: `{_fmt(runtime['games_per_second'])}` games/s.",
    ]
    if comparison["instrumentation_overhead_check"]["status"] == "MEASURED_SAME_HEAD_CONTROL":
        overhead = comparison["instrumentation_overhead_check"]["shape_worker_vs_no_shape_control"]
        lines += [
            f"Against the same-head Release control without shape instrumentation, the shape worker changed canonical-serialization timing by `{_fmt(overhead['observation_canonical_serialization_us']['delta_percent'])}`% and worker-local runtime by `{_fmt(overhead['worker_local_runtime_us']['delta_percent'])}`%. This is a measured workload-level instrumentation signal, not a speedup claim and not an isolated overhead benchmark.",
        ]
    elif comparison.get("reference_available"):
        overhead = comparison["instrumentation_overhead_check"]["shape_worker_vs_frozen_release"]
        lines += [
            f"Against the frozen M4.3.3 Release reference, the shape worker changed canonical-serialization timing by `{_fmt(overhead['observation_canonical_serialization_us']['delta_percent'])}`% and worker-local runtime by `{_fmt(overhead['worker_local_runtime_us']['delta_percent'])}`%. This is a measured workload-level instrumentation signal, not a speedup claim and not an isolated overhead benchmark.",
        ]
    else:
        lines.append("Frozen M4.3.3 Release comparison: `NOT_MEASURED`.")
    lines += [
        "",
        "## Candidate classification and recommendation",
        "",
        f"Observed classifications: {', '.join(f'`{item}`' for item in report['classifications'])}.",
        "",
        "| Rank | Candidate | Measured runtime fraction | Emitted-byte fraction | Complexity |",
        "|---:|---|---:|---:|---|",
    ]
    for candidate in report["optimization_candidates"]:
        runtime_fraction = "NOT_DIRECTLY_MEASURED" if candidate["measured_runtime_fraction_percent"] is None else f"{_fmt(candidate['measured_runtime_fraction_percent'])}%"
        bytes_fraction = "NOT_DIRECTLY_MEASURED" if candidate["measured_bytes_fraction_percent"] is None else f"{_fmt(candidate['measured_bytes_fraction_percent'])}%"
        lines.append(f"| {candidate['rank']} | `{candidate['name']}` | {runtime_fraction} | {bytes_fraction} | {candidate['implementation_complexity']} |")
    recommendation = report["recommendation"]["first_optimization_experiment"]
    lines += [
        "",
        f"**First future experiment:** `{recommendation['candidate']}` — {recommendation['concept']}",
        "",
        "The candidate details in the JSON report include measured evidence, suspected redundant work, semantic/privacy risk, required equivalence tests, and affected buckets. No speedup estimate is made because this audit does not measure a candidate implementation.",
        "",
        "## Gates",
        "",
        "| Gate | Status | Verification evidence |",
        "|---|---|---|",
    ]
    for name, status in report["gates"].items():
        evidence = report["gate_provenance"]["evidence"].get(name, "NOT_RECORDED")
        if isinstance(evidence, dict):
            command = " ".join(str(argument) for argument in evidence.get("command", []))
            evidence = (
                f"exit={evidence.get('returncode')}; command=`{command}`; "
                f"stdout_sha256=`{evidence.get('stdout_sha256')}`; stderr_sha256=`{evidence.get('stderr_sha256')}`"
            )
        lines.append(f"| `{name}` | `{status}` | {evidence} |")
    lines += [
        "",
        "The audit stops here. No optimization is authorized by this characterization task.",
        "",
    ]
    return "\n".join(lines)


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--reference-audit", type=Path, default=None)
    parser.add_argument("--control-audit", type=Path, default=None)
    parser.add_argument("--control-worker", type=Path, default=None)
    parser.add_argument("--compile-commands", type=Path, default=None)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-markdown", type=Path, required=True)
    parser.add_argument("--gate", action="append", default=[], help="NAME=PASS|FAIL|NOT_RUN|SKIPPED|BLOCKED")
    parser.add_argument("--gate-evidence", action="append", default=[], help="NAME=verification evidence")
    parser.add_argument("--gate-evidence-manifest", type=Path, default=None)
    args = parser.parse_args(arguments)
    run = _load(args.run)
    gates = _parse_gates(args.gate)
    gate_evidence = _parse_gate_evidence(args.gate_evidence)
    report = build_report(
        run,
        run_path=args.run,
        worker=args.worker,
        reference_audit=args.reference_audit,
        compile_commands=args.compile_commands,
        control_audit=args.control_audit,
        control_worker=args.control_worker,
        gates=gates,
        gate_evidence=gate_evidence,
        gate_manifest=args.gate_evidence_manifest,
    )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.output_markdown.write_text(render_markdown(report), encoding="utf-8")
    print(json.dumps({"status": report["status"], "json": str(args.output_json.resolve()), "markdown": str(args.output_markdown.resolve())}))
    return 0 if report["status"] == "M4.3.4 SERIALIZATION CHARACTERIZATION PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
