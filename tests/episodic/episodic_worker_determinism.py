#!/usr/bin/env python3
"""Compare value-only Episodic V2 probe output across independent processes."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


def run_job(probe: Path, job: tuple[int, bool, int], max_actions: int) -> dict[str, Any]:
    seed, mirror, starting_player = job
    command = [
        str(probe),
        "--seed",
        str(seed),
        "--max-actions",
        str(max_actions),
        "--engine-process-budget",
        "4096",
        "--semantic-action-budget",
        "4096",
        "--starting-player",
        str(starting_player),
    ]
    if mirror:
        command.append("--mirror-seats")
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(
            f"probe failed for seed={seed} mirror={mirror} starting={starting_player}: "
            f"{completed.stderr.strip()}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"probe emitted invalid JSON: {exc}") from exc


def semantic_payload(value: dict[str, Any]) -> dict[str, Any]:
    # Keep only fields owned by the public V2 value contract. In particular,
    # worker count, PID, timing, paths, and stderr diagnostics never enter the
    # comparison or the evidence record.
    return {
        "contract_id": value["contract_id"],
        "environment_semantic_id": value["environment_semantic_id"],
        "episode_semantic_id": value["episode_semantic_id"],
        "root_seed": value["root_seed"],
        "seat_assignment": value["seat_assignment"],
        "frames": value["frames"],
        "actions": value["actions"],
        "closure": value["closure"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--max-actions", type=int, default=8)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--workers", type=int, default=16)
    args = parser.parse_args()
    if args.max_actions < 0 or args.workers < 1:
        parser.error("--max-actions must be non-negative and --workers must be positive")

    jobs = [(seed, mirror, starting) for seed in range(4) for mirror in (False, True) for starting in (0, 1)]
    baseline = [run_job(args.probe, job, args.max_actions) for job in jobs]
    expected = [semantic_payload(value) for value in baseline]
    mismatches: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = [executor.submit(run_job, args.probe, job, args.max_actions) for job in jobs]
        for job, future, expected_value in zip(jobs, futures, expected):
            actual = semantic_payload(future.result())
            if actual != expected_value:
                mismatches.append(f"job={job}")

    evidence = {
        "schema": "ocgforge.episodic_acceptance_evidence.v2",
        "gate": "G21",
        "result": "PASS" if not mismatches else "FAIL",
        "probe": str(args.probe.resolve()),
        "jobs": len(jobs),
        "worker_counts": [1, args.workers],
        "max_actions": args.max_actions,
        "compared_fields": [
            "environment_semantic_id",
            "episode_semantic_id",
            "frames",
            "actions",
            "closure",
        ],
        "mismatches": mismatches,
    }
    rendered = json.dumps(evidence, sort_keys=True, separators=(",", ":")) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8", newline="\n")
    sys.stdout.write(rendered)
    return 0 if not mismatches else 1


if __name__ == "__main__":
    raise SystemExit(main())
