from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SWORDSOUL_PROFILE = ROOT / "fixtures" / "teacher_profiles" / "ocgforge.swordsoul_tenyi.v1.json"
SALAMANGREAT_PROFILE = ROOT / "fixtures" / "teacher_profiles" / "ocgforge.salamangreat.v1.json"
MATCHUP = ROOT / "fixtures" / "decks" / "ocgforge.matchup.swordsoul_salamangreat.v1.json"

COMMON_EXPECTED = {
    "matchup_id": "ocgforge.matchup.swordsoul_salamangreat.v1",
    "rules_bundle_id": "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
    "format_id": "TCG_ADVANCED_2026_05_18",
    "duel_mode": "DUEL_MODE_MR5",
    "duel_flags": 190464,
}

PROFILES = (
    (
        SWORDSOUL_PROFILE,
        0,
        "ocgforge.swordsoul_tenyi.ml_v1",
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
        1,
        "ocgforge.salamangreat.ml_v1",
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
        "deck_a",
    ),
    (
        SALAMANGREAT_PROFILE,
        1,
        "ocgforge.salamangreat.ml_v1",
        "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
        0,
        "ocgforge.swordsoul_tenyi.ml_v1",
        "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
        "deck_b",
    ),
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    matchup = json.loads(MATCHUP.read_text(encoding="utf-8"))
    for (path, own_role, own_id, own_hash, opponent_role, opponent_id,
         opponent_hash, deck_key) in PROFILES:
        require(path.is_file(), f"profile fixture is missing: {path}")
        profile = json.loads(path.read_text(encoding="utf-8"))
        for key, expected in COMMON_EXPECTED.items():
            require(profile.get(key) == expected, f"profile binding mismatch: {path.name}:{key}")
        expected_binding = {
            "own_deck_role": own_role,
            "own_deck_id": own_id,
            "own_deck_sha256": own_hash,
            "opponent_deck_role": opponent_role,
            "opponent_deck_id": opponent_id,
            "opponent_deck_sha256": opponent_hash,
        }
        for key, expected in expected_binding.items():
            require(profile.get(key) == expected, f"profile binding mismatch: {path.name}:{key}")
        locked_passcodes = set(matchup["main_deck_passcodes"][deck_key])
        locked_passcodes.update(matchup["extra_deck_passcodes"][deck_key])
        for entry in profile.get("card_roles", []):
            require(entry.get("passcode") in locked_passcodes,
                    f"{path.name} references a passcode outside {deck_key}")
    print("teacher_profile_binding=ok profiles=2")


if __name__ == "__main__":
    main()
