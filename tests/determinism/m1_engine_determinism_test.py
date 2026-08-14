#!/usr/bin/env python3
"""Verify process-level semantic determinism for the M1.1 engine fixtures."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


FIXTURES = {
    "select_option": (14, "m1.1.select_option", 0),
    "select_card_multi": (15, "m1.1.select_card_multi", 1),
    "select_sum": (23, "m1.1.select_sum", 1),
    "select_counter": (22, "m1.1.select_counter", 1),
    "sort_card": (25, "m1.1.sort_card", 1),
    "select_disfield": (24, "m1.1.select_disfield", 1),
}
RULES_BUNDLE_ID = "6fbbd212ae4be2df36170dcbfcdf5c46aaaa0e3091cf815c2d0261fd01640ea4"


def run_fixture(executable: Path, fixture: str, root: Path) -> tuple[str, list[dict], str]:
    completed = subprocess.run(
        [str(executable), fixture], cwd=root, capture_output=True, text=True
    )
    if completed.returncode != 0:
        raise AssertionError(
            f"M1.1 fixture {fixture} failed with exit {completed.returncode}:\n"
            f"stdout={completed.stdout}\nstderr={completed.stderr}"
        )
    semantic_match = re.search(r"^semantic_gameplay_hash=([0-9a-f]{64})$", completed.stdout, re.MULTILINE)
    if semantic_match is None:
        raise AssertionError(f"M1.1 fixture {fixture} omitted semantic gameplay hash:\n{completed.stdout}")
    if "final_response_accepted=true" not in completed.stdout:
        raise AssertionError(f"M1.1 fixture {fixture} did not report final response acceptance")
    if "ocg_duel_set_response_calls=1" not in completed.stdout:
        raise AssertionError(f"M1.1 fixture {fixture} did not report one target response submission")

    artifact = root / "artifacts" / "m1-engine" / f"m1.1.{fixture}.jsonl"
    records = [json.loads(line) for line in artifact.read_text(encoding="utf-8").splitlines()]
    if not records or records[0]["trace_schema_version"] != "ygo.engine_trace.v2":
        raise AssertionError(f"M1.1 fixture {fixture} did not write a v2 trace")
    if records[0]["rules_bundle_id"] != RULES_BUNDLE_ID:
        raise AssertionError(f"M1.1 fixture {fixture} trace has the wrong rules bundle")
    target_type, _, minimum_intermediates = FIXTURES[fixture]
    target_records = [record for record in records[1:] if record["engine_message_type"] == target_type]
    if not target_records:
        raise AssertionError(f"M1.1 fixture {fixture} trace omitted target message {target_type}")
    if sum(not record["engine_advanced"] for record in target_records) < minimum_intermediates:
        raise AssertionError(f"M1.1 fixture {fixture} omitted an intermediate continuation record")
    state_hashes = {record["public_state_hash"] for record in target_records}
    if len(state_hashes) != 1:
        raise AssertionError(f"M1.1 fixture {fixture} changed engine state during local continuation")
    intermediate = [record for record in target_records if not record["engine_advanced"]]
    if any(record["final_engine_response_hash"] is not None for record in intermediate):
        raise AssertionError(f"M1.1 fixture {fixture} exposed a final response before completion")
    final = [record for record in target_records if record["engine_advanced"]]
    if len(final) != 1 or final[0]["final_engine_response_hash"] is None:
        raise AssertionError(f"M1.1 fixture {fixture} did not produce exactly one final response record")
    return semantic_match.group(1), target_records, completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    executable = args.probe.resolve()

    for fixture in FIXTURES:
        repeated = [run_fixture(executable, fixture, root) for _ in range(2)]
        semantic_hashes = [result[0] for result in repeated]
        if len(set(semantic_hashes)) != 1:
            raise AssertionError(f"{fixture} semantic hashes differ across independent processes: {semantic_hashes}")
        print(f"{fixture}_semantic_gameplay_hash={semantic_hashes[0]}")
        print(f"{fixture}_intermediate_records={sum(not record['engine_advanced'] for record in repeated[0][1])}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
