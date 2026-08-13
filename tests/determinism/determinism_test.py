#!/usr/bin/env python3
"""Process-level conformance checks for the controlled M0 trace."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def run_trace(probe: Path, seed: int, output: Path, root: Path) -> tuple[str, list[dict]]:
    completed = subprocess.run(
        [str(probe), "--seed", str(seed), "--max-steps", "1000", "--output", str(output)],
        cwd=root,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"probe failed for seed {seed} with exit {completed.returncode}:\n"
            f"stdout={completed.stdout}\nstderr={completed.stderr}"
        )
    lines = output.read_bytes().splitlines(keepends=True)
    canonical_lines = [line for line in lines if not line.startswith(b"# trace_hash=")]
    canonical = b"".join(canonical_lines)
    actual_hash = hashlib.sha256(canonical).hexdigest()
    footer = lines[-1].decode("ascii").strip()
    expected_hash = footer.removeprefix("# trace_hash=")
    if actual_hash != expected_hash:
        raise AssertionError(f"probe footer hash mismatch: {actual_hash} != {expected_hash}")

    records = [json.loads(line) for line in canonical.decode("utf-8").splitlines()]
    for record in records[1:]:
        candidate_count = record["complete_candidate_count"]
        keys = record["ordered_candidate_semantic_keys"]
        if candidate_count != len(keys):
            raise AssertionError(f"candidate count mismatch in step {record['step_index']}")
        selected = record["selected_semantic_key"]
        if not record["terminal"] and selected not in keys:
            raise AssertionError(f"selected key is not selectable in step {record['step_index']}")
    return actual_hash, records


def meaningful_trace_signature(records: list[dict]) -> tuple:
    return tuple(
        (
            record["step_index"],
            record["raw_message_sha256"],
            record["decision_request_kind"],
            tuple(record["ordered_candidate_semantic_keys"]),
            record["selected_semantic_key"],
            record["public_state_hash"],
            record["terminal"],
            record["winner"],
            record["win_reason"],
        )
        for record in records[1:]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    same_seed = 0x0123456789ABCDEF
    with tempfile.TemporaryDirectory(prefix="ygo-m0-determinism-") as directory:
        temp = Path(directory)
        repeated = [run_trace(args.probe, same_seed, temp / f"repeat-{index}.jsonl", root)[0] for index in range(20)]
        if len(set(repeated)) != 1:
            raise AssertionError(f"same-seed hashes differ across repeated runs: {repeated}")

        independent = [run_trace(args.probe, same_seed, temp / f"process-{index}.jsonl", root)[0] for index in range(8)]
        if len(set(independent)) != 1:
            raise AssertionError(f"same-seed hashes differ across independent processes: {independent}")

        different_results = [
            run_trace(args.probe, seed, temp / f"seed-{index}.jsonl", root)
            for index, seed in enumerate((1, 2, 3, 4))
        ]
        different = [result[0] for result in different_results]
        meaningful = [meaningful_trace_signature(result[1]) for result in different_results]
        if len(set(meaningful)) < 2:
            raise AssertionError(
                "different seeds did not change a meaningful non-manifest trace field: "
                f"{different}"
            )

    print(f"same_seed_trace_hash={repeated[0]}")
    print(f"independent_process_trace_hash={independent[0]}")
    print(f"different_seed_trace_hashes={','.join(different)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
