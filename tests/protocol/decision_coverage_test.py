"""Validate the pinned-core interactive decision coverage inventory."""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INVENTORY = ROOT / "docs" / "protocol" / "decision_coverage.json"
COVERAGE_DOC = ROOT / "docs" / "protocol" / "DECISION_COVERAGE.md"
MATRIX_DOC = ROOT / "docs" / "protocol" / "M1_ACCEPTANCE_MATRIX.md"

EXPECTED_MESSAGES = {
    "MSG_REQUEST_DECK": 8,
    "MSG_SELECT_BATTLECMD": 10,
    "MSG_SELECT_IDLECMD": 11,
    "MSG_SELECT_EFFECTYN": 12,
    "MSG_SELECT_YESNO": 13,
    "MSG_SELECT_OPTION": 14,
    "MSG_SELECT_CARD": 15,
    "MSG_SELECT_CHAIN": 16,
    "MSG_SELECT_PLACE": 18,
    "MSG_SELECT_POSITION": 19,
    "MSG_SELECT_TRIBUTE": 20,
    "MSG_SORT_CHAIN": 21,
    "MSG_SELECT_COUNTER": 22,
    "MSG_SELECT_SUM": 23,
    "MSG_SELECT_DISFIELD": 24,
    "MSG_SORT_CARD": 25,
    "MSG_SELECT_UNSELECT_CARD": 26,
    "MSG_ROCK_PAPER_SCISSORS": 132,
    "MSG_ANNOUNCE_RACE": 140,
    "MSG_ANNOUNCE_ATTRIB": 141,
    "MSG_ANNOUNCE_CARD": 142,
    "MSG_ANNOUNCE_NUMBER": 143,
}

ALLOWED_STATUSES = {
    "SUPPORTED_ENGINE_VERIFIED",
    "SUPPORTED_PROTOCOL_VERIFIED",
    "UNSUPPORTED_FAIL_CLOSED",
    "NON_INTERACTIVE",
    "OUT_OF_SCOPE_M1",
}

REQUIRED_STATUS_DEFINITIONS = {
    "SUPPORTED_ENGINE_VERIFIED": (
        "A pinned real ocgcore fixture has emitted the relevant interactive decision, "
        "OCGForge exposed it correctly, a valid semantic response was selected and "
        "encoded, ocgcore accepted it, and execution continued without retry/protocol failure."
    ),
    "SUPPORTED_PROTOCOL_VERIFIED": (
        "The parser, legal candidate generation, continuation semantics, response encoder, "
        "and isolated/property/oracle tests are verified, but no actual pinned-core fixture "
        "has yet proven the full decision path."
    ),
    "UNSUPPORTED_FAIL_CLOSED": (
        "The decision family is recognized as unsupported and fails closed without guessing "
        "or silently selecting an action."
    ),
    "OUT_OF_SCOPE_M1": (
        "The decision family is intentionally outside the M1/M1.1 scope and is not counted "
        "as supported coverage."
    ),
}

REQUIRED_ENGINE_EVIDENCE_FIELDS = {
    "fixture_id",
    "test_id",
    "rules_bundle_id",
    "status",
}


def test_status_contract_and_engine_evidence() -> None:
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    assert inventory["status_definitions"] == REQUIRED_STATUS_DEFINITIONS

    for entry in inventory["messages"]:
        status = entry["m1_target_status"]
        assert status in REQUIRED_STATUS_DEFINITIONS, (
            f"unknown M1 target status for {entry['message_name']}: {status}"
        )
        evidence = entry.get("engine_verification")
        if status == "SUPPORTED_ENGINE_VERIFIED":
            assert isinstance(evidence, dict), (
                f"engine-verified family lacks engine_verification: {entry['message_name']}"
            )
            assert REQUIRED_ENGINE_EVIDENCE_FIELDS <= evidence.keys(), (
                f"incomplete engine evidence: {entry['message_name']}"
            )
            assert evidence["fixture_id"] and evidence["test_id"], (
                f"engine-verified family lacks fixture/test identifiers: {entry['message_name']}"
            )
            assert evidence["rules_bundle_id"] == inventory["authority"]["rules_bundle_id"]
            assert evidence["status"] == "PASS"
        else:
            assert evidence is None, (
                f"non-engine family has engine evidence: {entry['message_name']}"
            )


def test_inventory_covers_every_pinned_interactive_message() -> None:
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    assert inventory["schema_version"] == 1
    assert inventory["authority"]["core_commit"] == "9a0c558c2d686542f7914a6d529fd7aa57746aed"
    messages = inventory["messages"]
    assert {entry["message_name"]: entry["numeric_id"] for entry in messages} == EXPECTED_MESSAGES
    assert len(messages) == len(EXPECTED_MESSAGES)
    for entry in messages:
        assert entry["current_m0_status"] in ALLOWED_STATUSES
        assert entry["m1_target_status"] in ALLOWED_STATUSES
        for field in (
            "request_structure",
            "response_structure",
            "selection_order_matters",
            "combinatorial",
            "continuation_appropriate",
            "engine_fixture_coverage",
            "unit_protocol_coverage",
            "known_edge_cases",
            "source_evidence",
        ):
            assert field in entry, f"missing {field} for {entry['message_name']}"
    actual_counts = {
        status: sum(entry["m1_target_status"] == status for entry in messages)
        for status in REQUIRED_STATUS_DEFINITIONS
    }
    assert inventory["coverage_summary"]["after_m1_1"] == actual_counts
    assert inventory["coverage_summary"]["before_m1_1"] == {
        "SUPPORTED_ENGINE_VERIFIED": 1,
        "SUPPORTED_PROTOCOL_VERIFIED": 18,
        "UNSUPPORTED_FAIL_CLOSED": 1,
        "OUT_OF_SCOPE_M1": 2,
    }


def test_fail_closed_inventory_entries_explain_their_blocker() -> None:
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    for entry in inventory["messages"]:
        if entry["m1_target_status"] in {"UNSUPPORTED_FAIL_CLOSED", "OUT_OF_SCOPE_M1"}:
            assert entry["known_edge_cases"], entry["message_name"]
            assert entry["unit_protocol_coverage"], entry["message_name"]


def _markdown_statuses(path: Path, status_column: int) -> dict[str, str]:
    statuses: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("| `MSG_"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        message_name = cells[0].strip("`")
        statuses[message_name] = cells[status_column].strip("`")
    return statuses


def test_markdown_classifications_match_machine_inventory() -> None:
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    expected = {entry["message_name"]: entry["m1_target_status"] for entry in inventory["messages"]}
    assert _markdown_statuses(COVERAGE_DOC, 3) == expected
    assert _markdown_statuses(MATRIX_DOC, 2) == expected
    coverage_text = COVERAGE_DOC.read_text(encoding="utf-8")
    for definition in inventory["status_definitions"].values():
        if definition not in coverage_text:
            raise AssertionError(f"coverage document omits status definition: {definition}")


if __name__ == "__main__":
    test_status_contract_and_engine_evidence()
    test_inventory_covers_every_pinned_interactive_message()
    test_fail_closed_inventory_entries_explain_their_blocker()
    test_markdown_classifications_match_machine_inventory()
    print("decision_coverage=ok")
