from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "include" / "ygo" / "teacher" / "teacher_core.hpp"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    text = HEADER.read_text(encoding="utf-8")
    proposal = re.findall(r"\bTeacherRankingResult\s+propose\s*\((.*?)\)\s*const", text, re.S)
    require(len(proposal) == 1, "TeacherCore must expose exactly one proposal signature")
    signature = " ".join(proposal[0].split())
    for required in (
        "const ygo::policy::PolicyInput& input",
        "const StrategyProfileV1& profile",
        "const EpisodeLocalStrategyStateV1& state",
    ):
        require(required in signature, f"TeacherCore boundary is missing {required}")

    forbidden = (
        "DecisionFrame",
        "SubmissionToken",
        "CoreHost",
        "PlayerObservation",
        "ActionSelection",
        "EnvironmentDecisionRequest",
        "internal semantic key",
        "response bytes",
        "raw engine",
    )
    for value in forbidden:
        require(value not in text, f"forbidden TeacherCore boundary value is exposed: {value}")
    print("teacher_public_boundary=PASS")


if __name__ == "__main__":
    main()
