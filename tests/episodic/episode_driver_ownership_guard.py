from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CANONICAL = ROOT / "src" / "simulation" / "canonical_simulation.cpp"

FORBIDDEN_AUTHORITATIVE_PATTERNS = (
    r"\bCoreHost\b",
    r"\bObservationSession\b",
    r"\bdecode_messages\s*\(",
    r"\bbuild_player_observation\s*\(",
    r"\battach_decision_context\s*\(",
    r"\bcandidate_observation_consistent\s*\(",
    r"\bapply_continuation_action\s*\(",
    r"\bmake_decision_step\s*\(",
    r"\bsubmit_response\s*\(",
    r"\bprocess\s*\(",
)


def main() -> int:
    source = CANONICAL.read_text(encoding="utf-8")
    if "environment::EpisodeDriver" not in source:
        raise AssertionError("canonical simulation does not construct EpisodeDriver")
    if "driver.advance_until_boundary()" not in source or "driver.apply_semantic_key(" not in source:
        raise AssertionError("canonical simulation does not use the Driver boundary")
    for pattern in FORBIDDEN_AUTHORITATIVE_PATTERNS:
        match = re.search(pattern, source)
        if match is not None:
            raise AssertionError(f"canonical simulation retains forbidden authority call: {pattern}")
    print("episode_driver_ownership=ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
