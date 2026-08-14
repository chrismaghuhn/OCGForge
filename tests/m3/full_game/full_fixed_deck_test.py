from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import subprocess
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.m3.rules_mode import assert_canonical_environment, load_canonical_environment


DECK_A_HASH = "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7"
DECK_B_HASH = "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188"
REQUIRED_GAME_COUNT = 16
PARTITION_SEEDS = (1, 2, 3, 4)
CANONICAL_ENVIRONMENT = load_canonical_environment(ROOT / "third_party" / "rules_bundle.lock.json")


def _read_trace(path: Path) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    records: list[dict[str, Any]] = []
    summary: dict[str, Any] | None = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("# m3_summary="):
            summary = json.loads(line[len("# m3_summary="):])
        elif line and not line.startswith("#"):
            records.append(json.loads(line))
    if not records or summary is None:
        raise AssertionError(f"trace is missing records or M3 summary: {path}")
    return records, summary


def _validate_trace(records: list[dict[str, Any]], summary: dict[str, Any], mirror: bool, starting_player: int) -> None:
    manifest = records[0]
    expected_hashes = [DECK_B_HASH, DECK_A_HASH] if mirror else [DECK_A_HASH, DECK_B_HASH]
    if manifest.get("fixture_deck_hashes") != expected_hashes:
        raise AssertionError(f"seat manifest mismatch: {manifest.get('fixture_deck_hashes')}")
    assert_canonical_environment(manifest, "full-game trace manifest")
    if manifest.get("rules_bundle_id") != CANONICAL_ENVIRONMENT["rules_bundle_id"]:
        raise AssertionError("full game did not use the canonical rules bundle")
    if manifest.get("starting_player") != starting_player:
        raise AssertionError("full-game trace did not record the requested starting player")
    assert_canonical_environment(summary, "full-game summary")
    if summary.get("rules_bundle_id") != CANONICAL_ENVIRONMENT["rules_bundle_id"]:
        raise AssertionError("full-game summary did not use the canonical rules bundle")
    if summary.get("starting_player") != starting_player:
        raise AssertionError("full-game summary did not record the requested starting player")
    terminal = [record for record in records if record.get("terminal") is True]
    if not summary.get("terminal") or len(terminal) != 1:
        raise AssertionError("full fixed-deck run did not produce exactly one terminal result")
    if summary.get("battle_command_count", 0) <= 0:
        raise AssertionError("full fixed-deck run did not execute a Battle Phase command")
    if summary.get("visible_win_event_count", 0) != 1:
        raise AssertionError("full fixed-deck run did not expose exactly one MSG_WIN projection")
    for record in records[1:]:
        if record.get("terminal") is True:
            continue
        if "complete_candidate_count" not in record:
            continue
        keys = record.get("ordered_candidate_semantic_keys", [])
        if len(keys) != record["complete_candidate_count"] or len(set(keys)) != len(keys):
            raise AssertionError("candidate domain was truncated or non-canonical")
        selected = record.get("selected_semantic_key")
        if selected is not None and selected not in keys:
            raise AssertionError("selected semantic action was not in the complete candidate domain")
    if any(summary.get(name, 0) != 0 for name in (
        "unsupported_count",
        "retry_count",
        "automatic_decision_count",
        "candidate_truncation_count",
        "core_error_count",
    )):
        raise AssertionError("full game contains a rejected conformance category")


