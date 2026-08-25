"""Run and verify the M4.3.4 Release serialization-shape workload.

This is an audit harness only. It runs a conformance pass against the frozen
M4.3.3 Release traces, then the exact FULL/THROUGHPUT workload and records the
native shape sidecars emitted on worker stderr.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from typing import Any, Mapping

_REPO_ROOT = Path(__file__).resolve().parents[2]
if __package__ in (None, ""):
    if str(_REPO_ROOT) not in sys.path:
        sys.path.insert(0, str(_REPO_ROOT))

from tools.m4.benchmark import PersistentWorkerPool
from tools.m4.job_generation import derive_job_with_options
from tools.m4.report import SEMANTIC_RESULT_FIELDS, validate_complete_results


SHAPE_PREFIX = "M4_SERIALIZATION_SHAPE "
PERFORMANCE_PREFIX = "M4_PERFORMANCE_AUDIT "
LIFECYCLE_PREFIX = "M4_SERIALIZATION_LIFECYCLE "
SHAPE_SCHEMA = "ocgforge.m4.serialization_shape.v1"
SHAPE_TYPE = "serialization_shape"
PERFORMANCE_SCHEMA = "ocgforge.m4.performance_audit.v1"
PERFORMANCE_TYPE = "performance_audit"
LIFECYCLE_SCHEMA = "ocgforge.m4.serialization_lifecycle.v1"
LIFECYCLE_TYPE = "serialization_lifecycle"
RUN_SCHEMA = "ocgforge.m4.m4_3_4_shape_run.v1"
IDENTITY_EXCLUDED_PATHS = {
    "docs/m4/M4_3_4_SERIALIZATION_SHAPE_AUDIT.md",
    "docs/m4/m4_3_4_serialization_shape_audit.json",
}


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _read_trace(path: Path) -> dict[str, Any]:
    raw = path.read_bytes()
    lines = raw.decode("utf-8").splitlines()
    records = [json.loads(line) for line in lines if line and not line.startswith("# ")]
    if not records or not all(isinstance(record, dict) for record in records):
        raise ValueError(f"trace has no canonical object records: {path}")
    footer: dict[str, str] = {}
    for line in lines:
        if line.startswith("# "):
            key, separator, value = line[2:].partition("=")
            if separator:
                footer[key] = value
    steps = records[1:]
    return {
        "path": str(path.resolve()),
        "bytes": len(raw),
        "sha256": _sha256_bytes(raw),
        "steps": steps,
        "observation_hashes": [step.get("observation_hash", "") for step in steps],
        "footer": footer,
    }


def _repository_identity() -> dict[str, Any]:
    def git(*arguments: str) -> bytes:
        completed = subprocess.run(
            ["git", *arguments],
            cwd=_REPO_ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return completed.stdout

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
    untracked_paths = [
        line.decode("utf-8", errors="strict")
        for line in git("ls-files", "--others", "--exclude-standard").splitlines()
        if line
    ]
    untracked_files = []
    for relative_path in untracked_paths:
        if relative_path.replace("\\", "/") in IDENTITY_EXCLUDED_PATHS:
            continue
        path = _REPO_ROOT / relative_path
        if path.is_file():
            untracked_files.append(
                {
                    "path": relative_path,
                    "sha256": _sha256_bytes(path.read_bytes()),
                    "bytes": path.stat().st_size,
                }
            )
    return {
        "repository_root": str(_REPO_ROOT.resolve()),
        "git_head": git("rev-parse", "HEAD").decode("ascii").strip(),
        "tracked_diff_sha256": _sha256_bytes(tracked_diff),
        "status_sha256": _sha256_bytes(status),
        "status_lines": status_lines,
        "untracked_files": untracked_files,
    }


def _run_focused_equivalence(worker: Path) -> dict[str, Any]:
    test_executable = worker.parent / "observation_builder_test.exe"
    if not test_executable.exists():
        raise ValueError(f"focused shape equivalence executable is missing: {test_executable}")
    completed = subprocess.run(
        [str(test_executable)],
        cwd=_REPO_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=240.0,
    )
    stdout = completed.stdout.decode("utf-8", errors="replace")
    stderr = completed.stderr.decode("utf-8", errors="replace")
    fixture_lines = [line for line in stdout.splitlines() if line.startswith("m4_3_4_shape_fixture=")]
    marker = "m4_3_4_shape_equivalence=ok"
    passed = completed.returncode == 0 and marker in stdout and len(fixture_lines) == 6
    if not passed:
        raise ValueError(
            "focused shape equivalence failed: "
            f"returncode={completed.returncode}, fixtures={len(fixture_lines)}, stderr={stderr[-2000:]}"
        )
    return {
        "pass": True,
        "executable": str(test_executable.resolve()),
        "executable_sha256": _sha256_bytes(test_executable.read_bytes()),
        "fixture_count": len(fixture_lines),
        "fixture_lines": fixture_lines,
        "stdout_sha256": _sha256_bytes(completed.stdout),
        "stderr_sha256": _sha256_bytes(completed.stderr),
        "timing_is_diagnostic_only": True,
    }


def _run_mode(
    *,
    label: str,
    executable: Path,
    output_dir: Path,
    master_seed: int,
    games: int,
    max_steps: int,
    mode: str,
    execution_identity: Mapping[str, Any],
) -> dict[str, Any]:
    mode_dir = output_dir / label
    trace_dir = mode_dir / "traces"
    trace_dir.mkdir(parents=True, exist_ok=True)
    jobs: list[dict[str, Any]] = []
    for index in range(games):
        job = derive_job_with_options(
            master_seed,
            index,
            max_steps=max_steps,
            mode=mode,
            observation_mode="full",
            instrumentation=False,
            persist_trace=mode == "conformance",
        )
        if mode == "conformance":
            job["trace_output"] = str((trace_dir / f"{job['job_id']}.jsonl").resolve())
        jobs.append(job)

    pool = PersistentWorkerPool(
        executable,
        worker_count=1,
        output_dir=mode_dir / "workers",
        result_timeout_seconds=240.0,
    )
    try:
        pool.start()
        ready = pool.ready_messages[0] if pool.ready_messages else {}
        results = pool.run(jobs, require_primary_integrity=True)
        validate_complete_results(
            results,
            [job["job_id"] for job in jobs],
            pool.last_run_metadata,
            require_trace_hash=mode == "conformance",
        )
    finally:
        pool.close()

    metadata = dict(pool.last_run_metadata)
    workers = metadata.get("workers", [])
    stderr_paths = [
        Path(worker["stderr_path"])
        for worker in workers
        if isinstance(worker, Mapping) and worker.get("stderr_path") not in (None, "NOT_MEASURED")
    ]
    sidecars: list[dict[str, Any]] = []
    performance_sidecars: list[dict[str, Any]] = []
    lifecycle_sidecars: list[dict[str, Any]] = []
    for stderr_path in stderr_paths:
        for line in stderr_path.read_text(encoding="utf-8").splitlines():
            if line.startswith(SHAPE_PREFIX):
                sidecar = json.loads(line[len(SHAPE_PREFIX) :])
                if (
                    sidecar.get("schema") != SHAPE_SCHEMA
                    or sidecar.get("type") != SHAPE_TYPE
                    or not isinstance(sidecar.get("job_id"), str)
                ):
                    raise ValueError(f"invalid shape sidecar in {stderr_path}")
                sidecar["_stderr_path"] = str(stderr_path.resolve())
                sidecars.append(sidecar)
            elif line.startswith(PERFORMANCE_PREFIX):
                sidecar = json.loads(line[len(PERFORMANCE_PREFIX) :])
                if (
                    sidecar.get("schema") != PERFORMANCE_SCHEMA
                    or sidecar.get("type") != PERFORMANCE_TYPE
                    or not isinstance(sidecar.get("job_id"), str)
                ):
                    raise ValueError(f"invalid performance sidecar in {stderr_path}")
                sidecar["_stderr_path"] = str(stderr_path.resolve())
                performance_sidecars.append(sidecar)
            elif line.startswith(LIFECYCLE_PREFIX):
                sidecar = json.loads(line[len(LIFECYCLE_PREFIX) :])
                if (
                    sidecar.get("schema") != LIFECYCLE_SCHEMA
                    or sidecar.get("type") != LIFECYCLE_TYPE
                    or not isinstance(sidecar.get("job_id"), str)
                ):
                    raise ValueError(f"invalid lifecycle sidecar in {stderr_path}")
                sidecar["_stderr_path"] = str(stderr_path.resolve())
                lifecycle_sidecars.append(sidecar)

    expected_ids = {str(job["job_id"]) for job in jobs}
    for sidecar_name, sidecar_rows in (
        ("shape", sidecars),
        ("performance", performance_sidecars),
        ("lifecycle", lifecycle_sidecars),
    ):
        sidecar_ids = [str(sidecar["job_id"]) for sidecar in sidecar_rows]
        if len(sidecar_ids) != len(set(sidecar_ids)):
            raise ValueError(f"{label} has duplicate {sidecar_name} sidecar job IDs")
    result_ids = [str(result["job_id"]) for result in results]
    if len(result_ids) != len(set(result_ids)):
        raise ValueError(f"{label} has duplicate worker result job IDs")
    if {str(sidecar["job_id"]) for sidecar in sidecars} != expected_ids:
        raise ValueError(
            f"{label} shape sidecars do not cover exactly the requested jobs: "
            f"expected {sorted(expected_ids)}, got {sorted(sidecar['job_id'] for sidecar in sidecars)}"
        )
    if {str(sidecar["job_id"]) for sidecar in performance_sidecars} != expected_ids:
        raise ValueError(
            f"{label} performance sidecars do not cover exactly the requested jobs: "
            f"expected {sorted(expected_ids)}, got "
            f"{sorted(sidecar['job_id'] for sidecar in performance_sidecars)}"
        )
    if {str(sidecar["job_id"]) for sidecar in lifecycle_sidecars} != expected_ids:
        raise ValueError(
            f"{label} lifecycle sidecars do not cover exactly the requested jobs: "
            f"expected {sorted(expected_ids)}, got "
            f"{sorted(sidecar['job_id'] for sidecar in lifecycle_sidecars)}"
        )

    result_by_id = {str(result["job_id"]): result for result in results}
    performance_by_id = {str(sidecar["job_id"]): sidecar for sidecar in performance_sidecars}
    lifecycle_by_id = {str(sidecar["job_id"]): sidecar for sidecar in lifecycle_sidecars}
    rows: list[dict[str, Any]] = []
    for job in jobs:
        job_id = str(job["job_id"])
        result = result_by_id[job_id]
        row: dict[str, Any] = {
            "job_id": job_id,
            "result_semantics": {
                field: result.get(field) for field in SEMANTIC_RESULT_FIELDS
            },
            "gameplay_hash": result.get("gameplay_hash"),
            "trace_hash": result.get("trace_hash"),
            "observation_count": result.get("observation_entity_total"),
            "event_count": result.get("observation_event_total"),
            "status": result.get("status"),
            "simulation_elapsed_us": result.get("simulation_elapsed_us"),
            "timing_us": result.get("timing_us"),
            "counters": result.get("counters"),
            "errors": result.get("errors"),
            "performance_audit": performance_by_id[job_id],
            "serialization_lifecycle": lifecycle_by_id[job_id],
        }
        if mode == "conformance":
            row["trace"] = _read_trace(Path(job["trace_output"]))
        rows.append(row)

    return {
        "label": label,
        "mode": mode,
        "worker_executable": str(executable.resolve()),
        "worker_sha256": _sha256_bytes(executable.read_bytes()),
        "execution_identity": dict(execution_identity),
        "ready": ready,
        "metadata": metadata,
        "jobs": rows,
        "sidecars": sidecars,
        "performance_sidecars": performance_sidecars,
        "lifecycle_sidecars": lifecycle_sidecars,
    }


def _compare_reference(
    run: Mapping[str, Any], reference_trace_dir: Path
) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    passed = True
    for row in run["jobs"]:
        job_id = str(row["job_id"])
        actual = row["trace"]
        expected_path = reference_trace_dir / f"{job_id}.jsonl"
        expected = _read_trace(expected_path)
        result_semantics = row["result_semantics"]
        expected_gameplay_hash = expected["footer"].get("semantic_gameplay_hash")
        expected_trace_hash = expected["footer"].get("trace_hash")
        semantic_fields_ok = (
            result_semantics.get("terminal") is True
            and result_semantics.get("winner") in (0, 1)
            and result_semantics.get("win_reason") in (0, 1, 2, 3, 4, 5, 6, 7)
            and row["gameplay_hash"] == expected_gameplay_hash
            and row["trace_hash"] == expected_trace_hash
        )
        trace_bytes_ok = actual["sha256"] == expected["sha256"]
        observation_hashes_ok = actual["observation_hashes"] == expected["observation_hashes"]
        steps_ok = actual["steps"] == expected["steps"]
        row_pass = semantic_fields_ok and trace_bytes_ok and observation_hashes_ok and steps_ok
        passed = passed and row_pass
        rows.append(
            {
                "job_id": job_id,
                "pass": row_pass,
                "semantic_fields_and_footers_equal": semantic_fields_ok,
                "trace_bytes_equal": trace_bytes_ok,
                "observation_hashes_equal": observation_hashes_ok,
                "trace_steps_equal": steps_ok,
                "actual_observation_count": len(actual["observation_hashes"]),
                "reference_observation_count": len(expected["observation_hashes"]),
                "actual_trace": actual["sha256"],
                "reference_trace": expected["sha256"],
            }
        )
    return {
        "pass": passed,
        "reference_trace_dir": str(reference_trace_dir.resolve()),
        "jobs": rows,
        "observation_count": sum(row["actual_observation_count"] for row in rows),
    }


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reference-traces", type=Path, required=True)
    parser.add_argument("--master-seed", type=int, default=20260815)
    parser.add_argument("--games", type=int, default=16)
    parser.add_argument("--max-steps", type=int, default=2200)
    args = parser.parse_args(arguments)

    args.output.mkdir(parents=True, exist_ok=True)
    execution_identity = {
        "source": _repository_identity(),
        "worker": {
            "path": str(args.worker.resolve()),
            "sha256": _sha256_bytes(args.worker.read_bytes()),
        },
    }
    semantic = _run_mode(
        label="semantic",
        executable=args.worker,
        output_dir=args.output,
        master_seed=args.master_seed,
        games=args.games,
        max_steps=args.max_steps,
        mode="conformance",
        execution_identity=execution_identity,
    )
    equivalence = _compare_reference(semantic, args.reference_traces)
    if not equivalence["pass"]:
        raise SystemExit("M4.3.4 semantic equivalence failed")

    focused_equivalence = _run_focused_equivalence(args.worker)

    throughput = _run_mode(
        label="throughput",
        executable=args.worker,
        output_dir=args.output,
        master_seed=args.master_seed,
        games=args.games,
        max_steps=args.max_steps,
        mode="throughput",
        execution_identity=execution_identity,
    )
    output = {
        "schema": RUN_SCHEMA,
        "status": "M4.3.4 shape workload PASS",
        "workload": {
            "matchup": "Swordsoul Tenyi ML v1 vs Salamangreat ML v1",
            "master_seed": args.master_seed,
            "games": args.games,
            "workers": 1,
            "max_steps": args.max_steps,
            "observation_mode": "full",
            "semantic_mode": "conformance",
            "throughput_mode": "throughput",
            "trace_persistence_throughput": False,
        },
        "semantic_equivalence": equivalence,
        "focused_shape_equivalence": focused_equivalence,
        "semantic_run": semantic,
        "throughput_run": throughput,
    }
    args.output.joinpath("shape_workload.json").write_text(
        json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps({
        "status": output["status"],
        "semantic_observations": equivalence["observation_count"],
        "throughput_sidecars": len(throughput["sidecars"]),
        "output": str(args.output.joinpath("shape_workload.json").resolve()),
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
