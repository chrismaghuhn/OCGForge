"""Compare deterministic Debug and Release worker semantics for M4.3.3.

This is a characterization harness, not a production benchmark path.  It
explicitly supplies one trace output path per job because ``persist_trace``
alone does not request native trace-file persistence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Mapping

if __package__ in (None, ""):
    _REPO_ROOT = Path(__file__).resolve().parents[2]
    if str(_REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(_REPO_ROOT))

from tools.m4.benchmark import PersistentWorkerPool
from tools.m4.job_generation import derive_job_with_options
from tools.m4.report import SEMANTIC_RESULT_FIELDS, validate_complete_results


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_text(data: str) -> str:
    return _sha256_bytes(data.encode("utf-8"))


def _normalise_trace_records(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if not records:
        raise ValueError("trace has no canonical records")
    manifest = dict(records[0])
    for key in ("build_type", "compiler_identity"):
        if key in manifest:
            manifest[key] = "<build-dependent>"
    return [manifest, *records[1:]]


def _canonical_json_lines(records: list[dict[str, Any]]) -> str:
    return "".join(
        json.dumps(record, ensure_ascii=False, separators=(",", ":")) + "\n"
        for record in records
    )


def _read_trace(path: Path) -> dict[str, Any]:
    raw = path.read_bytes()
    lines = raw.decode("utf-8").splitlines()
    json_lines: list[str] = []
    footer: dict[str, str] = {}
    for line in lines:
        if line.startswith("# "):
            key, separator, value = line[2:].partition("=")
            if separator:
                footer[key] = value
        elif line:
            json_lines.append(line)
    records = [json.loads(line) for line in json_lines]
    if not all(isinstance(record, dict) for record in records):
        raise ValueError(f"trace contains a non-object record: {path}")
    typed_records = [record for record in records if isinstance(record, dict)]
    normalised = _normalise_trace_records(typed_records)
    steps = typed_records[1:]
    return {
        "path": str(path),
        "bytes": len(raw),
        "sha256": _sha256_bytes(raw),
        "step_bytes_sha256": _sha256_text("".join(json_lines[1:])),
        "manifest": typed_records[0],
        "steps": steps,
        "observation_hashes": [step.get("observation_hash", "") for step in steps],
        "footer": footer,
        "normalized_sha256": _sha256_text(_canonical_json_lines(normalised)),
    }


def _run_build(
    label: str,
    executable: Path,
    output_dir: Path,
    master_seed: int,
    games: int,
    max_steps: int,
) -> dict[str, Any]:
    build_dir = output_dir / label
    trace_dir = build_dir / "traces"
    trace_dir.mkdir(parents=True, exist_ok=True)
    jobs = []
    for index in range(games):
        job = derive_job_with_options(
            master_seed,
            index,
            max_steps=max_steps,
            mode="conformance",
            observation_mode="full",
            instrumentation=False,
            persist_trace=True,
        )
        job["trace_output"] = str((trace_dir / f"{job['job_id']}.jsonl").resolve())
        jobs.append(job)

    pool = PersistentWorkerPool(
        executable,
        worker_count=1,
        output_dir=build_dir / "workers",
        result_timeout_seconds=120.0,
    )
    try:
        pool.start()
        ready = pool.ready_messages[0] if pool.ready_messages else {}
        results = pool.run(jobs, require_primary_integrity=True)
        validate_complete_results(
            results,
            [job["job_id"] for job in jobs],
            pool.last_run_metadata,
            require_trace_hash=True,
        )
        metadata = dict(pool.last_run_metadata)
    finally:
        pool.close()

    by_id = {result["job_id"]: result for result in results}
    jobs_out: list[dict[str, Any]] = []
    for job in jobs:
        job_id = job["job_id"]
        trace = _read_trace(Path(job["trace_output"]))
        result = by_id[job_id]
        jobs_out.append(
            {
                "job_id": job_id,
                "result_semantics": {
                    field: result.get(field) for field in SEMANTIC_RESULT_FIELDS
                },
                "trace_hash": result.get("trace_hash"),
                "trace": trace,
            }
        )
    return {
        "worker_executable": str(executable.resolve()),
        "ready": ready,
        "metadata": metadata,
        "jobs": jobs_out,
    }


def _compare(debug: Mapping[str, Any], release: Mapping[str, Any]) -> dict[str, Any]:
    debug_jobs = {row["job_id"]: row for row in debug["jobs"]}
    release_jobs = {row["job_id"]: row for row in release["jobs"]}
    job_ids = sorted(debug_jobs)
    if job_ids != sorted(release_jobs):
        raise ValueError("Debug and Release job ID sets differ")

    rows: list[dict[str, Any]] = []
    semantic_ok = True
    for job_id in job_ids:
        left = debug_jobs[job_id]
        right = release_jobs[job_id]
        left_trace = left["trace"]
        right_trace = right["trace"]
        semantic_fields_equal = left["result_semantics"] == right["result_semantics"]
        observation_hashes_equal = left_trace["observation_hashes"] == right_trace["observation_hashes"]
        steps_equal = left_trace["steps"] == right_trace["steps"]
        normalized_trace_equal = left_trace["normalized_sha256"] == right_trace["normalized_sha256"]
        step_bytes_equal = left_trace["step_bytes_sha256"] == right_trace["step_bytes_sha256"]
        raw_trace_hash_equal = left["trace_hash"] == right["trace_hash"]
        row_ok = (
            semantic_fields_equal
            and observation_hashes_equal
            and steps_equal
            and normalized_trace_equal
            and step_bytes_equal
        )
        semantic_ok = semantic_ok and row_ok
        rows.append(
            {
                "job_id": job_id,
                "pass": row_ok,
                "semantic_fields_equal": semantic_fields_equal,
                "observation_hashes_equal": observation_hashes_equal,
                "observation_count": len(left_trace["observation_hashes"]),
                "trace_steps_equal": steps_equal,
                "trace_step_bytes_equal": step_bytes_equal,
                "normalized_trace_hash_equal": normalized_trace_equal,
                "raw_trace_hash_equal": raw_trace_hash_equal,
                "debug_trace_hash": left["trace_hash"],
                "release_trace_hash": right["trace_hash"],
                "debug_trace_sha256": left_trace["sha256"],
                "release_trace_sha256": right_trace["sha256"],
                "debug_normalized_trace_sha256": left_trace["normalized_sha256"],
                "release_normalized_trace_sha256": right_trace["normalized_sha256"],
            }
        )
    return {
        "pass": semantic_ok,
        "raw_trace_hash_equal": all(row["raw_trace_hash_equal"] for row in rows),
        "normalized_trace_hash_equal": all(row["normalized_trace_hash_equal"] for row in rows),
        "observation_hashes_equal": all(row["observation_hashes_equal"] for row in rows),
        "rows": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debug-worker", required=True, type=Path)
    parser.add_argument("--release-worker", required=True, type=Path)
    parser.add_argument("--master-seed", required=True, type=int)
    parser.add_argument("--games", required=True, type=int)
    parser.add_argument("--max-steps", required=True, type=int)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    debug = _run_build(
        "debug",
        args.debug_worker,
        args.output_dir,
        args.master_seed,
        args.games,
        args.max_steps,
    )
    release = _run_build(
        "release",
        args.release_worker,
        args.output_dir,
        args.master_seed,
        args.games,
        args.max_steps,
    )
    comparison = _compare(debug, release)
    report = {
        "schema": "ocgforge.m4.build_mode_equivalence.v1",
        "workload": {
            "master_seed": args.master_seed,
            "games": args.games,
            "workers": 1,
            "max_steps": args.max_steps,
            "mode": "conformance",
            "observation_mode": "full",
            "trace_persistence": True,
        },
        "debug": debug,
        "release": release,
        "comparison": comparison,
    }
    destination = args.output or (args.output_dir / "build_mode_equivalence.json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(comparison, ensure_ascii=False, sort_keys=True))
    return 0 if comparison["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
