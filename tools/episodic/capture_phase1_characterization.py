"""Normalize the immutable Phase-1 pre-refactor artifacts.

This module is deliberately limited to an artifact transformation.  It reads
the raw trace/result files and their manifest from the immutable capture
worktree; it never constructs a probe, invokes a worker, or makes a gameplay
decision.  The candidate-domain digest is a test-side witness only and is not
part of EngineTrace, PlayerObservation, or the semantic gameplay hash.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any, Iterable


CHARACTERIZATION_SCHEMA = "ocgforge.episodic.phase1.characterization.v1"
PROVENANCE_SCHEMA = "ocgforge.episodic.phase1.characterization.provenance.v1"
COLLECTOR_VERSION = "capture_phase1_characterization.v1"
CANDIDATE_DOMAIN_SCHEMA = "ocgforge.episodic.phase1.candidate_domain_digest.v1"
BASELINE_COMMIT = "72c29009f107a2ebb172d85de1c70b38d2f007d8"
FULL_GAME_COUNT = 16
FULL_GAME_SEEDS = (1, 2, 3, 4)

FULL_TRACE_PATTERN = re.compile(r"^seed-(?P<seed>[1-4])-(?P<seat>normal|mirror)-start-(?P<start>[01])\.jsonl$")
COMMAND_VALUE_PATTERN = re.compile(r"--(?P<name>seed|starting-player|max-steps)\s+(?P<value>\d+)")
ARTIFACT_PREFIX = "artifacts/episodic/phase1/pre-refactor/"


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _canonical_json(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True).encode("utf-8")


def _write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _normalize_path(value: str) -> str:
    return value.replace("\\", "/")


def _manifest_artifact_path(raw_root: Path, manifest_path: str) -> Path:
    normalized = _normalize_path(manifest_path)
    if normalized.startswith(ARTIFACT_PREFIX):
        return raw_root.joinpath(*normalized[len(ARTIFACT_PREFIX) :].split("/"))
    return raw_root.joinpath(*normalized.split("/"))


def _load_and_verify_manifest(raw_root: Path, require_baseline: bool) -> dict[str, Any]:
    manifest_path = raw_root / "raw-artifact-manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"raw artifact manifest is missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != "ocgforge.episodic.phase1.raw_artifact_manifest.v1":
        raise ValueError("unexpected raw artifact manifest schema")
    if require_baseline and manifest.get("source_base_commit") != BASELINE_COMMIT:
        raise ValueError("raw artifacts are not bound to the immutable Phase-1 baseline")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ValueError("raw artifact manifest has no artifacts")
    if manifest.get("artifact_count") != len(artifacts):
        raise ValueError("raw artifact manifest count does not match its artifact list")
    for artifact in artifacts:
        relative = _normalize_path(str(artifact["path"]))
        path = _manifest_artifact_path(raw_root, relative)
        if not path.is_file():
            raise ValueError(f"raw artifact is missing: {relative}")
        actual_size = path.stat().st_size
        actual_hash = _sha256_file(path)
        if actual_size != artifact["size_bytes"] or actual_hash != artifact["sha256"]:
            raise ValueError(f"raw artifact integrity mismatch: {relative}")
    for probe in manifest.get("negative_probes", []):
        output_path = _manifest_artifact_path(raw_root, _normalize_path(probe["output_path"]))
        stderr_path = _manifest_artifact_path(raw_root, _normalize_path(probe["stderr_path"]))
        if bool(probe.get("output_present")) != output_path.is_file():
            raise ValueError(f"negative probe output-presence mismatch: {probe.get('job_id')}")
        if not stderr_path.is_file() or _sha256_file(stderr_path) != probe.get("stderr_sha256"):
            raise ValueError(f"negative probe diagnostic integrity mismatch: {probe.get('job_id')}")
    return manifest


def _normalized_manifest(manifest: dict[str, Any]) -> dict[str, Any]:
    normalized = json.loads(json.dumps(manifest))
    normalized["capture_worktree"] = "<immutable-worktree-a>"
    for artifact in normalized.get("artifacts", []):
        artifact["path"] = _normalize_path(artifact["path"])
    for probe in normalized.get("negative_probes", []):
        for name in ("output_path", "stderr_path"):
            if name in probe:
                probe[name] = _normalize_path(probe[name])
    return normalized


def _read_trace(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]], dict[str, Any], dict[str, str]]:
    manifest: dict[str, Any] | None = None
    records: list[dict[str, Any]] = []
    summary: dict[str, Any] | None = None
    comments: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line:
            continue
        if line.startswith("# m3_summary="):
            summary = json.loads(line[len("# m3_summary=") :])
        elif line.startswith("# ") and "=" in line:
            name, value = line[2:].split("=", 1)
            comments[name] = value
        else:
            value = json.loads(line)
            if manifest is None and "trace_schema_version" in value:
                manifest = value
            else:
                records.append(value)
    if manifest is None or summary is None:
        raise ValueError(f"trace is missing manifest or summary: {path.name}")
    if manifest.get("trace_schema_version") != "ygo.engine_trace.v2":
        raise ValueError(f"unexpected trace schema: {path.name}")
    return manifest, records, summary, comments


def _continuation_kind(record: dict[str, Any]) -> str | None:
    continuation_id = str(record.get("continuation_id") or "")
    if continuation_id:
        return continuation_id.rsplit(".", 1)[-1]
    if record.get("continuation_state_hash") or record.get("continuation_steps"):
        return str(record.get("decision_request_kind") or "") or None
    return None


def _candidate_domain_digest(keys: list[str]) -> str:
    payload = {"schema_version": CANDIDATE_DOMAIN_SCHEMA, "ordered_semantic_keys": keys}
    return _sha256_bytes(_canonical_json(payload))


def _failure_counters(summary: dict[str, Any]) -> dict[str, int]:
    names = (
        "unsupported_count",
        "retry_count",
        "automatic_decision_count",
        "candidate_truncation_count",
        "core_error_count",
        "worker_error_count",
    )
    return {name: int(summary.get(name, 0) or 0) for name in names}


def _record(
    job_id: str,
    source_base_commit: str,
    seed: int,
    seat_assignment: str,
    starting_player: int,
    ordered_record_index: int,
    trace_record: dict[str, Any],
    gameplay_hash: str | None,
    trace_hash: str | None,
    failure_code: str,
    error_message_category: str | None,
    failure_counters: dict[str, int],
) -> dict[str, Any]:
    keys = [str(key) for key in trace_record.get("ordered_candidate_semantic_keys", [])]
    has_decision = bool(trace_record.get("decision_id"))
    selected = trace_record.get("selected_semantic_key") or None
    return {
        "job_id": job_id,
        "source_base_commit": source_base_commit,
        "seed": seed,
        "seat_assignment": seat_assignment,
        "starting_player": starting_player,
        "ordered_record_index": ordered_record_index,
        "trace_record_identity": {
            "step_index": trace_record.get("step_index"),
            "decision_index": trace_record.get("decision_index"),
            "engine_step_index": trace_record.get("engine_step_index"),
        },
        "decision_id": trace_record.get("decision_id") or None,
        "decision_kind": trace_record.get("decision_request_kind") or None,
        "acting_player": trace_record.get("perspective_player") if has_decision else None,
        "player_to_act": trace_record.get("player_to_act") if has_decision else None,
        "engine_step_index": trace_record.get("engine_step_index"),
        "raw_message_sha256": trace_record.get("raw_message_sha256"),
        "ordered_semantic_keys": keys,
        "candidate_count": int(trace_record.get("complete_candidate_count", len(keys)) or 0),
        "candidate_semantic_keys": keys,
        "candidate_domain_digest_schema": CANDIDATE_DOMAIN_SCHEMA,
        "candidate_domain_digest": _candidate_domain_digest(keys),
        "selected_semantic_key": selected,
        "observation_schema": trace_record.get("observation_schema") or None,
        "observation_hash": trace_record.get("observation_hash") or None,
        "continuation_id": trace_record.get("continuation_id") or None,
        "continuation_kind": _continuation_kind(trace_record),
        "continuation_step": (
            int(trace_record.get("continuation_step", 0))
            if trace_record.get("continuation_id")
            else None
        ),
        "continuation_state_hash": trace_record.get("continuation_state_hash") or None,
        "submitted_response_sha256": trace_record.get("final_engine_response_hash") or None,
        "engine_advanced": bool(trace_record.get("engine_advanced", False)),
        "trace_hash": trace_hash,
        "semantic_gameplay_hash": gameplay_hash,
        "terminal": bool(trace_record.get("terminal", False)),
        "winner": trace_record.get("winner") if trace_record.get("terminal") else None,
        "win_reason": trace_record.get("win_reason") if trace_record.get("terminal") else None,
        "failure_code": failure_code,
        "error_message_category": error_message_category,
        "failure_counters": failure_counters,
    }


def _trace_job(
    path: Path,
    raw_root: Path,
    manifest: dict[str, Any],
    job_id: str,
    seed: int,
    seat_assignment: str,
    starting_player: int,
) -> dict[str, Any]:
    trace_manifest, trace_records, m3_summary, comments = _read_trace(path)
    expected_decks = manifest["rules"]["locked_deck_hashes"]
    if seat_assignment == "mirror":
        expected_decks = list(reversed(expected_decks))
    if trace_manifest.get("fixture_deck_hashes") != expected_decks:
        raise ValueError(f"seat assignment mismatch in {path.name}")
    if trace_manifest.get("starting_player") != starting_player:
        raise ValueError(f"starting player mismatch in {path.name}")
    gameplay_hash = comments.get("semantic_gameplay_hash") or m3_summary.get("semantic_gameplay_hash")
    trace_hash = comments.get("trace_hash") or m3_summary.get("trace_hash")
    if trace_hash == "":
        trace_hash = None
    if gameplay_hash == "":
        gameplay_hash = None
    failure_code = "" if bool(m3_summary.get("terminal")) else "nonterminal"
    error_category = None if not failure_code else failure_code
    counters = _failure_counters(m3_summary)
    records = [
        _record(
            job_id,
            manifest["source_base_commit"],
            seed,
            seat_assignment,
            starting_player,
            index,
            trace_record,
            gameplay_hash,
            trace_hash,
            failure_code,
            error_category,
            counters,
        )
        for index, trace_record in enumerate(trace_records)
    ]
    semantic_action_count = sum(1 for record in records if record["selected_semantic_key"] is not None)
    decision_records = [record for record in records if record["decision_id"] is not None]
    summary = {
        "terminal": bool(m3_summary.get("terminal")),
        "winner": m3_summary.get("winner") if m3_summary.get("terminal") else None,
        "win_reason": m3_summary.get("win_reason") if m3_summary.get("terminal") else None,
        "engine_steps": int(m3_summary.get("engine_steps", 0) or 0),
        "interactive_decisions": int(m3_summary.get("interactive_decisions", len(decision_records)) or 0),
        "semantic_action_count": semantic_action_count,
        "continuation_intermediate_steps": int(m3_summary.get("continuation_intermediate_steps", 0) or 0),
        "candidate_count_max": int(m3_summary.get("candidate_count_max", 0) or 0),
        "candidate_count_mean": m3_summary.get("candidate_count_mean", 0),
        "candidate_record_count": len(decision_records),
        "candidate_count_total": sum(record["candidate_count"] for record in decision_records),
        "turns": int(m3_summary.get("turns", 0) or 0),
        "battle_command_count": int(m3_summary.get("battle_command_count", 0) or 0),
        "visible_life_points_event_count": int(m3_summary.get("visible_life_points_event_count", 0) or 0),
        "visible_destroyed_event_count": int(m3_summary.get("visible_destroyed_event_count", 0) or 0),
        "visible_win_event_count": int(m3_summary.get("visible_win_event_count", 0) or 0),
        "observation_entity_total": int(m3_summary.get("observation_entity_total", 0) or 0),
        "observation_event_total": int(m3_summary.get("observation_event_total", 0) or 0),
        "failure_counters": counters,
        "failure_code": failure_code,
        "error_message_category": error_category,
        "error_message": None if not failure_code else "canonical simulation did not reach terminal state before max_steps",
        "pass": bool(m3_summary.get("terminal")) and not any(counters.values()),
        "output_present": True,
        "exit_code": 0,
        "semantic_gameplay_hash": gameplay_hash,
        "trace_hash": trace_hash,
    }
    return {
        "job_id": job_id,
        "seed": seed,
        "seat_assignment": seat_assignment,
        "starting_player": starting_player,
        "deck_hashes": list(trace_manifest["fixture_deck_hashes"]),
        "artifact_name": path.name,
        "records": records,
        "summary": summary,
    }


def _command_values(command: str) -> dict[str, int]:
    return {match.group("name"): int(match.group("value")) for match in COMMAND_VALUE_PATTERN.finditer(command)}


def _forced_job(raw_root: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    probes = {probe["job_id"]: probe for probe in manifest.get("negative_probes", [])}
    probe = probes.get("shared-forced-unsupported")
    if probe is None:
        raise ValueError("forced-unsupported negative probe is missing from raw manifest")
    values = _command_values(probe["command"])
    stderr_path = _manifest_artifact_path(raw_root, _normalize_path(probe["stderr_path"]))
    diagnostic = stderr_path.read_text(encoding="utf-8").strip()
    summary = {
        "terminal": False,
        "winner": None,
        "win_reason": None,
        "engine_steps": 0,
        "interactive_decisions": 0,
        "semantic_action_count": 0,
        "continuation_intermediate_steps": 0,
        "candidate_count_max": 0,
        "candidate_count_mean": 0,
        "candidate_record_count": 0,
        "candidate_count_total": 0,
        "turns": 0,
        "battle_command_count": 0,
        "visible_life_points_event_count": 0,
        "visible_destroyed_event_count": 0,
        "visible_win_event_count": 0,
        "observation_entity_total": 0,
        "observation_event_total": 0,
        "failure_counters": {
            "unsupported_count": 1,
            "retry_count": 0,
            "automatic_decision_count": 0,
            "candidate_truncation_count": 0,
            "core_error_count": 0,
            "worker_error_count": 0,
        },
        "failure_code": str(probe["failure_code"]),
        "error_message_category": str(probe["diagnostic_code"]),
        "error_message": str(probe["error"]),
        "diagnostic": diagnostic,
        "pass": False,
        "output_present": bool(probe["output_present"]),
        "exit_code": int(probe["exit_code"]),
        "semantic_gameplay_hash": None,
        "trace_hash": None,
    }
    return {
        "job_id": "shared-forced-unsupported",
        "seed": values.get("seed", 2),
        "seat_assignment": "normal",
        "starting_player": values.get("starting-player", 0),
        "deck_hashes": list(manifest["rules"]["locked_deck_hashes"]),
        "artifact_name": Path(_normalize_path(probe["output_path"])).name,
        "records": [],
        "summary": summary,
    }


def _full_specs() -> Iterable[tuple[int, str, int]]:
    for seed in FULL_GAME_SEEDS:
        for seat_assignment in ("normal", "mirror"):
            for starting_player in (0, 1):
                yield seed, seat_assignment, starting_player


def _job_id(seed: int, seat_assignment: str, starting_player: int) -> str:
    return f"full-seed-{seed}-{seat_assignment}-start-{starting_player}"


def _build_jobs(raw_root: Path, manifest: dict[str, Any]) -> list[dict[str, Any]]:
    full_dir = raw_root / "full-games"
    jobs: list[dict[str, Any]] = []
    for seed, seat_assignment, starting_player in _full_specs():
        name = f"seed-{seed}-{seat_assignment}-start-{starting_player}.jsonl"
        path = full_dir / name
        if not path.is_file():
            raise ValueError(f"canonical full-game trace is missing: {name}")
        jobs.append(_trace_job(path, raw_root, manifest, _job_id(seed, seat_assignment, starting_player), seed, seat_assignment, starting_player))
    shared_full = raw_root / "shared" / "full.jsonl"
    shared_nonterminal = raw_root / "shared" / "nonterminal.jsonl"
    jobs.append(_trace_job(shared_full, raw_root, manifest, "shared-full", 2, "normal", 0))
    jobs.append(_trace_job(shared_nonterminal, raw_root, manifest, "shared-nonterminal", 2, "normal", 0))
    jobs.append(_forced_job(raw_root, manifest))
    return jobs


def _maximum_witness(jobs: list[dict[str, Any]]) -> dict[str, Any]:
    candidates = [record for job in jobs for record in job["records"] if record["candidate_count"] > 0]
    if not candidates:
        raise ValueError("characterization corpus has no candidate domains")
    maximum = max(record["candidate_count"] for record in candidates)
    record = next(record for record in candidates if record["candidate_count"] == maximum)
    return {
        "job_id": record["job_id"],
        "seed": record["seed"],
        "seat_assignment": record["seat_assignment"],
        "starting_player": record["starting_player"],
        "engine_step_index": record["engine_step_index"],
        "decision_id": record["decision_id"],
        "decision_kind": record["decision_kind"],
        "raw_message_sha256": record["raw_message_sha256"],
        "continuation_id": record["continuation_id"],
        "continuation_kind": record["continuation_kind"],
        "continuation_step": record["continuation_step"],
        "candidate_count": record["candidate_count"],
        "candidate_semantic_keys": record["candidate_semantic_keys"],
        "semantic_key_vector": record["candidate_semantic_keys"],
        "candidate_domain_digest_schema": record["candidate_domain_digest_schema"],
        "candidate_domain_digest": record["candidate_domain_digest"],
        "observation_hash": record["observation_hash"],
        "selected_semantic_key": record["selected_semantic_key"],
        "ordered_record_index": record["ordered_record_index"],
    }


def _summary(jobs: list[dict[str, Any]]) -> dict[str, Any]:
    records = [record for job in jobs for record in job["records"]]
    candidate_records = [record for record in records if record["candidate_count"] > 0]
    return {
        "job_count": len(jobs),
        "terminal_job_count": sum(1 for job in jobs if job["summary"]["terminal"]),
        "nonterminal_job_count": sum(1 for job in jobs if not job["summary"]["terminal"]),
        "failure_job_count": sum(1 for job in jobs if job["summary"]["failure_code"]),
        "record_count": len(records),
        "candidate_record_count": len(candidate_records),
        "candidate_count_total": sum(record["candidate_count"] for record in candidate_records),
        "candidate_count_max": max(record["candidate_count"] for record in candidate_records),
    }


def _collector_descriptor(source_hash: str) -> dict[str, str]:
    return {
        "version": COLLECTOR_VERSION,
        "source_path": "tools/episodic/capture_phase1_characterization.py",
        "source_sha256": source_hash,
    }


def collect_characterization(
    raw_root: Path,
    output_path: Path | None = None,
    *,
    require_baseline: bool = True,
) -> dict[str, Any]:
    raw_root = Path(raw_root)
    manifest = _load_and_verify_manifest(raw_root, require_baseline=require_baseline)
    jobs = _build_jobs(raw_root, manifest)
    full_report = json.loads((raw_root / "full-games" / "full_fixed_deck_results.json").read_text(encoding="utf-8"))
    if full_report.get("complete_games") != FULL_GAME_COUNT or not full_report.get("both_start_player_partitions"):
        raise ValueError("full-game report does not prove the locked 16-game matrix")
    source_hash = _sha256_file(Path(__file__))
    result = {
        "schema_version": CHARACTERIZATION_SCHEMA,
        "source_base_commit": manifest["source_base_commit"],
        "collector": _collector_descriptor(source_hash),
        "candidate_domain_digest_schema": CANDIDATE_DOMAIN_SCHEMA,
        "build": manifest["build"],
        "rules": manifest["rules"],
        "locked_corpus": {
            "full_game_count": FULL_GAME_COUNT,
            "full_game_seeds": list(FULL_GAME_SEEDS),
            "seat_assignments": ["normal", "mirror"],
            "starting_players": [0, 1],
            "max_process_steps": 2200,
            "shared_full": {"seed": 2, "starting_player": 0, "max_process_steps": 1800},
            "shared_nonterminal": {"seed": 2, "starting_player": 0, "max_process_steps": 1},
        },
        "jobs": jobs,
        "maximum_candidate_witness": _maximum_witness(jobs),
        "summary": _summary(jobs),
    }
    if output_path is not None:
        _write_json(Path(output_path), result)
    return result


def make_provenance(raw_root: Path, fixture_path: Path) -> dict[str, Any]:
    manifest = _load_and_verify_manifest(Path(raw_root), require_baseline=True)
    normalized_manifest = _normalized_manifest(manifest)
    fixture_path = Path(fixture_path)
    return {
        "schema_version": PROVENANCE_SCHEMA,
        "characterization_schema_version": CHARACTERIZATION_SCHEMA,
        "source_base_commit": manifest["source_base_commit"],
        "fixture_path": "tests/episodic/fixtures/phase1-pre-extraction-characterization.json",
        "fixture_sha256": _sha256_file(fixture_path),
        "collector_source_path": "tools/episodic/capture_phase1_characterization.py",
        "collector_source_sha256": _sha256_file(Path(__file__)),
        "collector_version": COLLECTOR_VERSION,
        "collector_arguments": {
            "raw_root": "<immutable-worktree-a>/artifacts/episodic/phase1/pre-refactor",
            "fixture": "tests/episodic/fixtures/phase1-pre-extraction-characterization.json",
            "provenance": "tests/episodic/fixtures/phase1-pre-extraction-characterization.provenance.json",
            "require_baseline": True,
        },
        "raw_artifact_manifest": normalized_manifest,
    }


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-root", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    args = parser.parse_args(arguments)
    collect_characterization(args.raw_root, args.fixture, require_baseline=True)
    _write_json(args.provenance, make_provenance(args.raw_root, args.fixture))
    print(json.dumps({"fixture": str(args.fixture), "provenance": str(args.provenance)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
