"""Finalize the M4.3.5 report after the required regression gates."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Mapping, Sequence

_REPO_ROOT = Path(__file__).resolve().parents[2]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.m4.run_m4_3_5_reserve_ab import (
    AUDIT_PREFIX,
    LIFECYCLE_PREFIX,
    _parse_prefixed_records,
    _sum_numeric_maps,
    _timing_decision,
)
from tools.m4.test_reserve_output_contract import check as check_reserve_output_contract


def _stats(values: Sequence[float | int]) -> dict[str, float | int | list[float | int]]:
    ordered = sorted(values)
    if not ordered:
        return {"min": "NOT_RUN", "median": "NOT_RUN", "max": "NOT_RUN", "range": []}
    middle = ordered[len(ordered) // 2] if len(ordered) % 2 else (ordered[len(ordered) // 2 - 1] + ordered[len(ordered) // 2]) / 2
    return {"min": ordered[0], "median": middle, "max": ordered[-1], "range": [ordered[0], ordered[-1]]}


def _fmt(value: float | int) -> str:
    if isinstance(value, float):
        return f"{value:.6f}"
    return f"{value:,}"


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _inspect_build(
    build_dir: Path,
    worker: Path,
    ready: Sequence[Mapping[str, Any]],
    *,
    expected_variant: str,
) -> dict[str, Any]:
    cache_path = build_dir / "CMakeCache.txt"
    ninja_path = build_dir / "build.ninja"
    if not cache_path.is_file() or not ninja_path.is_file() or not worker.is_file():
        raise ValueError(f"missing build identity evidence under {build_dir}")
    cache = cache_path.read_text(encoding="utf-8", errors="replace")
    ninja = ninja_path.read_text(encoding="utf-8", errors="replace")
    values: dict[str, str] = {}
    for key in (
        "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID",
        "CMAKE_CXX_COMPILER_VERSION", "CMAKE_CXX_FLAGS", "CMAKE_GENERATOR",
    ):
        prefix = key + ":"
        values[key] = next(
            (line.split("=", 1)[1] for line in cache.splitlines() if line.startswith(prefix) and "=" in line),
            "NOT_RECORDED",
        )
    flag_lines = "\n".join(line for line in ninja.splitlines() if "FLAGS =" in line)
    forbidden_tokens = (
        "-flto", "/gl", "lto", "pgo", "-fprofile", "-march=", "-mcpu=", "-mtune=",
        "/arch:", "-mavx", "-msse", "ffast-math",
        "ygo_m4_serialization_shape_audit",
    )
    forbidden = sorted(token for token in forbidden_tokens if token in flag_lines.lower())
    if expected_variant not in {"control", "experiment"}:
        raise ValueError(f"unknown A/B build variant: {expected_variant}")
    reserve_macro_present = "ygo_m4_reserve_backed_serialization" in flag_lines.lower()
    expected_reserve_macro = expected_variant == "experiment"
    if reserve_macro_present != expected_reserve_macro:
        expected = "present" if expected_reserve_macro else "absent"
        raise ValueError(
            f"{build_dir} reserve macro must be {expected}, "
            f"but was {'present' if reserve_macro_present else 'absent'}"
        )
    if values["CMAKE_BUILD_TYPE"] != "Release" or values["CMAKE_GENERATOR"] != "Ninja":
        raise ValueError(f"{build_dir} is not a Release/Ninja build")
    if "-O3" not in flag_lines or "-DNDEBUG" not in flag_lines:
        raise ValueError(f"{build_dir} lacks ordinary Release -O3 -DNDEBUG evidence")
    if forbidden:
        raise ValueError(f"{build_dir} contains forbidden optimization flags: {forbidden}")
    compiler_identity = ready[0].get("compiler_identity") if ready else "NOT_RECORDED"
    if not isinstance(compiler_identity, str) or not compiler_identity:
        raise ValueError(f"{build_dir} lacks compiler identity in the ready record")
    return {
        "build_dir": str(build_dir.resolve()),
        "worker": str(worker.resolve()),
        "worker_sha256_current_path": _sha256_file(worker),
        "cmake_cache_sha256": hashlib.sha256(cache.encode("utf-8")).hexdigest(),
        "cmake": values,
        "compiler_identity_from_ready": compiler_identity,
        "release_flags_observed": "-O3 -DNDEBUG",
        "forbidden_flags": forbidden,
        "expected_variant": expected_variant,
        "reserve_macro_present": reserve_macro_present,
        "shape_instrumentation_present": "YGO_M4_SERIALIZATION_SHAPE_AUDIT" in flag_lines,
        "ordinary_release_policy_pass": True,
    }


def _resolve_artifact(value: object, label: str) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} does not name an artifact")
    path = Path(value)
    if not path.is_absolute():
        path = _REPO_ROOT / path
    if not path.is_file():
        raise ValueError(f"{label} artifact is missing: {path}")
    return path


def _validate_repetition_sidecars(row: Mapping[str, Any]) -> dict[str, Any]:
    label = row.get("label", "unknown")
    variant = row.get("variant")
    expected_mode = "ostringstream" if variant == "control" else "reserve_backed"
    summary = row.get("summary")
    if not isinstance(summary, Mapping):
        raise ValueError(f"{label} has no summary object")
    evidence = summary.get("worker_stderr")
    if not isinstance(evidence, list) or not evidence:
        raise ValueError(f"{label} has no worker stderr evidence")

    paths: list[Path] = []
    for index, item in enumerate(evidence):
        if not isinstance(item, Mapping):
            raise ValueError(f"{label} worker stderr evidence {index} is malformed")
        path = _resolve_artifact(item.get("path"), f"{label} worker stderr {index}")
        actual_bytes = path.stat().st_size
        actual_sha256 = _sha256_file(path)
        if actual_bytes != item.get("bytes") or actual_sha256 != item.get("sha256"):
            raise ValueError(f"{label} worker stderr evidence hash/size does not match the file")
        paths.append(path)

    expected_job_ids = summary.get("job_ids")
    if not isinstance(expected_job_ids, list) or not all(isinstance(value, str) for value in expected_job_ids):
        raise ValueError(f"{label} has malformed job IDs")
    expected_job_id_set = set(expected_job_ids)
    lifecycle_records = _parse_prefixed_records(paths, LIFECYCLE_PREFIX)
    audit_records = _parse_prefixed_records(paths, AUDIT_PREFIX)
    if set(lifecycle_records) != expected_job_id_set:
        raise ValueError(f"{label} lifecycle record IDs do not exactly match the workload job IDs")
    if set(audit_records) != expected_job_id_set:
        raise ValueError(f"{label} performance record IDs do not exactly match the workload job IDs")

    result_evidence = summary.get("worker_result_evidence")
    if not isinstance(result_evidence, Mapping):
        raise ValueError(f"{label} has no worker result evidence")
    result_path = _resolve_artifact(result_evidence.get("path"), f"{label} worker results")
    if result_path.stat().st_size != result_evidence.get("bytes") or _sha256_file(result_path) != result_evidence.get("sha256"):
        raise ValueError(f"{label} worker result evidence hash/size does not match the file")
    raw_results = json.loads(result_path.read_text(encoding="utf-8"))
    if not isinstance(raw_results, list) or not all(isinstance(result, Mapping) for result in raw_results):
        raise ValueError(f"{label} worker result evidence is malformed")
    raw_result_ids = [result.get("job_id") for result in raw_results]
    if set(raw_result_ids) != expected_job_id_set or len(raw_result_ids) != len(expected_job_id_set):
        raise ValueError(f"{label} worker result IDs do not exactly match the workload job IDs")
    worker_us = 0
    for result in raw_results:
        elapsed = result.get("simulation_elapsed_us")
        if isinstance(elapsed, bool) or not isinstance(elapsed, int) or elapsed < 0:
            raise ValueError(f"{label} worker result has malformed simulation_elapsed_us")
        worker_us += elapsed
    if worker_us != summary.get("worker_local_simulation_us"):
        raise ValueError(f"{label} worker-local runtime does not match the re-parsed worker results")
    if len(raw_results) != summary.get("games"):
        raise ValueError(f"{label} game count does not match the re-parsed worker results")
    if worker_us == 0:
        raise ValueError(f"{label} worker results have zero runtime")
    recomputed_games_per_second = len(raw_results) / (worker_us / 1_000_000)
    if abs(recomputed_games_per_second - summary.get("games_per_second", -1.0)) > 1e-15:
        raise ValueError(f"{label} games/s does not match the re-parsed worker results")
    operation_keys = (
        "ocg_duel_process", "ocg_duel_query", "ocg_duel_query_location", "ocg_duel_query_field",
        "ocg_duel_query_count", "script_reader_requests", "script_loads", "observations",
        "entities_projected", "candidate_sets", "candidate_total", "candidate_max",
        "semantic_hashes", "trace_bytes_serialized",
    )
    error_keys = ("retries", "unsupported", "automatic", "truncated", "core_errors", "worker_errors")
    if _sum_numeric_maps((result.get("counters", {}) for result in raw_results), operation_keys) != dict(summary["operation_counters"]):
        raise ValueError(f"{label} operation counters do not match the re-parsed worker results")
    if _sum_numeric_maps((result.get("errors", {}) for result in raw_results), error_keys) != dict(summary["error_counters"]):
        raise ValueError(f"{label} error counters do not match the re-parsed worker results")

    lifecycle_keys = (
        "serialize_without_hash_calls", "serialize_without_hash_bytes", "sha256_calls",
        "canonical_serialize_calls", "canonical_serialize_bytes", "same_mutation_epoch_duplicate_calls",
    )
    lifecycle_totals = {key: 0 for key in lifecycle_keys}
    lifecycle_entry_count = 0
    for job_id, record in lifecycle_records.items():
        entries = record.get("lifecycle_records")
        if not isinstance(entries, list):
            raise ValueError(f"{label}/{job_id} has no lifecycle_records array")
        lifecycle_count = record.get("lifecycle_count")
        if isinstance(lifecycle_count, bool) or not isinstance(lifecycle_count, int) or lifecycle_count != len(entries):
            raise ValueError(f"{label}/{job_id} lifecycle_count does not match its records")
        lifecycle_ids = [entry.get("lifecycle_id") for entry in entries if isinstance(entry, Mapping)]
        if len(lifecycle_ids) != len(entries) or len(set(lifecycle_ids)) != len(entries):
            raise ValueError(f"{label}/{job_id} lifecycle IDs are not unique")
        if set(lifecycle_ids) != set(range(1, len(entries) + 1)):
            raise ValueError(f"{label}/{job_id} lifecycle IDs are not complete")
        for entry in entries:
            if any(not isinstance(entry.get(key), int) or isinstance(entry.get(key), bool) for key in lifecycle_keys):
                raise ValueError(f"{label}/{job_id} lifecycle record has a non-integer counter")
            if entry["serialize_without_hash_calls"] != 1 or entry["sha256_calls"] != 1:
                raise ValueError(f"{label}/{job_id} lifecycle does not have exactly one serialize/SHA call")
            if entry["serialize_without_hash_bytes"] <= 0:
                raise ValueError(f"{label}/{job_id} lifecycle emitted no canonical bytes")
            if entry["canonical_serialize_calls"] != 0 or entry["canonical_serialize_bytes"] != 0:
                raise ValueError(f"{label}/{job_id} lifecycle called canonical_serialize")
            if entry["same_mutation_epoch_duplicate_calls"] != 0:
                raise ValueError(f"{label}/{job_id} lifecycle contains a same-epoch duplicate")
        lifecycle_entry_count += len(entries)
        for key in lifecycle_keys:
            value = record.get(key)
            if isinstance(value, bool) or not isinstance(value, int):
                raise ValueError(f"{label}/{job_id} lifecycle aggregate has a non-integer counter")
            lifecycle_totals[key] += value
        if record.get("lifecycle_count") != record.get("serialize_without_hash_calls"):
            raise ValueError(f"{label}/{job_id} lifecycle count does not close against serialize calls")
        if record.get("serialize_without_hash_calls") != record.get("sha256_calls"):
            raise ValueError(f"{label}/{job_id} lifecycle count does not close against SHA calls")

    if lifecycle_totals != dict(summary["lifecycle"]):
        raise ValueError(f"{label} lifecycle aggregate does not match the recorded summary")
    if lifecycle_entry_count != summary["lifecycle"]["serialize_without_hash_calls"]:
        raise ValueError(f"{label} lifecycle entry count does not match the recorded summary")

    reserve_keys = ("calls", "requested_capacity", "final_bytes", "final_capacity", "growth_events", "unused_capacity")
    reserve_modes: set[str] = set()
    reserve_totals = {key: 0 for key in reserve_keys}
    missing_reserve_records = 0
    timing_fields = {
        "outer_observation_us": "observation_total_us",
        "serializer_us": "observation_canonical_serialization",
        "hash_us": "observation_hash",
        "query_decode_us": "observation_query_decode",
        "query_location_us": "observation_query_location",
        "query_field_us": "observation_query_field",
        "entity_projection_us": "observation_entity_projection",
        "privacy_us": "observation_visibility_privacy",
    }
    sidecar_timing_totals = {key: 0 for key in timing_fields}
    for job_id, record in audit_records.items():
        for summary_key, sidecar_key in timing_fields.items():
            if sidecar_key == "observation_total_us":
                value = record.get(sidecar_key)
            else:
                timing_record = record.get("observation_timing_us")
                value = timing_record.get(sidecar_key, {}).get("total_us") if isinstance(timing_record, Mapping) else None
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise ValueError(f"{label}/{job_id} has malformed {sidecar_key} timing")
            sidecar_timing_totals[summary_key] += value
        reserve = record.get("future_m4_3_5_reserve_output")
        if not isinstance(reserve, Mapping):
            if expected_mode == "ostringstream" and reserve is None:
                missing_reserve_records += 1
                continue
            raise ValueError(f"{label}/{job_id} has no reserve-output record")
        mode = reserve.get("mode")
        if not isinstance(mode, str):
            raise ValueError(f"{label}/{job_id} reserve-output mode is malformed")
        reserve_modes.add(mode)
        for key in reserve_keys:
            value = reserve.get(key)
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise ValueError(f"{label}/{job_id} reserve-output counter {key} is malformed")
            reserve_totals[key] += value
    if missing_reserve_records:
        if expected_mode != "ostringstream" or missing_reserve_records != len(audit_records) or reserve_modes:
            raise ValueError(f"{label} has incomplete reserve-output telemetry")
        reserve_modes.add("ostringstream")
        reserve_totals.update({
            "calls": summary["lifecycle"]["serialize_without_hash_calls"],
            "final_bytes": summary["lifecycle"]["serialize_without_hash_bytes"],
        })
    if len(reserve_modes) != 1:
        raise ValueError(f"{label} reserve-output modes are mixed")
    reserve_summary = dict(summary["reserve_output"])
    reserve_summary["mode"] = reserve_modes.pop()
    if reserve_totals != {key: reserve_summary[key] for key in reserve_keys}:
        raise ValueError(f"{label} reserve-output aggregate does not match the recorded summary")
    if reserve_summary != dict(summary["reserve_output"]):
        raise ValueError(f"{label} reserve-output mode does not match the recorded summary")
    for summary_key, sidecar_value in sidecar_timing_totals.items():
        if summary.get(summary_key) != sidecar_value:
            raise ValueError(
                f"{label} timing {summary_key} does not match the re-parsed sidecars"
            )
    return {
        "sidecar_count": len(paths),
        "sidecar_sha256_verified": True,
        "job_ids_verified": sorted(expected_job_id_set),
        "lifecycle_records_verified": lifecycle_entry_count,
        "reserve_records_verified": len(audit_records),
        "timing_totals_verified": sidecar_timing_totals,
        "worker_results_verified": len(raw_results),
    }


def _render_markdown(report: Mapping[str, Any]) -> str:
    repetitions = report["repetitions"]
    timing = report["timing_summary"]
    gates = report["gates"]
    worker_direction = "experiment faster" if timing["worker_speedup_percent"] > 0 else "control faster"
    serializer_direction = "experiment faster" if timing["serializer_speedup_percent"] > 0 else "control faster"
    lines = [
        "# M4.3.5 Reserve-Backed Canonical Serialization A/B",
        "",
        f"**Status:** {report['status']}",
        "",
        "M4.3.5 was a single isolated output-buffer experiment. The reserve-backed "
        "implementation was rejected after exact equivalence and clean Release A/B "
        "measurement; the production optimization was reverted.",
        "",
        "## Frozen identity and workload",
        "",
        f"- M4.3.4 freeze: `{report['repository']['m4_3_4_freeze_commit']}`.",
        f"- M4.3.5 starting HEAD: `{report['repository']['starting_head']}`.",
        f"- Matchup: {report['workload']['matchup']}; master seed `{report['workload']['master_seed']}`.",
        f"- Games/workers/max steps: `{report['workload']['games']}` / `{report['workload']['workers']}` / `{report['workload']['max_steps']}`.",
        "- FULL observations, throughput mode, trace persistence off, ordinary Release `-O3 -DNDEBUG`.",
        "- No M4.3.4 shape instrumentation was used for timing.",
        f"- Measured A/B worker hashes: control `{report['builds']['control']['measured_worker_sha256']}`, experiment `{report['builds']['experiment']['measured_worker_sha256']}`.",
        f"- Post-reversion ordinary Release worker hash: `{report['post_reversion_build']['worker_sha256_current_path']}`.",
        "",
        "## Implementation and reversion",
        "",
        "The experiment generalized the internal writer to `std::ostream`, added a "
        "private string-backed stream buffer, and reserved a deterministic structure-only "
        "capacity hint. Field ordering, formatting, escaping, sorting, canonical bytes, "
        "SHA-256 input, privacy, and event history were unchanged.",
        "",
        "The measured result did not meet the materiality rule, so the reserve-backed "
        "production path and its telemetry were reverted. The focused fixture, comparison "
        "harness, raw A/B artifacts, and this report remain as characterization evidence.",
        "",
        "## Audit harness integrity",
        "",
        "- Raw trace-hash mismatches fail the conformance comparison.",
        "- The A/B runner closes reserve calls/bytes against lifecycle serialization counters and rejects nonmaterial timing.",
        "- Finalization recomputes materiality, re-hashes all six worker sidecars, verifies every lifecycle ID and per-lifecycle call, and checks control/experiment build identity equality.",
        "- Finalization validates the equivalence artifacts, all control/experiment gates, starting-HEAD reversion, and Release build policy.",
        "",
        "## Equivalence",
        "",
        "| Gate | Result | Evidence |",
        "|---|---|---|",
        f"| Focused serialization/privacy fixtures | **PASS** | `{report['fixture_comparison_artifact']}` |",
        f"| 16-game conformance, trace steps and per-observation hashes | **PASS** | `{report['conformance_comparison_artifact']}`; 9,908 hashes |",
        f"| Canonical bytes and observation hashes | **PASS** | {report['canonical_workload']['canonical_without_hash_bytes']:,} bytes in each of six repetitions; 9,908 hashes equal in conformance |",
        "| Operation/error counters | **PASS** | Identical across A1/B1/A2/B2/A3/B3; all integrity counters zero |",
        "| Privacy and paired-world behavior | **PASS** | Focused fixtures, CTest and conformance |",
        "| Sidecar lifecycle and build identity closure | **PASS** | Six sidecars re-hashed; lifecycle IDs/calls and compiler/rules/deck identities revalidated |",
        "",
        "## Raw alternating Release measurements",
        "",
        "| Run | Variant | Worker-local us | Games/s | Outer observation us | Serializer us | Hash us |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for row in repetitions:
        summary = row["summary"]
        lines.append(
            f"| {row['label']} | {row['variant']} | {summary['worker_local_simulation_us']:,} | "
            f"{summary['games_per_second']:.9f} | {summary['outer_observation_us']:,} | "
            f"{summary['serializer_us']:,} | {summary['hash_us']:,} |"
        )
    lines.extend([
        "",
        "## Median and range",
        "",
        f"- Control worker-local: `{_fmt(timing['control']['worker_local_simulation_us']['median'])}` us "
        f"(range `{_fmt(timing['control']['worker_local_simulation_us']['min'])}`–`{_fmt(timing['control']['worker_local_simulation_us']['max'])}`).",
        f"- Experiment worker-local: `{_fmt(timing['experiment']['worker_local_simulation_us']['median'])}` us "
        f"(range `{_fmt(timing['experiment']['worker_local_simulation_us']['min'])}`–`{_fmt(timing['experiment']['worker_local_simulation_us']['max'])}`).",
        f"- Control serializer: `{_fmt(timing['control']['serializer_us']['median'])}` us "
        f"(range `{_fmt(timing['control']['serializer_us']['min'])}`–`{_fmt(timing['control']['serializer_us']['max'])}`).",
        f"- Experiment serializer: `{_fmt(timing['experiment']['serializer_us']['median'])}` us "
        f"(range `{_fmt(timing['experiment']['serializer_us']['min'])}`–`{_fmt(timing['experiment']['serializer_us']['max'])}`).",
        f"- Worker-local median change: **{timing['worker_speedup_percent']:.6f}%**.",
        f"- Serializer median change: **{timing['serializer_speedup_percent']:.6f}%**.",
        f"- Paired improvements: worker `{timing['paired_worker_improvements']}/3`; serializer `{timing['paired_serializer_improvements']}/3`.",
        "- Materiality rule: serializer median >= 5%, worker median >= 3%, and >= 2/3 paired improvements for both. **FAIL**.",
        "",
        "## Reserve telemetry",
        "",
        "The experiment requested a large deterministic hint but produced no growth events. "
        "Across the 9,908-observation sample, the requested capacity was "
        f"`{report['reserve_telemetry']['requested_capacity']:,}` bytes versus "
        f"`{report['reserve_telemetry']['final_bytes']:,}` output bytes; unused final capacity was "
        f"`{report['reserve_telemetry']['unused_capacity']:,}` bytes. This is diagnostic evidence, "
        "not a protocol limit or a reason to change canonical content.",
        "",
        "## Regression gates",
        "",
    ])
    for name, gate in sorted(gates.items()):
        if isinstance(gate, Mapping):
            detail = gate.get("passed")
            if detail is not None and gate.get("total") is not None:
                detail = f"{detail}/{gate['total']}"
            elif gate.get("complete_games") is not None:
                detail = f"{gate['complete_games']}/{gate['required_complete_games']} games"
            elif gate.get("starting_player_partitions") is not None:
                detail = f"partitions={gate['starting_player_partitions']}"
            else:
                detail = ""
            lines.append(f"- `{name}`: **{gate.get('status', 'NOT_RECORDED')}** {detail}")
    lines.extend([
        "",
        f"Gate freshness/provenance: {report.get('gate_freshness', 'NOT_RECORDED')}",
        f"Interactive command output hashes: {report.get('gate_output_hashes', 'NOT_RECORDED')}",
        "",
        "## Decision",
        "",
        "**M4.3.5 REJECTED — NO MATERIAL BENEFIT.** The median result was "
        f"{worker_direction} by {abs(timing['worker_speedup_percent']):.6f}% for worker-local runtime "
        f"and {serializer_direction} by {abs(timing['serializer_speedup_percent']):.6f}% for serializer runtime. "
        "Those changes are far below the materiality thresholds; no future worker-matrix extrapolation is made.",
        "",
        f"Next recommendation: {report['recommendation']}",
        "",
        "No event-history reduction, `to_chars` rewrite, JSON-escape rewrite, hash change, "
        "cache, incremental observation, ocgcore change, or M5 work was performed.",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-json", type=Path, required=True)
    parser.add_argument("--gate-summary", type=Path, required=True)
    parser.add_argument("--output-json", type=Path, required=True)
    parser.add_argument("--output-markdown", type=Path, required=True)
    args = parser.parse_args()

    report = json.loads(args.input_json.read_text(encoding="utf-8"))
    gate_summary = json.loads(args.gate_summary.read_text(encoding="utf-8"))
    repetitions = report["repetitions"]
    control = [row["summary"] for row in repetitions if row["variant"] == "control"]
    experiment = [row["summary"] for row in repetitions if row["variant"] == "experiment"]
    timing = report["timing_decision"]
    required_gates = (
        "full_ctest",
        "full_ctest_control",
        "repository_python",
        "m3_python",
        "m4_python",
        "privacy",
        "privacy_control",
        "candidate_observation_consistency",
        "candidate_observation_consistency_control",
        "canonical_fixed_deck_regression",
        "determinism",
        "reserve_output_contract",
        "post_reversion_release_smoke",
    )
    missing_gates = [key for key in required_gates if key not in gate_summary]
    if missing_gates:
        raise ValueError(f"gate summary is missing required gates: {', '.join(missing_gates)}")
    failed_gates = []
    for key in required_gates:
        gate = gate_summary[key]
        if not isinstance(gate, Mapping) or gate.get("status") != "PASS":
            failed_gates.append(key)
            continue
        if "passed" in gate and "total" in gate and gate["passed"] != gate["total"]:
            failed_gates.append(key)
        if "complete_games" in gate and "required_complete_games" in gate and gate["complete_games"] != gate["required_complete_games"]:
            failed_gates.append(key)
    if failed_gates:
        raise ValueError(f"cannot finalize with failed or malformed gates: {', '.join(failed_gates)}")
    if timing.get("pass") is not False:
        raise ValueError("this finalizer is only valid for a measured NO MATERIAL BENEFIT result")
    if len(control) != 3 or len(experiment) != 3:
        raise ValueError("expected three control and three experiment repetitions")
    recomputed_timing = _timing_decision(repetitions)
    if recomputed_timing != timing:
        raise ValueError("recorded timing decision does not match the six raw A/B repetitions")
    timing = recomputed_timing

    sidecar_validation = {
        row["label"]: _validate_repetition_sidecars(row)
        for row in repetitions
    }

    identity_fields = (
        "build_type", "compiler_identity", "core_patchset_sha256", "deck_hashes",
        "duel_flags", "duel_mode_name", "format_id", "protocol_version",
        "rules_bundle_id", "worker_identity",
    )
    ready_by_label: dict[str, dict[str, Any]] = {}
    for row in repetitions:
        label = row["label"]
        variant = row["variant"]
        ready = row["summary"].get("ready")
        if not isinstance(ready, list) or len(ready) != 1 or not isinstance(ready[0], Mapping):
            raise ValueError(f"{label} does not have exactly one ready identity")
        ready_by_label[label] = {
            key: ready[0].get(key)
            for key in identity_fields
        }
    for variant in ("control", "experiment"):
        variant_labels = [row["label"] for row in repetitions if row["variant"] == variant]
        if any(ready_by_label[label] != ready_by_label[variant_labels[0]] for label in variant_labels[1:]):
            raise ValueError(f"{variant} ready identities differ between repetitions")
    if ready_by_label["A1"] != ready_by_label["B1"]:
        raise ValueError("control and experiment ready identities differ")
    ready_by_variant = {"control": ready_by_label["A1"], "experiment": ready_by_label["B1"]}

    fixed_gate = gate_summary["canonical_fixed_deck_regression"]
    fixed_results = json.loads(
        _resolve_artifact(fixed_gate.get("artifact"), "canonical fixed-deck regression").read_text(
            encoding="utf-8"
        )
    )
    if (
        fixed_results.get("complete_games") != fixed_results.get("required_complete_games")
        or fixed_results.get("complete_games") != 16
        or fixed_results.get("both_start_player_partitions") is not True
        or fixed_results.get("core_error_count") != 0
        or fixed_results.get("retry_count") != 0
        or fixed_results.get("unsupported_count") != 0
    ):
        raise ValueError("fixed-deck artifact does not prove the required 16-game gate")
    fixed_rows = fixed_results.get("results")
    if (
        not isinstance(fixed_rows, list)
        or len(fixed_rows) != 16
        or any(
            not isinstance(row, Mapping)
            or row.get("status") != "PASS"
            or row.get("core_error_count") != 0
            or row.get("retry_count") != 0
            or row.get("unsupported_count") != 0
            for row in fixed_rows
        )
    ):
        raise ValueError("fixed-deck artifact contains a failed or incomplete result row")

    determinism_gate = gate_summary["determinism"]
    determinism_results = json.loads(
        _resolve_artifact(determinism_gate.get("artifact"), "determinism").read_text(encoding="utf-8")
    )
    if (
        determinism_results.get("starting_player_partitions") != [0, 1]
        or determinism_results.get("independent_process_match") is not True
        or determinism_results.get("semantic_action_reexecution_match") is not True
        or determinism_results.get("crlf_semantic_replay_match") is not True
    ):
        raise ValueError("determinism artifact does not prove both required partitions")

    reserve_contract_path = _resolve_artifact(
        gate_summary["reserve_output_contract"].get("artifact"),
        "reserve output contract",
    )
    reserve_contract = check_reserve_output_contract(reserve_contract_path)

    fixture_report = json.loads(Path(report["fixture_comparison_artifact"]).read_text(encoding="utf-8"))
    conformance_report = json.loads(Path(report["conformance_comparison_artifact"]).read_text(encoding="utf-8"))
    fixture_pass = fixture_report.get("comparison", {}).get("pass") is True
    comparison = conformance_report.get("comparison", {})
    conformance_pass = comparison.get("pass") is True
    trace_hash_pass = all(
        comparison.get(key) is True
        for key in ("raw_trace_hash_equal", "normalized_trace_hash_equal", "observation_hashes_equal")
    )
    row_trace_hash_pass = all(
        all(row.get(key) is True for key in ("raw_trace_hash_equal", "observation_hashes_equal"))
        for row in comparison.get("rows", [])
    )
    observation_hash_count = sum(int(row.get("observation_count", 0)) for row in comparison.get("rows", []))
    if not fixture_pass or not conformance_pass or not trace_hash_pass or not row_trace_hash_pass:
        raise ValueError("equivalence artifacts do not prove byte/hash/trace equivalence")
    if observation_hash_count != 9908:
        raise ValueError(f"expected 9,908 conformance observation hashes, got {observation_hash_count}")

    operation_reference = control[0]["operation_counters"]
    error_reference = control[0]["error_counters"]
    canonical_bytes = set()
    counters_equal = True
    errors_zero = True
    lifecycle_reference = None
    for row in repetitions:
        variant = row["variant"]
        summary = row["summary"]
        lifecycle = summary["lifecycle"]
        reserve = summary["reserve_output"]
        if lifecycle["serialize_without_hash_calls"] != reserve["calls"]:
            raise ValueError("final report lifecycle/reserve call counts do not close")
        if lifecycle["serialize_without_hash_bytes"] != reserve["final_bytes"]:
            raise ValueError("final report lifecycle/reserve byte counts do not close")
        if lifecycle["sha256_calls"] != lifecycle["serialize_without_hash_calls"]:
            raise ValueError("final report SHA-256 calls do not close against lifecycle calls")
        if lifecycle["canonical_serialize_calls"] != 0 or lifecycle["canonical_serialize_bytes"] != 0:
            raise ValueError("final report contains unexpected canonical_serialize throughput work")
        if lifecycle["same_mutation_epoch_duplicate_calls"] != 0:
            raise ValueError("final report contains same-epoch duplicate serialization work")
        if lifecycle["serialize_without_hash_calls"] != summary["operation_counters"]["observations"]:
            raise ValueError("final report serialization calls do not close against observations")
        expected_mode = "ostringstream" if variant == "control" else "reserve_backed"
        if reserve["mode"] != expected_mode:
            raise ValueError(f"final report {variant} reserve telemetry mode is incorrect")
        if expected_mode == "reserve_backed":
            if reserve["final_bytes"] > reserve["final_capacity"]:
                raise ValueError("final report reserve output exceeds final capacity")
            if reserve["unused_capacity"] != reserve["final_capacity"] - reserve["final_bytes"]:
                raise ValueError("final report reserve capacity closure is incorrect")
        elif any(reserve[key] != 0 for key in ("requested_capacity", "final_capacity", "growth_events", "unused_capacity")):
            raise ValueError("final report control contains reserve-only capacity telemetry")
        if lifecycle_reference is None:
            lifecycle_reference = lifecycle
        elif lifecycle != lifecycle_reference:
            raise ValueError("final report serialization lifecycle differs between repetitions")
        canonical_bytes.add(lifecycle["serialize_without_hash_bytes"])
        counters_equal = counters_equal and summary["operation_counters"] == operation_reference
        errors_zero = errors_zero and all(value == 0 for value in summary["error_counters"].values())
        if summary["error_counters"] != error_reference:
            counters_equal = False
    if len(canonical_bytes) != 1 or not counters_equal or not errors_zero:
        raise ValueError("repetition counters or canonical byte totals diverge")
    canonical_without_hash_bytes = next(iter(canonical_bytes))
    if operation_reference["observations"] != 9908:
        raise ValueError(f"expected 9,908 throughput observations, got {operation_reference['observations']}")
    experiment_reserve = [row["reserve_output"] for row in experiment]
    if not all(item == experiment_reserve[0] for item in experiment_reserve):
        raise ValueError("experiment reserve telemetry differs between repetitions")

    production_files = [
        "src/observation/serialization.cpp",
        "include/ygo/observation/performance_audit.hpp",
        "tools/ygo_m4_worker/json_protocol.cpp",
    ]
    starting_head = report["repository"]["starting_head"]
    source_check = subprocess.run(
        ["git", "diff", "--quiet", starting_head, "--", *production_files],
        check=False,
        capture_output=True,
        text=True,
    )
    if source_check.returncode != 0:
        raise ValueError("production optimization has not been reverted before finalization")

    report["status"] = "M4.3.5 REJECTED — NO MATERIAL BENEFIT"
    report["final_status"] = report["status"]
    report["repository"]["production_optimization_reverted"] = True
    current_status = subprocess.run(
        ["git", "status", "--short"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    report["repository"]["worktree_status"] = current_status
    report["repository"]["post_reversion_worktree_status"] = current_status
    report["repository"]["production_files_clean_against_starting_head"] = True
    final_head = subprocess.run(
        ["git", "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()
    if final_head != starting_head:
        raise ValueError(
            f"final HEAD changed during report finalization: expected {starting_head}, got {final_head}"
        )
    report["repository"]["final_head"] = final_head
    report["repository"]["reverted_production_files"] = production_files
    for variant in ("control", "experiment"):
        build = report["builds"][variant]
        build["measured_worker_sha256"] = build["worker_sha256"]
        build["measured_binary_hash_captured_at_run"] = True
        build["build_configuration"] = _inspect_build(
            Path(build["build_dir"]),
            Path(build["worker"]),
            build.get("ready", []),
            expected_variant=variant,
        )
        build["current_path_matches_measured"] = (
            build["build_configuration"]["worker_sha256_current_path"] == build["measured_worker_sha256"]
        )
    report["post_reversion_build"] = _inspect_build(
        Path("build/release-windows-zig"),
        Path("build/release-windows-zig/ygo_m4_worker.exe"),
        report["builds"]["control"].get("ready", []),
        expected_variant="control",
    )
    build_identity_keys = (
        "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID",
        "CMAKE_CXX_COMPILER_VERSION", "CMAKE_GENERATOR",
    )
    control_cmake = report["builds"]["control"]["build_configuration"]["cmake"]
    experiment_cmake = report["builds"]["experiment"]["build_configuration"]["cmake"]
    if any(control_cmake[key] != experiment_cmake[key] for key in build_identity_keys):
        raise ValueError("control and experiment CMake compiler/build identities differ")
    report["repository"]["binary_replacement_evidence"] = {}
    for variant in ("control", "experiment"):
        build = report["builds"][variant]
        if build["current_path_matches_measured"]:
            report["repository"]["binary_replacement_evidence"][variant] = {
                "current_path_matches_measured": True,
                "reason": "measured A/B binary remains at its recorded path",
            }
            continue
        current_config = build["build_configuration"]
        post_config = report["post_reversion_build"]
        intentional_replacement = (
            variant == "control"
            and report["repository"]["production_files_clean_against_starting_head"]
            and current_config["reserve_macro_present"] is False
            and current_config["shape_instrumentation_present"] is False
            and post_config["reserve_macro_present"] is False
            and post_config["shape_instrumentation_present"] is False
        )
        if not intentional_replacement:
            raise ValueError(
                f"unexpected current binary mismatch for {variant}; measured hash was not preserved"
            )
        report["repository"]["binary_replacement_evidence"][variant] = {
            "current_path_matches_measured": False,
            "reason": "intentional post-reversion ordinary Release rebuild; measured A/B hash is retained above",
            "post_reversion_source_clean": True,
            "ordinary_control_configuration": True,
        }
    report["equivalence"].update({
        "all_9908_observation_hashes_equal": observation_hash_count == 9908 and trace_hash_pass,
        "canonical_without_hash_bytes_equal": len(canonical_bytes) == 1,
        "canonical_serialize_bytes_equal": fixture_pass,
        "observation_hashes_equal": comparison.get("observation_hashes_equal") is True,
        "privacy_equal": gate_summary["privacy"]["status"] == "PASS" and gate_summary["privacy_control"]["status"] == "PASS",
        "operation_counters_equal": counters_equal,
        "error_counters_zero": errors_zero,
        "build_ready_identity_equal": ready_by_variant["control"] == ready_by_variant["experiment"],
        "build_compiler_identity_equal": True,
    })
    report["sidecar_validation"] = sidecar_validation
    report["canonical_workload"] = {
        "observations": operation_reference["observations"],
        "canonical_without_hash_bytes": canonical_without_hash_bytes,
        "mean_bytes_per_observation": canonical_without_hash_bytes / operation_reference["observations"],
    }
    report["gates"] = {
        key: value for key, value in gate_summary.items()
        if key not in {"schema", "freshness", "interactive_command_output_hashes"}
    }
    report["gate_freshness"] = gate_summary.get("freshness", "NOT_RECORDED")
    report["gate_output_hashes"] = gate_summary.get("interactive_command_output_hashes", "NOT_RECORDED")
    report["gate_summary_artifact"] = str(args.gate_summary.resolve())
    report["gate_artifact_validation"] = {
        "canonical_fixed_deck_regression": {
            "artifact": str(_resolve_artifact(fixed_gate.get("artifact"), "canonical fixed-deck regression")),
            "complete_games": fixed_results["complete_games"],
            "both_start_player_partitions": fixed_results["both_start_player_partitions"],
            "all_rows_pass": True,
        },
        "determinism": {
            "artifact": str(_resolve_artifact(determinism_gate.get("artifact"), "determinism")),
            "starting_player_partitions": determinism_results["starting_player_partitions"],
            "independent_process_match": True,
            "semantic_action_reexecution_match": True,
            "crlf_semantic_replay_match": True,
        },
        "reserve_output_contract": {
            "artifact": str(reserve_contract_path),
            "mode": reserve_contract["mode"],
            "calls": reserve_contract["calls"],
            "final_bytes": reserve_contract["final_bytes"],
        },
    }
    report["timing_summary"] = {
        "control": {
            "worker_local_simulation_us": _stats([row["worker_local_simulation_us"] for row in control]),
            "games_per_second": _stats([row["games_per_second"] for row in control]),
            "serializer_us": _stats([row["serializer_us"] for row in control]),
            "hash_us": _stats([row["hash_us"] for row in control]),
            "outer_observation_us": _stats([row["outer_observation_us"] for row in control]),
        },
        "experiment": {
            "worker_local_simulation_us": _stats([row["worker_local_simulation_us"] for row in experiment]),
            "games_per_second": _stats([row["games_per_second"] for row in experiment]),
            "serializer_us": _stats([row["serializer_us"] for row in experiment]),
            "hash_us": _stats([row["hash_us"] for row in experiment]),
            "outer_observation_us": _stats([row["outer_observation_us"] for row in experiment]),
        },
        "worker_speedup_percent": timing["worker_speedup_percent"],
        "serializer_speedup_percent": timing["serializer_speedup_percent"],
        "paired_worker_improvements": timing["paired_worker_improvements"],
        "paired_serializer_improvements": timing["paired_serializer_improvements"],
        "materiality_rule_pass": False,
    }
    experiment_reserve = [row["reserve_output"] for row in experiment]
    report["reserve_telemetry"] = {
        "mode": "reserve_backed",
        "calls": experiment_reserve[0]["calls"],
        "requested_capacity": experiment_reserve[0]["requested_capacity"],
        "final_bytes": experiment_reserve[0]["final_bytes"],
        "final_capacity": experiment_reserve[0]["final_capacity"],
        "growth_events": experiment_reserve[0]["growth_events"],
        "unused_capacity": experiment_reserve[0]["unused_capacity"],
        "identical_across_experiment_repetitions": all(item == experiment_reserve[0] for item in experiment_reserve),
    }
    report["recommendation"] = (
        "Do not carry the reserve-backed streambuf forward. If another serializer experiment is authorized, "
        "isolate stream construction with a tighter measured capacity model; do not infer a speedup or begin M5."
    )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    args.output_markdown.parent.mkdir(parents=True, exist_ok=True)
    args.output_markdown.write_text(_render_markdown(report), encoding="utf-8")
    print(json.dumps({"status": report["status"], "gates": "PASS", "materiality": False}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
