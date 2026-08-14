#!/usr/bin/env python3
"""Process-level conformance checks for the controlled Windows trace."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def run_trace(probe: Path, seed: int, output: Path, root: Path) -> tuple[str, str, list[dict]]:
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
    canonical_lines = [line for line in lines if not line.startswith(b"#")]
    canonical = b"".join(canonical_lines)
    actual_hash = hashlib.sha256(canonical).hexdigest()
    trace_footer = next(line.decode("ascii").strip() for line in lines if line.startswith(b"# trace_hash="))
    semantic_footer = next(
        line.decode("ascii").strip() for line in lines if line.startswith(b"# semantic_gameplay_hash=")
    )
    expected_hash = trace_footer.removeprefix("# trace_hash=")
    if actual_hash != expected_hash:
        raise AssertionError(f"probe footer hash mismatch: {actual_hash} != {expected_hash}")
    semantic_hash = semantic_footer.removeprefix("# semantic_gameplay_hash=")
    if len(semantic_hash) != 64 or any(character not in "0123456789abcdef" for character in semantic_hash):
        raise AssertionError(f"invalid semantic gameplay hash footer: {semantic_hash}")

    records = [json.loads(line) for line in canonical.decode("utf-8").splitlines()]
    if not records or records[0]["trace_schema_version"] != "ygo.engine_trace.v2":
        raise AssertionError("probe did not emit the v2 trace manifest")
    previous_decision_index = -1
    for record in records[1:]:
        for field in (
            "decision_index",
            "engine_step_index",
            "engine_advanced",
            "continuation_state_hash",
            "continuation_step",
            "continuation_steps",
            "final_engine_response_hash",
            "peak_candidate_count",
            "terminal_solution_count",
        ):
            if field not in record:
                raise AssertionError(f"v2 trace omitted {field} in step {record['step_index']}")
        if record["decision_index"] <= previous_decision_index:
            raise AssertionError("decision_index did not increase monotonically")
        previous_decision_index = record["decision_index"]
        candidate_count = record["complete_candidate_count"]
        keys = record["ordered_candidate_semantic_keys"]
        if candidate_count != len(keys):
            raise AssertionError(f"candidate count mismatch in step {record['step_index']}")
        selected = record["selected_semantic_key"]
        if not record["terminal"] and selected not in keys:
            raise AssertionError(f"selected key is not selectable in step {record['step_index']}")
        if not record["engine_advanced"] and record["final_engine_response_hash"] is not None:
            raise AssertionError("intermediate continuation exposed a final engine response hash")
        if not record["terminal"] and record["engine_advanced"] and record["final_engine_response_hash"] is None:
            raise AssertionError("engine-advancing decision omitted its final response hash")
        if record["continuation_id"] and record["peak_candidate_count"] < candidate_count:
            raise AssertionError("continuation peak candidate metric is below the current legal domain")
    return actual_hash, semantic_hash, records


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
        repeated_results = [
            run_trace(args.probe, same_seed, temp / f"repeat-{index}.jsonl", root) for index in range(20)
        ]
        repeated = [result[0] for result in repeated_results]
        repeated_semantic = [result[1] for result in repeated_results]
        if len(set(repeated)) != 1:
            raise AssertionError(f"same-seed hashes differ across repeated runs: {repeated}")
        if len(set(repeated_semantic)) != 1:
            raise AssertionError(f"same-seed semantic gameplay hashes differ: {repeated_semantic}")

        independent_results = [
            run_trace(args.probe, same_seed, temp / f"process-{index}.jsonl", root) for index in range(8)
        ]
        independent = [result[0] for result in independent_results]
        if len(set(independent)) != 1:
            raise AssertionError(f"same-seed hashes differ across independent processes: {independent}")

        different_results = [
            run_trace(args.probe, seed, temp / f"seed-{index}.jsonl", root)
            for index, seed in enumerate((1, 2, 3, 4))
        ]
        different = [result[0] for result in different_results]
        different_semantic = [result[1] for result in different_results]
        meaningful = [meaningful_trace_signature(result[2]) for result in different_results]
        if len(set(meaningful)) < 2:
            raise AssertionError(
                "different seeds did not change a meaningful non-manifest trace field: "
                f"{different}"
            )

    print(f"same_seed_trace_hash={repeated[0]}")
    print(f"independent_process_trace_hash={independent[0]}")
    print(f"different_seed_trace_hashes={','.join(different)}")
    print(f"same_seed_semantic_gameplay_hash={repeated_semantic[0]}")
    print(f"different_seed_semantic_gameplay_hashes={','.join(different_semantic)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
