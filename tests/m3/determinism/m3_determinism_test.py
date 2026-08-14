from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.m3.rules_mode import assert_canonical_environment, load_canonical_environment


EXPECTED_DECK_HASHES = [
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
]
CANONICAL_ENVIRONMENT = load_canonical_environment(ROOT / "third_party" / "rules_bundle.lock.json")


def _read_trace(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any], dict[str, str]]:
    records: list[dict[str, Any]] = []
    summary: dict[str, Any] | None = None
    footers: dict[str, str] = {}
    for raw_line in path.read_bytes().splitlines():
        line = raw_line.decode("utf-8")
        if line.startswith("# m3_summary="):
            summary = json.loads(line[len("# m3_summary="):])
        elif line.startswith("# ") and "=" in line:
            name, value = line[2:].split("=", 1)
            footers[name] = value
        elif line:
            records.append(json.loads(line))
    if not records or summary is None:
        raise AssertionError(f"trace is missing records or M3 summary: {path}")
    return records, summary, footers


def _validate_trace(path: Path, expected_starting_player: int) -> tuple[list[dict[str, Any]], dict[str, Any], dict[str, str]]:
    records, summary, footers = _read_trace(path)
    manifest = records[0]
    if manifest.get("fixture_deck_hashes") != EXPECTED_DECK_HASHES:
        raise AssertionError(f"unexpected deck hashes in {path}")
    assert_canonical_environment(manifest, f"determinism trace manifest {path}")
    if manifest.get("rules_bundle_id") != CANONICAL_ENVIRONMENT["rules_bundle_id"]:
        raise AssertionError(f"unexpected canonical rules bundle in {path}")
    if manifest.get("starting_player") != expected_starting_player:
        raise AssertionError(f"unexpected starting player in {path}")
    assert_canonical_environment(summary, f"determinism summary {path}")
    if summary.get("rules_bundle_id") != CANONICAL_ENVIRONMENT["rules_bundle_id"]:
        raise AssertionError(f"unexpected canonical rules bundle in summary {path}")
    if summary.get("starting_player") != expected_starting_player:
        raise AssertionError(f"unexpected starting player in summary {path}")
    if not summary.get("terminal") or len([record for record in records if record.get("terminal")]) != 1:
        raise AssertionError(f"trace is not one complete terminal game: {path}")
    for record in records[1:]:
        if record.get("terminal"):
            continue
        keys = record.get("ordered_candidate_semantic_keys", [])
        if record.get("complete_candidate_count") != len(keys) or len(keys) != len(set(keys)):
            raise AssertionError(f"candidate domain is incomplete or non-canonical at step {record['step_index']}")
        if record.get("selected_semantic_key") not in keys:
            raise AssertionError(f"selected action is outside the domain at step {record['step_index']}")
    for field in ("unsupported_count", "retry_count", "automatic_decision_count", "candidate_truncation_count", "core_error_count"):
        if summary.get(field, 0) != 0:
            raise AssertionError(f"{field} is non-zero in {path}")
    for field in ("semantic_gameplay_hash", "trace_hash"):
        value = summary.get(field) or footers.get(field)
        if not isinstance(value, str) or len(value) != 64:
            raise AssertionError(f"missing or malformed {field} in {path}")
    return records, summary, footers


def _run_probe(probe: Path, root: Path, output: Path, seed: int, starting_player: int,
               replay: Path | None = None,
               timeout: int = 300) -> None:
    command = [
        str(probe),
        "--m3-full-game",
        "--seed",
        str(seed),
        "--starting-player",
        str(starting_player),
        "--max-steps",
        "1800",
        "--output",
        str(output),
    ]
    if replay is not None:
        command.extend(("--replay-actions", str(replay)))
    completed = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=timeout, check=False)
    if completed.returncode != 0:
        raise AssertionError(
            f"probe failed with exit {completed.returncode}:\n"
            f"stdout={completed.stdout}\n\nstderr={completed.stderr}"
        )


def _actions(records: list[dict[str, Any]]) -> list[str]:
    return [
        record["selected_semantic_key"]
        for record in records[1:]
        if not record.get("terminal") and record.get("selected_semantic_key")
    ]


