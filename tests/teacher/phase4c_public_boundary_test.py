from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "include" / "ygo" / "teacher" / "public_battle_snapshot.hpp"


def main() -> int:
    text = HEADER.read_text(encoding="utf-8")
    required = (
        "extract_public_battle_snapshot",
        "PublicEnvironmentObservation",
        "EnvironmentActionCandidate",
        "PublicBattleSnapshotExtractionResult",
    )
    forbidden = (
        "DecisionFrame",
        "SubmissionToken",
        "CoreHost",
        "PlayerObservation",
        "EnvironmentDecisionRequest",
        "ActionSelection",
        "semantic_key",
        "response bytes",
        "raw engine",
    )
    missing = [value for value in required if value not in text]
    exposed = [value for value in forbidden if value in text]
    if missing:
        raise SystemExit(f"missing public battle API markers: {missing}")
    if exposed:
        raise SystemExit(f"forbidden public battle API markers: {exposed}")
    print("phase4c_public_boundary=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
