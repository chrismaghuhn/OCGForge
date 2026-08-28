#!/usr/bin/env python3
"""Discover and replay the deterministic maximum public candidate domain."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def run_probe(probe: Path, seed: int, mirror: bool, starting_player: int, max_actions: int) -> dict[str, Any]:
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
        raise RuntimeError(f"probe failed: {completed.stderr.strip()}")
    return json.loads(completed.stdout)


def frame_rows(value: dict[str, Any]) -> list[dict[str, Any]]:
    rows = []
    for frame in value["frames"]:
        rows.append(
            {
                "candidate_count": frame["candidate_count"],
                "request_kind": frame["request_kind"],
                "episode_semantic_id": value["episode_semantic_id"],
                "environment_decision_index": frame["decision_index"],
                "engine_step_index": frame["engine_step_index"],
                "public_semantic_decision_id": frame["public_semantic_decision_id"],
                "public_observation_digest": frame["public_observation_digest"],
                "public_candidate_domain_digest": frame["public_candidate_domain_digest"],
                "ordered_public_action_keys": frame["public_action_keys"],
            }
        )
    return rows


def replay_witness(probe: Path, witness: dict[str, Any], max_actions: int) -> None:
    actions = witness["ordered_replay_actions"]
    replay_path = witness["_replay_path"]
    replay_path.write_text("\n".join(actions) + "\n", encoding="utf-8", newline="\n")
    command = [
        str(probe),
        "--seed",
        str(witness["root_seed"]),
        "--max-actions",
        str(len(actions)),
        "--engine-process-budget",
        "4096",
        "--semantic-action-budget",
        "4096",
        "--starting-player",
        str(witness["starting_player"]),
        "--replay-public-actions",
        str(replay_path),
    ]
    if witness["seat_assignment"] == "mirror":
        command.append("--mirror-seats")
    completed = subprocess.run(command, check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(f"witness replay failed: {completed.stderr.strip()}")
    replay = json.loads(completed.stdout)
    replay_frames = replay["frames"][: len(witness["frames"])]
    if replay_frames != witness["frames"]:
        raise RuntimeError("witness replay changed a public frame")
    if replay["actions"][: len(actions)] != witness["actions"]:
        raise RuntimeError("witness replay changed a public action prefix")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-actions", type=int, default=64)
    parser.add_argument("--seeds", type=int, default=4)
    args = parser.parse_args()
    if args.max_actions < 0 or args.seeds < 1:
        parser.error("--max-actions must be non-negative and --seeds must be positive")

    all_rows: list[dict[str, Any]] = []
    job_values: list[tuple[int, bool, int, dict[str, Any]]] = []
    for seed in range(args.seeds):
        for mirror in (False, True):
            for starting_player in (0, 1):
                value = run_probe(args.probe, seed, mirror, starting_player, args.max_actions)
                job_values.append((seed, mirror, starting_player, value))
                all_rows.extend(frame_rows(value))
    if not all_rows:
        raise RuntimeError("witness corpus published no complete public domains")

    ordered = sorted(
        all_rows,
        key=lambda row: (
            -row["candidate_count"],
            row["episode_semantic_id"],
            row["environment_decision_index"],
            row["engine_step_index"],
            row["public_semantic_decision_id"],
            row["public_candidate_domain_digest"],
        ),
    )
    selected = ordered[0]
    per_job_maxima = [
        max(frame["candidate_count"] for frame in value["frames"])
        for _, _, _, value in job_values
    ]
    selected_job: tuple[int, bool, int, dict[str, Any]] | None = None
    for job in job_values:
        if job[3]["episode_semantic_id"] != selected["episode_semantic_id"]:
            continue
        if any(
            frame["decision_index"] == selected["environment_decision_index"]
            and frame["public_candidate_domain_digest"] == selected["public_candidate_domain_digest"]
            for frame in job[3]["frames"]
        ):
            selected_job = job
            break
    if selected_job is None:
        raise RuntimeError("selected witness did not resolve to a corpus job")

    seed, mirror, starting_player, value = selected_job
    action_count = value["frames"].index(
        next(
            frame
            for frame in value["frames"]
            if frame["decision_index"] == selected["environment_decision_index"]
            and frame["public_candidate_domain_digest"] == selected["public_candidate_domain_digest"]
        )
    )
    witness = {
        "schema": "ocgforge.candidate_domain_evidence.v2",
        "gate": "G28",
        "result": "PASS",
        "contract_id": value["contract_id"],
        "environment_semantic_id": value["environment_semantic_id"],
        "corpus": {
            "seeds": args.seeds,
            "seat_assignments": ["normal", "mirror"],
            "starting_players": [0, 1],
            "max_actions_per_job": args.max_actions,
        },
        "candidate_domain_max": selected["candidate_count"],
        "candidate_max_total": sum(per_job_maxima),
        "tie_break": [
            "candidate_count descending",
            "episode_semantic_id ascending",
            "environment_decision_index ascending",
            "engine_step_index ascending",
            "public_semantic_decision_id ascending",
            "public_candidate_domain_digest ascending",
        ],
        "witness": selected,
        "root_seed": seed,
        "seat_assignment": "mirror" if mirror else "normal",
        "starting_player": starting_player,
        "ordered_replay_actions": [
            action["public_action_key"] for action in value["actions"][:action_count]
        ],
        "frames": value["frames"][: action_count + 1],
        "actions": value["actions"][:action_count],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    replay_path = args.output.with_suffix(args.output.suffix + ".replay-actions")
    witness["_replay_path"] = replay_path
    replay_witness(args.probe, witness, args.max_actions)
    del witness["_replay_path"]
    args.output.write_text(json.dumps(witness, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8", newline="\n")
    sys.stdout.write(json.dumps({"gate": "G28", "result": "PASS", "candidate_domain_max": selected["candidate_count"]}, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
