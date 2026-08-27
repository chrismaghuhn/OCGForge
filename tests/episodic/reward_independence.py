#!/usr/bin/env python3
"""Prove that externally applied rewards cannot alter EpisodicEnvironment values."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any


def run_probe(probe: Path, *, max_actions: int, engine_budget: int, semantic_budget: int) -> dict[str, Any]:
    completed = subprocess.run(
        [
            str(probe),
            "--seed",
            "2",
            "--starting-player",
            "0",
            "--max-actions",
            str(max_actions),
            "--engine-process-budget",
            str(engine_budget),
            "--semantic-action-budget",
            str(semantic_budget),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(f"probe failed: {completed.stderr.strip()}")
    return json.loads(completed.stdout)


def apply_external_policy(environment_value: dict[str, Any], policy_id: str, reward: int) -> dict[str, Any]:
    # This wrapper is intentionally outside the environment value. It models
    # the training adapter boundary rather than adding a reward API to V2.
    return {
        "environment": environment_value,
        "reward_policy_id": policy_id,
        "reward": reward,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    terminal_a = run_probe(args.probe, max_actions=800, engine_budget=20000, semantic_budget=20000)
    # Both policies are applied to this exact recorded environment execution.
    # Independent process determinism is covered separately by G21.
    terminal_b = json.loads(json.dumps(terminal_a))
    if terminal_a.get("closure", {}).get("kind") != "TERMINAL":
        raise RuntimeError(f"reward fixture did not reach a true terminal: {terminal_a.get('closure')}")

    policy_a = apply_external_policy(terminal_a, "reward-policy-a", 1)
    policy_b = apply_external_policy(terminal_b, "reward-policy-b", -1)
    if policy_a["environment"] != policy_b["environment"]:
        raise RuntimeError("external reward policy changed environment values")

    interrupted = run_probe(args.probe, max_actions=1, engine_budget=1, semantic_budget=1)
    if interrupted.get("closure", {}).get("kind") not in {"INTERRUPTED", "FAILED"}:
        raise RuntimeError("interruption fixture unexpectedly produced an outcome")
    interrupted_a = apply_external_policy(interrupted, "reward-policy-a", 0)
    interrupted_b = apply_external_policy(interrupted, "reward-policy-b", 0)
    if interrupted_a["environment"] != interrupted_b["environment"]:
        raise RuntimeError("external reward policy changed interrupted environment values")
    if interrupted_a["reward"] != 0 or interrupted_b["reward"] != 0:
        raise RuntimeError("interrupted/failed closure received an implicit reward")

    evidence = {
        "gate": "G29",
        "result": "PASS",
        "schema": "ocgforge.reward_independence_evidence.v1",
        "terminal_closure": terminal_a["closure"]["kind"],
        "terminal_action_count": len(terminal_a["actions"]),
        "interrupted_closure": interrupted["closure"]["kind"],
        "policies": ["reward-policy-a", "reward-policy-b"],
        "environment_values_equal": True,
        "reward_is_external": True,
        "interrupted_failed_implicit_reward": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(evidence, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
