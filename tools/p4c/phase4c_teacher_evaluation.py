from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ROWS = (("normal", 0), ("normal", 1), ("mirror", 0), ("mirror", 1))
MATCHUP_ID = "ocgforge.matchup.swordsoul_salamangreat.v1"
RULES_BUNDLE_ID = (
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f"
)
SWORDSOUL_PROFILE_ID = (
    "ocgforge.strategy_profile.v1."
    "7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74"
)
SALAMANGREAT_PROFILE_ID = (
    "ocgforge.strategy_profile.v1."
    "3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55"
)
SWORDSOUL_BINDING_ID = (
    "ocgforge.teacher_policy_binding.v1."
    "4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c"
)
SALAMANGREAT_BINDING_ID = (
    "ocgforge.teacher_policy_binding.v1."
    "ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56"
)
SWORDSOUL_ARTIFACT_ID = (
    "policy_artifact.v1."
    "52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d"
)
SALAMANGREAT_ARTIFACT_ID = (
    "policy_artifact.v1."
    "a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527"
)

REQUIRED_FIELDS = {
    "ROW",
    "STARTING_PLAYER",
    "ROOT_SEED",
    "SEMANTIC_ACTION_BUDGET",
    "ENGINE_PROCESS_BUDGET",
    "MATCHUP_ID",
    "RULES_BUNDLE_ID",
    "SWORDSOUL_PROFILE_ID",
    "SALAMANGREAT_PROFILE_ID",
    "SWORDSOUL_BINDING_ID",
    "SALAMANGREAT_BINDING_ID",
    "SWORDSOUL_POLICY_ARTIFACT_ID",
    "SALAMANGREAT_POLICY_ARTIFACT_ID",
    "DISPOSITION",
    "RECORD_COUNT",
    "BATTLE_DECISION_RECORD_COUNT",
    "BATTLE_COMMAND_CANDIDATE_COUNT",
    "SIDECAR_INVALID_COUNT",
    "PROVEN_LETHAL_COUNT",
    "LOWER_BOUND_PRESENT_COUNT",
    "PUBLIC_GAMEPLAY_TRAJECTORY_ID",
    "TRAJECTORY_RECORD_ID",
    "DATASET_SEMANTIC_ID",
    "SIDECAR_INFLUENCES_GAMEPLAY",
    "POSITIVE_LETHAL_CAPABILITY",
    "ROW_STATUS",
}

HEX_DIGEST = re.compile(r"^[0-9a-f]{64}$")


def parse_output(output: bytes) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in output.decode("utf-8").splitlines():
        if not line:
            continue
        if "=" not in line:
            raise ValueError("probe output contains a non key/value line")
        key, value = line.split("=", 1)
        if not key or key in values:
            raise ValueError("probe output contains a duplicate/empty key")
        values[key] = value
    return values


def _integer(values: dict[str, str], key: str, minimum: int = 0) -> int:
    value = values[key]
    if not value.isdigit():
        raise ValueError(f"{key} is not a non-negative integer")
    parsed = int(value)
    if parsed < minimum:
        raise ValueError(f"{key} is below its minimum")
    return parsed


