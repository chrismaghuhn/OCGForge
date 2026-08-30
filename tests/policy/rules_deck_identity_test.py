from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / "third_party" / "rules_bundle.lock.json"
MATCHUP = ROOT / "fixtures" / "decks" / "ocgforge.matchup.swordsoul_salamangreat.v1.json"

EXPECTED = {
    "rules_bundle_id": "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f",
    "matchup_id": "ocgforge.matchup.swordsoul_salamangreat.v1",
    "deck_a_id": "ocgforge.swordsoul_tenyi.ml_v1",
    "deck_a_sha256": "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7",
    "deck_b_id": "ocgforge.salamangreat.ml_v1",
    "deck_b_sha256": "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_ydk(path: Path) -> tuple[list[int], list[int]]:
    main: list[int] = []
    extra: list[int] = []
    section = main
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith(("#", "!")):
            if line == "#extra":
                section = extra
            elif line == "!side":
                break
            continue
        section.append(int(line))
    return main, extra


def main() -> None:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    matchup = json.loads(MATCHUP.read_text(encoding="utf-8"))
    for key, expected in EXPECTED.items():
        actual = matchup.get(key)
        require(actual == expected, f"frozen identity changed: {key}")
    require(lock.get("bundle_id") == EXPECTED["rules_bundle_id"],
            "rules lock bundle identity changed")

    require(matchup["main_deck_count"] == {"deck_a": 40, "deck_b": 40},
            "frozen main-deck counts changed")
    require(matchup["extra_deck_count"] == {"deck_a": 15, "deck_b": 15},
            "frozen Extra Deck counts changed")
    require(matchup["side_deck_policy"] == "none", "side-deck policy changed")

    deck_paths = {
        "deck_a": ROOT / "fixtures" / "decks" / "swordsoul_tenyi_ml_v1.ydk",
        "deck_b": ROOT / "fixtures" / "decks" / "salamangreat_ml_v1.ydk",
    }
    for deck, path in deck_paths.items():
        main, extra = parse_ydk(path)
        require(len(main) == 40 and len(extra) == 15,
                f"frozen deck counts changed on disk: {deck}")
        require(matchup["main_deck_passcodes"][deck] == main,
                f"frozen Main Deck passcodes changed: {deck}")
        require(matchup["extra_deck_passcodes"][deck] == extra,
                f"frozen Extra Deck passcodes changed: {deck}")
        expected_hash = EXPECTED[f"{deck}_sha256"]
        require(file_sha256(path) == expected_hash,
                f"frozen deck digest changed: {deck}")

    inputs = lock["rule_affecting_inputs"]
    require(inputs["core"]["patchset"]["id"] == "ocgforge.ocgcore.api_hardening.v1",
            "frozen core patchset identity changed")
    require(inputs["core"]["patchset"]["sha256"] ==
            "6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd",
            "frozen core patchset digest changed")
    require(lock["format_id"] == "TCG_ADVANCED_2026_05_18" and
            lock["duel_mode"] == "DUEL_MODE_MR5" and
            lock["duel_flags"]["value"] == 190464,
            "frozen rules format changed")
    print("rules_deck_identity=ok")


if __name__ == "__main__":
    main()
