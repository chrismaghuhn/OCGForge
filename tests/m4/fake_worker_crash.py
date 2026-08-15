"""Small JSONL worker fixture for coordinator failure-isolation tests."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import time

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


def claim_start(marker: str | None) -> int:
    if marker is None:
        return 1
    path = Path(os.path.abspath(marker))
    try:
        count = int(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        count = 0
    path.write_text(str(count + 1), encoding="utf-8")
    return count + 1


def main() -> int:
    sys.stdin.reconfigure(encoding="utf-8", errors="strict")
    sys.stdout.reconfigure(encoding="utf-8", errors="strict")
    parser = argparse.ArgumentParser()
    parser.add_argument("--behavior", default="normal")
    parser.add_argument("--marker", default=None)
    args = parser.parse_args()

    role = None
    if args.behavior == "one-hung-one-periodic":
        if args.marker and not Path(os.path.abspath(args.marker)).exists():
            Path(os.path.abspath(args.marker)).write_text("hung", encoding="utf-8")
            role = "hang"
        else:
            role = "periodic"
    elif args.behavior == "replacement-handshake-failure":
        start_number = claim_start(args.marker)
        role = {
            1: "crash",
            2: "hang",
        }.get(start_number, "bad-handshake")

    ready = ready_message()
    if args.behavior == "pid-mismatch":
        ready["pid"] = os.getpid() + 1
    if role == "bad-handshake":
        ready["rules_bundle_id"] = "replacement-rules-mismatch"
    print(json.dumps(ready, separators=(",", ":")), flush=True)
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
        if role == "crash":
            should_crash = True
        if should_crash:
            print("fake worker crash after job", file=sys.stderr, flush=True)
            return 17
        if args.behavior == "hang-first-job" or role == "hang":
            print("fake worker waiting without a result", file=sys.stderr, flush=True)
            time.sleep(30)
        if role == "periodic":
            time.sleep(0.04)
        if args.behavior == "invalid-utf8":
            sys.stdout.buffer.write(b'{"schema":"ocgforge.m4.worker.v1",\xff}\n')
            sys.stdout.buffer.flush()
            return 0
        if args.behavior == "deep-malformed":
            depth = 3000
            sys.stdout.write("[" * depth + "0" + "]" * depth + "\n")
            sys.stdout.flush()
            return 0
        passed = not bool(job.get("force_unsupported", False))
        result = result_message(job, passed=passed)
        if args.behavior == "result-pid-mismatch" and args.marker:
            marker = Path(os.path.abspath(args.marker))
            if not marker.exists():
                marker.write_text("mismatch", encoding="utf-8")
                result["worker"]["pid"] = os.getpid() + 1
        if args.behavior == "extra-first-job" and args.marker:
            marker = os.path.abspath(args.marker)
            if not os.path.exists(marker):
                with open(marker, "w", encoding="utf-8"):
                    pass
                encoded = json.dumps(result, separators=(",", ":"))
                sys.stdout.write(encoded + "\n" + encoded + "\n")
                sys.stdout.flush()
                time.sleep(0.02)
                continue
        if args.behavior == "result-then-exit":
            encoded = json.dumps(result, separators=(",", ":")).encode("utf-8") + b"\n"
            os.write(sys.stdout.fileno(), encoded)
            os._exit(17)
        print(json.dumps(result, separators=(",", ":")), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
