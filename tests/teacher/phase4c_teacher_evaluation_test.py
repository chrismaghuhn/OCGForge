from __future__ import annotations

import copy
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "p4c" / "phase4c_teacher_evaluation.py"
SPEC = importlib.util.spec_from_file_location("phase4c_teacher_evaluation", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("could not load the Task-5 evaluation validator")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def valid_row(row: str, starting_player: int) -> dict[str, str]:
    values = {
        "ROW": row,
        "STARTING_PLAYER": str(starting_player),
        "ROOT_SEED": "2",
        "SEMANTIC_ACTION_BUDGET": "32",
        "ENGINE_PROCESS_BUDGET": "4096",
        "MATCHUP_ID": MODULE.MATCHUP_ID,
        "RULES_BUNDLE_ID": MODULE.RULES_BUNDLE_ID,
        "SWORDSOUL_PROFILE_ID": MODULE.SWORDSOUL_PROFILE_ID,
        "SALAMANGREAT_PROFILE_ID": MODULE.SALAMANGREAT_PROFILE_ID,
        "SWORDSOUL_BINDING_ID": MODULE.SWORDSOUL_BINDING_ID,
        "SALAMANGREAT_BINDING_ID": MODULE.SALAMANGREAT_BINDING_ID,
        "SWORDSOUL_POLICY_ARTIFACT_ID": MODULE.SWORDSOUL_ARTIFACT_ID,
        "SALAMANGREAT_POLICY_ARTIFACT_ID": MODULE.SALAMANGREAT_ARTIFACT_ID,
        "DISPOSITION": "CLEAN_ADMITTED",
        "RECORD_COUNT": "4",
        "BATTLE_DECISION_RECORD_COUNT": "1",
        "BATTLE_COMMAND_CANDIDATE_COUNT": "2",
        "SIDECAR_INVALID_COUNT": "0",
        "PROVEN_LETHAL_COUNT": "0",
        "LOWER_BOUND_PRESENT_COUNT": "0",
        "PUBLIC_GAMEPLAY_TRAJECTORY_ID": "public_gameplay_trajectory.v1." + "a" * 64,
        "TRAJECTORY_RECORD_ID": "trajectory_record.v1." + "b" * 64,
        "DATASET_SEMANTIC_ID": "c" * 64,
        "SIDECAR_INFLUENCES_GAMEPLAY": "NO",
        "POSITIVE_LETHAL_CAPABILITY": (
            "BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT"
        ),
        "ROW_STATUS": "PASS",
    }
    return values


def valid_matrix() -> list[dict[str, str]]:
    return [valid_row(row, starting_player) for row, starting_player in MODULE.ROWS]


def rejects(mutated: list[dict[str, str]]) -> None:
    try:
        MODULE.validate_matrix(mutated)
    except ValueError:
        return
    raise AssertionError("validator accepted invalid synthetic evidence")


def main() -> int:
    MODULE.validate_matrix(valid_matrix())

    rejects(valid_matrix()[:-1])
    duplicated = valid_matrix()
    duplicated.append(copy.deepcopy(duplicated[0]))
    rejects(duplicated)

    for field, value in (
        ("STARTING_PLAYER", "2"),
        ("ROOT_SEED", "3"),
        ("SEMANTIC_ACTION_BUDGET", "31"),
        ("ENGINE_PROCESS_BUDGET", "4095"),
        ("MATCHUP_ID", "ocgforge.other.matchup.v1"),
        ("RULES_BUNDLE_ID", "a" * 64),
        ("SWORDSOUL_PROFILE_ID", "ocgforge.strategy_profile.v1." + "a" * 64),
        ("SWORDSOUL_BINDING_ID", "ocgforge.teacher_policy_binding.v1." + "a" * 64),
        ("SWORDSOUL_POLICY_ARTIFACT_ID", "policy_artifact.v1." + "a" * 64),
        ("DISPOSITION", "FAILED"),
        ("SIDECAR_INVALID_COUNT", "1"),
        ("PROVEN_LETHAL_COUNT", "1"),
        ("LOWER_BOUND_PRESENT_COUNT", "1"),
        ("SIDECAR_INFLUENCES_GAMEPLAY", "YES"),
        ("DATASET_SEMANTIC_ID", ""),
        ("TRAJECTORY_RECORD_ID", ""),
    ):
        mutated = valid_matrix()
        mutated[0][field] = value
        rejects(mutated)

    duplicate_key = (
        "ROW=normal\n"
        "ROW=normal\n"
    ).encode("utf-8")
    try:
        MODULE.parse_output(duplicate_key)
    except ValueError:
        pass
    else:
        raise AssertionError("parser accepted duplicate evidence keys")

    zero_coverage = valid_matrix()
    for row in zero_coverage:
        row["BATTLE_DECISION_RECORD_COUNT"] = "0"
        row["BATTLE_COMMAND_CANDIDATE_COUNT"] = "0"
    rejects(zero_coverage)

    print("phase4c_teacher_evaluation_test=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