def validate_row(values: dict[str, str], row: str, starting_player: int) -> None:
    if set(values) != REQUIRED_FIELDS:
        raise ValueError("probe row has missing or unexpected fields")
    if values["ROW"] != row or values["STARTING_PLAYER"] != str(starting_player):
        raise ValueError("probe row assignment is incorrect")
    if _integer(values, "ROOT_SEED") != 2:
        raise ValueError("probe root seed is not frozen")
    if _integer(values, "SEMANTIC_ACTION_BUDGET") != 32:
        raise ValueError("probe semantic action budget is not frozen")
    if _integer(values, "ENGINE_PROCESS_BUDGET") != 4096:
        raise ValueError("probe engine process budget is not frozen")
    if values["MATCHUP_ID"] != MATCHUP_ID or values["RULES_BUNDLE_ID"] != RULES_BUNDLE_ID:
        raise ValueError("probe environment identity is not frozen")
    if values["SWORDSOUL_PROFILE_ID"] != SWORDSOUL_PROFILE_ID:
        raise ValueError("Swordsoul profile identity drifted")
    if values["SALAMANGREAT_PROFILE_ID"] != SALAMANGREAT_PROFILE_ID:
        raise ValueError("Salamangreat profile identity drifted")
    if values["SWORDSOUL_BINDING_ID"] != SWORDSOUL_BINDING_ID:
        raise ValueError("Swordsoul binding identity drifted")
    if values["SALAMANGREAT_BINDING_ID"] != SALAMANGREAT_BINDING_ID:
        raise ValueError("Salamangreat binding identity drifted")
    if values["SWORDSOUL_POLICY_ARTIFACT_ID"] != SWORDSOUL_ARTIFACT_ID:
        raise ValueError("Swordsoul PolicyArtifact identity drifted")
    if values["SALAMANGREAT_POLICY_ARTIFACT_ID"] != SALAMANGREAT_ARTIFACT_ID:
        raise ValueError("Salamangreat PolicyArtifact identity drifted")
    if values["DISPOSITION"] != "CLEAN_ADMITTED":
        raise ValueError("probe row was not cleanly admitted")
    if _integer(values, "RECORD_COUNT", 1) < 1:
        raise ValueError("probe row has no trusted records")
    for key in (
        "BATTLE_DECISION_RECORD_COUNT",
        "BATTLE_COMMAND_CANDIDATE_COUNT",
        "SIDECAR_INVALID_COUNT",
        "PROVEN_LETHAL_COUNT",
        "LOWER_BOUND_PRESENT_COUNT",
    ):
        _integer(values, key)
    if values["SIDECAR_INVALID_COUNT"] != "0":
        raise ValueError("probe row contains an invalid sidecar result")
    if values["PROVEN_LETHAL_COUNT"] != "0":
        raise ValueError("probe row emitted PROVEN_LETHAL")
    if values["LOWER_BOUND_PRESENT_COUNT"] != "0":
        raise ValueError("probe row emitted a lower bound")
    if not values["PUBLIC_GAMEPLAY_TRAJECTORY_ID"].startswith(
        "public_gameplay_trajectory.v1."
    ):
        raise ValueError("missing public gameplay trajectory identity")
    if not values["TRAJECTORY_RECORD_ID"].startswith("trajectory_record.v1."):
        raise ValueError("missing trajectory record identity")
    if not HEX_DIGEST.fullmatch(values["DATASET_SEMANTIC_ID"]):
        raise ValueError("missing dataset semantic identity")
    if values["SIDECAR_INFLUENCES_GAMEPLAY"] != "NO":
        raise ValueError("sidecar reports gameplay influence")
    if (
        values["POSITIVE_LETHAL_CAPABILITY"]
        != "BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT"
    ):
        raise ValueError("positive lethal limitation is not explicit")
    if values["ROW_STATUS"] != "PASS":
        raise ValueError("probe row did not report PASS")
    forbidden = ("semantic_key", "response_bytes", "PRIVATE-", "C:\\")
    if any(marker in "\n".join(f"{key}={value}" for key, value in values.items())
           for marker in forbidden):
        raise ValueError("probe row contains private/control-plane data")


def validate_matrix(rows: list[dict[str, str]]) -> None:
    if len(rows) != len(ROWS):
        raise ValueError("fixed matrix does not contain exactly four rows")
    expected = set(ROWS)
    seen: set[tuple[str, int]] = set()
    battle_decisions = 0
    battle_candidates = 0
    for values in rows:
        row = values.get("ROW")
        starting_player = values.get("STARTING_PLAYER")
        if row is None or starting_player not in {"0", "1"}:
            raise ValueError("matrix row lacks routing fields")
        key = (row, int(starting_player))
        if key in seen or key not in expected:
            raise ValueError("matrix row is missing, duplicate, or unexpected")
        seen.add(key)
        validate_row(values, row, int(starting_player))
        battle_decisions += int(values["BATTLE_DECISION_RECORD_COUNT"])
        battle_candidates += int(values["BATTLE_COMMAND_CANDIDATE_COUNT"])
    if seen != expected:
        raise ValueError("matrix does not cover normal/mirror x starting-player")
    if battle_decisions <= 0 or battle_candidates <= 0:
        raise ValueError("fixed matrix has zero BattleCommand coverage")


def run_row(probe: Path, row: str, starting_player: int) -> bytes:
    completed = subprocess.run(
        [str(probe), "--row", row, str(starting_player)],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"teacher probe row failed with exit {completed.returncode}"
        )
    if completed.stderr:
        raise RuntimeError("teacher probe wrote to stderr")
    parsed = parse_output(completed.stdout)
    validate_row(parsed, row, starting_player)
    return completed.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve()
    if not probe.is_file():
        raise SystemExit(f"teacher evaluation probe is missing: {probe}")

    rows: list[dict[str, str]] = []
    for row, starting_player in ROWS:
        first = run_row(probe, row, starting_player)
        second = run_row(probe, row, starting_player)
        if first != second:
            raise SystemExit(
                f"teacher row {row}/{starting_player} is not deterministic"
            )
        rows.append(parse_output(first))
    validate_matrix(rows)

    print("NORMAL_START0=PASS")
    print("NORMAL_START1=PASS")
    print("MIRROR_START0=PASS")
    print("MIRROR_START1=PASS")
    print("FIXED_MATCHUP_MATRIX=PASS")
    print("INDEPENDENT_PROCESS_DETERMINISM=PASS")
    print("TEACHER_V1_IDENTITIES=PASS")
    print("TRUSTED_TRAJECTORY_PATH=PASS")
    print("SIDECAR_ISOLATION=PASS")
    print("BATTLE_COVERAGE=PASS")
    print("POSITIVE_LETHAL_LIMITATION=PASS")
    print("PHASE4C_TASK5_EVALUATION=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