def _run_one(probe: Path, output: Path, seed: int, mirror: bool, starting_player: int,
             max_steps: int, timeout: int) -> dict[str, Any]:
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(probe), "--m3-full-game", "--seed", str(seed), "--max-steps", str(max_steps),
               "--starting-player", str(starting_player), "--output", str(output)]
    if mirror:
        command.append("--mirror-seats")
    try:
        completed = subprocess.run(command, capture_output=True, text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        return {"seed": seed, "mirror_seats": mirror, "starting_player": starting_player,
                "status": "TIMEOUT", "stderr": str(error)}
    if completed.returncode != 0:
        return {
            "seed": seed,
            "mirror_seats": mirror,
            "starting_player": starting_player,
            "status": "PROCESS_FAILURE",
            "returncode": completed.returncode,
            "stderr": completed.stderr[-4000:],
        }
    try:
        records, summary = _read_trace(output)
        _validate_trace(records, summary, mirror, starting_player)
    except (AssertionError, json.JSONDecodeError, OSError) as error:
        return {"seed": seed, "mirror_seats": mirror, "starting_player": starting_player,
                "status": "CONFORMANCE_FAILURE", "error": str(error)}
    return {
        "seed": seed,
        "mirror_seats": mirror,
        "starting_player": starting_player,
        "status": "PASS",
        "trace": str(output),
        "manifest_deck_hashes": records[0]["fixture_deck_hashes"],
        **summary,
    }


def run_games(probe: Path, output: Path, games: int, max_steps: int, timeout: int) -> dict[str, Any]:
    if games != REQUIRED_GAME_COUNT:
        raise ValueError(f"M3.5 requires exactly {REQUIRED_GAME_COUNT} canonical partitioned games")
    specs = [(seed, mirror, starting_player)
             for seed in PARTITION_SEEDS
             for mirror in (False, True)
             for starting_player in (0, 1)]
    results: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=min(4, games)) as executor:
        futures = {
            executor.submit(_run_one, probe, output / f"seed-{seed}-{'mirror' if mirror else 'normal'}-start-{starting_player}.jsonl",
                            seed, mirror, starting_player, max_steps, timeout): (seed, mirror, starting_player)
            for seed, mirror, starting_player in specs
        }
        for future in as_completed(futures):
            results.append(future.result())
    results.sort(key=lambda item: (item["seed"], item["mirror_seats"], item["starting_player"]))
    complete = [item for item in results if item.get("status") == "PASS" and item.get("terminal")]
    start_players = sorted({item.get("starting_player") for item in complete})
    report = {
        "schema_version": "ocgforge.m3.full_fixed_deck_results.v1",
        "canonical_environment": {
            key: CANONICAL_ENVIRONMENT[key]
            for key in ("format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
                        "ocgcore_commit", "core_patchset_id", "core_patchset_sha256",
                        "ocg_api_version", "cardscripts_commit", "babelcdb_commit")
        },
        "requested_games": games,
        "complete_games": len(complete),
        "required_complete_games": REQUIRED_GAME_COUNT,
        "start_player_partitions": start_players,
        "both_start_player_partitions": 0 in start_players and 1 in start_players,
        "seat_partitions": ["normal", "mirror"],
        "partition_seeds": list(PARTITION_SEEDS),
        "unsupported_count": sum(item.get("unsupported_count", 0) for item in results),
        "retry_count": sum(item.get("retry_count", 0) for item in results),
        "automatic_decision_count": sum(item.get("automatic_decision_count", 0) for item in results),
        "candidate_truncation_count": sum(item.get("candidate_truncation_count", 0) for item in results),
        "core_error_count": sum(item.get("core_error_count", 0) for item in results),
        "results": results,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "full_fixed_deck_results.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--games", type=int, default=REQUIRED_GAME_COUNT)
    parser.add_argument("--max-steps", type=int, default=2200)
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--output", type=Path, default=Path("artifacts/m3/canonical_mr5/full_games"))
    parser.add_argument("--reuse-existing", action="store_true")
    args = parser.parse_args()
    if args.reuse_existing:
        if args.games != REQUIRED_GAME_COUNT:
            raise ValueError(f"M3.5 requires exactly {REQUIRED_GAME_COUNT} canonical partitioned games")
        specs = [(seed, mirror, starting_player)
                 for seed in PARTITION_SEEDS
                 for mirror in (False, True)
                 for starting_player in (0, 1)]
        results = []
        for seed, mirror, starting_player in specs:
            trace = args.output / f"seed-{seed}-{'mirror' if mirror else 'normal'}-start-{starting_player}.jsonl"
            try:
                records, summary = _read_trace(trace)
                _validate_trace(records, summary, mirror, starting_player)
                results.append({"seed": seed, "mirror_seats": mirror, "starting_player": starting_player,
                                "status": "PASS",
                                "trace": str(trace), "manifest_deck_hashes": records[0]["fixture_deck_hashes"],
                                **summary})
            except (AssertionError, json.JSONDecodeError, OSError) as error:
                results.append({"seed": seed, "mirror_seats": mirror, "starting_player": starting_player,
                                "status": "CONFORMANCE_FAILURE",
                                "error": str(error)})
        results.sort(key=lambda item: (item["seed"], item["mirror_seats"], item["starting_player"]))
        complete = [item for item in results if item.get("status") == "PASS" and item.get("terminal")]
        start_players = sorted({item.get("starting_player") for item in complete})
        report = {
            "schema_version": "ocgforge.m3.full_fixed_deck_results.v1",
            "canonical_environment": {
                key: CANONICAL_ENVIRONMENT[key]
                for key in ("format_id", "duel_mode_name", "duel_flags", "rules_bundle_id",
                            "ocgcore_commit", "core_patchset_id", "core_patchset_sha256",
                            "ocg_api_version", "cardscripts_commit", "babelcdb_commit")
            },
            "requested_games": args.games,
            "complete_games": len(complete),
            "required_complete_games": REQUIRED_GAME_COUNT,
            "start_player_partitions": start_players,
            "both_start_player_partitions": 0 in start_players and 1 in start_players,
            "seat_partitions": ["normal", "mirror"],
            "partition_seeds": list(PARTITION_SEEDS),
            "unsupported_count": sum(item.get("unsupported_count", 0) for item in results),
            "retry_count": sum(item.get("retry_count", 0) for item in results),
            "automatic_decision_count": sum(item.get("automatic_decision_count", 0) for item in results),
            "candidate_truncation_count": sum(item.get("candidate_truncation_count", 0) for item in results),
            "core_error_count": sum(item.get("core_error_count", 0) for item in results),
            "results": results,
        }
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output / "full_fixed_deck_results.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    else:
        report = run_games(args.probe, args.output, args.games, args.max_steps, args.timeout)
    print(json.dumps({
        "complete_games": report["complete_games"],
        "required_complete_games": report["required_complete_games"],
        "both_start_player_partitions": report["both_start_player_partitions"],
        "statuses": [item["status"] for item in report["results"]],
    }, sort_keys=True))
    return 0 if report["complete_games"] >= REQUIRED_GAME_COUNT else 1


if __name__ == "__main__":
    raise SystemExit(main())
