from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROFILE = ROOT / "fixtures" / "teacher_profiles" / "ocgforge.swordsoul_tenyi.v1.json"
MATCHUP = ROOT / "fixtures" / "decks" / "ocgforge.matchup.swordsoul_salamangreat.v1.json"

EXPECTED = {
    "matchup_id": "ocgforge.matchup.swordsoul_salamangreat.v1",
    "rules_bundle_id": "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
    "format_id": "TCG_ADVANCED_2026_05_18",
    "duel_mode": "DUEL_MODE_MR5",
    "duel_flags": 190464,
    "own_deck_role": 0,
    "own_deck_id": "ocgforge.swordsoul_tenyi.ml_v1",
    "own_deck_sha256": "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
    "opponent_deck_role": 1,
    "opponent_deck_id": "ocgforge.salamangreat.ml_v1",
    "opponent_deck_sha256": "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require(PROFILE.is_file(), f"Swordsoul profile fixture is missing: {PROFILE}")
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    for key, expected in EXPECTED.items():
        require(profile.get(key) == expected, f"Swordsoul binding mismatch: {key}")
    matchup = json.loads(MATCHUP.read_text(encoding="utf-8"))
    locked_swordsoul_passcodes = set(matchup["main_deck_passcodes"]["deck_a"])
    locked_swordsoul_passcodes.update(matchup["extra_deck_passcodes"]["deck_a"])
    for entry in profile.get("card_roles", []):
        require(entry.get("passcode") in locked_swordsoul_passcodes,
                "Swordsoul profile references a passcode outside deck_a")
    print("teacher_profile_binding=ok")


if __name__ == "__main__":
    main()
