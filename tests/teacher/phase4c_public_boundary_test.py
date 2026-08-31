from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADERS = (
    ROOT / "include" / "ygo" / "teacher" / "public_battle_snapshot.hpp",
    ROOT / "include" / "ygo" / "teacher" / "provable_lethal.hpp",
)


def main() -> int:
    texts = [header.read_text(encoding="utf-8") for header in HEADERS]
    required = (
        ("public_battle_snapshot.hpp", "extract_public_battle_snapshot"),
        ("public_battle_snapshot.hpp", "PublicEnvironmentObservation"),
        ("public_battle_snapshot.hpp", "EnvironmentActionCandidate"),
        ("public_battle_snapshot.hpp", "PublicBattleSnapshotExtractionResult"),
        ("provable_lethal.hpp", "evaluate_provable_lethal"),
        ("provable_lethal.hpp", "PublicBattleSnapshotV1"),
        ("provable_lethal.hpp", "ProvableLethalEvaluationResult"),
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
    missing = [value for name, value in required
               if value not in next(text for text, header in zip(texts, HEADERS)
                                    if header.name == name)]
    exposed = [value for value in forbidden if any(value in text for text in texts)]
    if missing:
        raise SystemExit(f"missing public battle API markers: {missing}")
    if exposed:
        raise SystemExit(f"forbidden public battle API markers: {exposed}")
    print("phase4c_public_boundary=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