def _assert_equal(reference: tuple[list[dict[str, Any]], dict[str, Any], dict[str, str]],
                  candidate: tuple[list[dict[str, Any]], dict[str, Any], dict[str, str]],
                  label: str) -> None:
    reference_records, reference_summary, reference_footers = reference
    candidate_records, candidate_summary, candidate_footers = candidate
    if reference_summary["semantic_gameplay_hash"] != candidate_summary["semantic_gameplay_hash"]:
        raise AssertionError(f"{label}: semantic gameplay hash differs")
    if reference_summary["trace_hash"] != candidate_summary["trace_hash"]:
        raise AssertionError(f"{label}: trace hash differs")
    if reference_footers.get("semantic_gameplay_hash") != candidate_footers.get("semantic_gameplay_hash"):
        raise AssertionError(f"{label}: semantic footer differs")
    if reference_footers.get("trace_hash") != candidate_footers.get("trace_hash"):
        raise AssertionError(f"{label}: trace footer differs")
    if reference_records != candidate_records:
        raise AssertionError(f"{label}: canonical gameplay records differ")


def _run_partition(probe: Path, output: Path, timeout: int, starting_player: int) -> dict[str, Any]:
    root = Path(__file__).resolve().parents[3]
    probe = probe.resolve()
    partition_root = output / f"start-{starting_player}"
    partition_root.mkdir(parents=True, exist_ok=True)
    source_path = partition_root / "seed-2-source.jsonl"
    independent_path = partition_root / "seed-2-independent.jsonl"

    with ThreadPoolExecutor(max_workers=2) as executor:
        source_future = executor.submit(_run_probe, probe, root, source_path, 2, starting_player, None, timeout)
        independent_future = executor.submit(_run_probe, probe, root, independent_path, 2, starting_player, None, timeout)
        source_future.result()
        independent_future.result()

    source = _validate_trace(source_path, starting_player)
    independent = _validate_trace(independent_path, starting_player)
    _assert_equal(source, independent, "independent process replay")

    action_path = partition_root / "seed-2-source.actions.txt"
    action_values = _actions(source[0])
    action_path.write_bytes(("\r\n".join(action_values) + "\r\n").encode("utf-8"))
    if b"\r\n" not in action_path.read_bytes():
        raise AssertionError("semantic action replay input was not emitted as CRLF")

    replay_path = partition_root / "seed-2-action-replay.jsonl"
    _run_probe(probe, root, replay_path, 2, starting_player, action_path, timeout)
    replay = _validate_trace(replay_path, starting_player)
    _assert_equal(source, replay, "semantic action re-execution")

    report = {
        "seed": 2,
        "starting_player": starting_player,
        "independent_processes": 2,
        "semantic_action_count": len(action_values),
        "action_replay_line_ending": "CRLF",
        "source_trace": str(source_path),
        "independent_trace": str(independent_path),
        "replay_trace": str(replay_path),
        "semantic_gameplay_hash": source[1]["semantic_gameplay_hash"],
        "trace_hash": source[1]["trace_hash"],
        "independent_process_match": True,
        "semantic_action_reexecution_match": True,
        "crlf_semantic_replay_match": True,
    }
    return report


def run(probe: Path, output: Path, timeout: int, starting_player: int | None = None) -> dict[str, Any]:
    output.mkdir(parents=True, exist_ok=True)
    players = (starting_player,) if starting_player is not None else (0, 1)
    partitions = {
        str(player): _run_partition(probe, output, timeout, player)
        for player in players
    }
    default_player = players[0]
    default_partition = partitions[str(default_player)]
    report = {
        "schema_version": "ocgforge.m3.determinism_results.v2",
        "canonical_environment": {
            key: CANONICAL_ENVIRONMENT[key]
            for key in ("format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
                        "ocgcore_commit", "core_patchset_id", "core_patchset_sha256",
                        "ocg_api_version", "cardscripts_commit", "babelcdb_commit")
        },
        "starting_player_partitions": list(players),
        "partitions": partitions,
        "seed": default_partition["seed"],
        "starting_player": default_player,
        "independent_processes": 2,
        "semantic_action_count": default_partition["semantic_action_count"],
        "action_replay_line_ending": "CRLF",
        "source_trace": default_partition["source_trace"],
        "independent_trace": default_partition["independent_trace"],
        "replay_trace": default_partition["replay_trace"],
        "semantic_gameplay_hash": default_partition["semantic_gameplay_hash"],
        "trace_hash": default_partition["trace_hash"],
        "independent_process_match": all(partition["independent_process_match"]
                                          for partition in partitions.values()),
        "semantic_action_reexecution_match": all(partition["semantic_action_reexecution_match"]
                                                   for partition in partitions.values()),
        "crlf_semantic_replay_match": all(partition["crlf_semantic_replay_match"]
                                           for partition in partitions.values()),
    }
    (output / "m3_determinism_results.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("artifacts/m3/canonical_mr5/determinism"))
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--starting-player", type=int, choices=(0, 1), default=None,
                        help="run only one partition; the canonical run covers both 0 and 1")
    args = parser.parse_args()
    report = run(args.probe, args.output, args.timeout, args.starting_player)
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
