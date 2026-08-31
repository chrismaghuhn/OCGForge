from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

# This is an acceptance matrix, not a runtime registry. The C++ registry
# remains authoritative; the executable probe below must match this complete
# expected coverage exactly.
EXPECTED_FACTS = {
    "blocked.continuation.assigned_amounts": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.assigned_amounts"),
    "blocked.continuation.available_mask": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.available_mask"),
    "blocked.continuation.can_cancel": ("BOOLEAN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.can_cancel"),
    "blocked.continuation.can_finish": ("BOOLEAN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.can_finish"),
    "blocked.continuation.max_count": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.max_count"),
    "blocked.continuation.min_count": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.min_count"),
    "blocked.continuation.remaining_indices": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.remaining_indices"),
    "blocked.continuation.required_amount": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.required_amount"),
    "blocked.continuation.selected_indices": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.selected_indices"),
    "blocked.continuation.selected_mask": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.selected_mask"),
    "blocked.continuation.target_sum": ("U64", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:continuation.target_sum"),
    "blocked.private.effect_use_state": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.effect_use_state"),
    "blocked.private.exact_face_down_identity": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.exact_face_down_identity"),
    "blocked.private.hidden_deck_order": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.hidden_deck_order"),
    "blocked.private.hidden_extra_deck_identity": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.hidden_extra_deck_identity"),
    "blocked.private.locator_cache": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.locator_cache"),
    "blocked.private.opponent_hand_identity": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.opponent_hand_identity"),
    "blocked.private.physical_identity_after_shuffle": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.physical_identity_after_shuffle"),
    "blocked.private.raw_engine_query_state": ("TOKEN", "CURRENT_RECONCILIATION", "BLOCKED", "blocked:private.raw_engine_query_state"),
    "public.chain.length": ("U64", "CURRENT_RECONCILIATION", "DIRECT", "public_safe_state.globals.chain_length"),
    "public.current_actor_is_perspective": ("BOOLEAN", "CURRENT_RECONCILIATION", "SAFE_DERIVATION", "public_safe_state.globals.player_to_act_vs_perspective"),
    "public.decision_context.kind": ("TOKEN", "CURRENT_RECONCILIATION", "DIRECT", "observation.decision_context.kind"),
    "public.last_event.amount": ("I32", "CURRENT_RECONCILIATION", "SAFE_DERIVATION", "public_safe_state.visible_events.last.amount"),
    "public.life_points.opponent": ("U64", "CURRENT_RECONCILIATION", "DIRECT", "public_safe_state.globals.life_points.opponent"),
    "public.life_points.self": ("U64", "CURRENT_RECONCILIATION", "DIRECT", "public_safe_state.globals.life_points.self"),
    "public.perspective_player": ("U64", "CURRENT_RECONCILIATION", "DIRECT", "observation.perspective_player"),
    "public.terminal": ("BOOLEAN", "CURRENT_RECONCILIATION", "DIRECT", "public_safe_state.globals.terminal"),
    "public.turn.count": ("U64", "CURRENT_RECONCILIATION", "DIRECT", "public_safe_state.globals.turn_count"),
    "public.turn.phase": ("U64", "CURRENT_RECONCILIATION", "DIRECT", "public_safe_state.globals.phase"),
    "public.visible.entity_count": ("U64", "CURRENT_RECONCILIATION", "SAFE_DERIVATION", "public_safe_state.entities.size"),
    "public.visible.event_count": ("U64", "CURRENT_RECONCILIATION", "SAFE_DERIVATION", "public_safe_state.visible_events.size"),
    "public.visible.face_down_present": ("BOOLEAN", "CURRENT_RECONCILIATION", "SAFE_DERIVATION", "public_safe_state.entities.face_down_any"),
}

EXPECTED_BLOCKED = {fact_id for fact_id, row in EXPECTED_FACTS.items() if row[2] == "BLOCKED"}


def fail(message: str) -> None:
    raise AssertionError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def probe_path() -> Path:
    configured = os.environ.get("OCGFORGE_BUILD_DIR")
    candidates = []
    if configured:
        candidates.append(ROOT / configured / "teacher_evaluator_test.exe")
    candidates.extend(
        (
            ROOT / "build" / "windows-zig" / "teacher_evaluator_test.exe",
            ROOT / "build" / "dev-windows" / "teacher_evaluator_test.exe",
        )
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    fail("teacher_evaluator_test executable is missing")


def read_registry_probe() -> list[tuple[str, str, str, str, str]]:
    result = subprocess.run(
        [str(probe_path()), "--fact-matrix"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    require(result.returncode == 0, f"fact registry probe failed: {result.stderr.decode()}")
    require(not result.stderr, "fact registry probe wrote diagnostics on success")
    rows = []
    for line in result.stdout.decode("utf-8").splitlines():
        cells = line.split("|")
        require(len(cells) == 5, f"fact registry row has wrong shape: {line}")
        rows.append(tuple(cells))
    return rows


def main() -> None:
    rows = read_registry_probe()
    ids = [row[0] for row in rows]
    require(ids == sorted(ids), "fact registry iteration is not canonical ID order")
    require(len(ids) == len(set(ids)), "fact registry contains duplicate IDs")
    require(set(ids) == set(EXPECTED_FACTS), "registry and acceptance matrix coverage differ")

    for fact_id, kind, scope, classification, source in rows:
        expected = EXPECTED_FACTS[fact_id]
        require((kind, scope, classification, source) == expected,
                f"fact registry row differs from matrix: {fact_id}")
        require(classification in {"DIRECT", "SAFE_DERIVATION", "BLOCKED"},
                f"fact has unknown classification: {fact_id}")
        require(source, f"fact has no source/classification rule: {fact_id}")
        if classification == "BLOCKED":
            require(fact_id in EXPECTED_BLOCKED, f"unexpected blocked fact: {fact_id}")
            require(source.startswith("blocked:"), f"blocked fact lacks explicit reason: {fact_id}")
        else:
            require(fact_id not in EXPECTED_BLOCKED, f"expected blocked fact is publishable: {fact_id}")
            require(source.startswith(("observation.", "public_safe_state.")),
                    f"non-blocked fact source is not public: {fact_id}")

    for required in (
        "blocked.private.opponent_hand_identity",
        "blocked.private.hidden_deck_order",
        "blocked.private.exact_face_down_identity",
        "blocked.private.physical_identity_after_shuffle",
        "blocked.private.locator_cache",
        "blocked.continuation.selected_indices",
        "blocked.continuation.remaining_indices",
        "blocked.continuation.assigned_amounts",
        "blocked.continuation.min_count",
        "blocked.continuation.max_count",
        "blocked.continuation.target_sum",
        "blocked.continuation.required_amount",
        "blocked.continuation.available_mask",
        "blocked.continuation.selected_mask",
        "blocked.continuation.can_finish",
        "blocked.continuation.can_cancel",
        "blocked.private.effect_use_state",
        "blocked.private.raw_engine_query_state",
    ):
        require(required in EXPECTED_BLOCKED, f"required blocked category missing from matrix: {required}")

    print(f"teacher_public_fact_matrix=ok registered={len(rows)} blocked={len(EXPECTED_BLOCKED)}")


if __name__ == "__main__":
    main()
