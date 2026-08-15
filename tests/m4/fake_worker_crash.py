"""Small JSONL worker fixture for coordinator failure-isolation tests."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from tools.m4.worker_protocol_contract import (
    CANONICAL_DECK_HASHES,
    CANONICAL_PATCHSET_SHA256,
    CANONICAL_RULES_BUNDLE_ID,
    PROTOCOL_SCHEMA,
    WORKER_IDENTITY,
)


def ready_message() -> dict[str, object]:
    return {
        "schema": PROTOCOL_SCHEMA,
        "type": "ready",
        "protocol_version": PROTOCOL_SCHEMA,
        "pid": os.getpid(),
        "rules_bundle_id": CANONICAL_RULES_BUNDLE_ID,
        "core_patchset_sha256": CANONICAL_PATCHSET_SHA256,
        "deck_hashes": list(CANONICAL_DECK_HASHES),
        "format_id": "TCG_ADVANCED_2026_05_18",
        "duel_mode_name": "DUEL_MODE_MR5",
        "duel_flags": 190464,
        "compiler_identity": "fake-worker",
        "build_type": "Test",
        "worker_identity": WORKER_IDENTITY,
    }


def result_message(job: dict[str, object], *, passed: bool) -> dict[str, object]:
    job_id = str(job["job_id"])
    errors = {
        "retries": 0,
        "unsupported": 0 if passed else 1,
        "automatic": 0,
        "truncated": 0,
        "core_errors": 0,
        "worker_errors": 0,
    }
    return {
        "schema": PROTOCOL_SCHEMA,
        "type": "result",
        "status": "passed" if passed else "failed",
        "job_id": job_id,
        "terminal": passed,
        "winner": 0 if passed else None,
        "win_reason": 1 if passed else None,
        "engine_steps": 12,
        "interactive_decisions": 2,
        "semantic_action_count": 2,
        "gameplay_hash": "a" * 64 if passed else None,
        "trace_hash": "b" * 64 if passed else None,
        "simulation_elapsed_us": 100,
        "coordinator_elapsed_us": None,
        "errors": errors,
        "timing_us": {
            "core_process": 20,
            "protocol_candidate": 10,
            "continuation": 10,
            "observation": 20,
            "trace_hash": 10,
            "serialization": 0,
            "other": 30,
            "trace_persistence": 0,
        },
        "counters": {
            "ocg_duel_process": 12,
            "ocg_duel_query": 0,
            "ocg_duel_query_location": 0,
            "ocg_duel_query_field": 0,
            "ocg_duel_query_count": 0,
            "script_reader_requests": 0,
            "script_loads": 0,
            "observations": 2,
            "entities_projected": 2,
            "candidate_sets": 2,
            "candidate_total": 4,
            "candidate_max": 2,
            "semantic_hashes": 1,
            "trace_bytes_serialized": 0,
        },
        "worker": {
            "pid": os.getpid(),
            "restart_index": 0,
            "crashed": False,
            "restarted": False,
        },
        "failure_code": None if passed else "unsupported",
        "error_message": None if passed else "deliberately unsupported test job",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--behavior", default="normal")
    parser.add_argument("--marker", default=None)
    args = parser.parse_args()

    print(json.dumps(ready_message(), separators=(",", ":")), flush=True)
    if args.behavior == "crash-after-ready":
        print("fake worker crash after ready", file=sys.stderr, flush=True)
        return 17

    for line in sys.stdin:
        job = json.loads(line)
        should_crash = args.behavior == "crash-after-job"
        if args.behavior == "crash-first-job" and args.marker:
            marker = os.path.abspath(args.marker)
            if not os.path.exists(marker):
                with open(marker, "w", encoding="utf-8"):
                    pass
                should_crash = True
        if should_crash:
            print("fake worker crash after job", file=sys.stderr, flush=True)
            return 17
        passed = not bool(job.get("force_unsupported", False))
        print(json.dumps(result_message(job, passed=passed), separators=(",", ":")), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
