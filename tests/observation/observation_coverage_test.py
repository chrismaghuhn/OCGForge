import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    with (ROOT / relative).open(encoding="utf-8") as stream:
        return json.load(stream)


def main():
    field = load("docs/observation/observation_field_coverage.json")
    events = load("docs/observation/event_coverage.json")
    lock = load("third_party/rules_bundle.lock.json")
    expected_bundle = lock["bundle_id"]
    assert field["authority"]["rules_bundle_id"] == expected_bundle
    assert events["authority"]["rules_bundle_id"] == expected_bundle

    classifications = {
        "EXPOSED_PUBLIC",
        "EXPOSED_PRIVATE_TO_OWNER",
        "REDACTED_WHEN_HIDDEN",
        "STATIC_CONTEXT",
        "DERIVED",
        "INTENTIONALLY_NOT_EXPOSED",
        "OUT_OF_SCOPE_M2",
    }
    field_rows = field["classifications"]
    assert field_rows
    field_names = [row["field"] for row in field_rows]
    assert len(field_names) == len(set(field_names))
    assert all(row["classification"] in classifications for row in field_rows)
    required_fields = {
        "zones.MainDeck.order",
        "zones.Hand.entities.opponent",
        "zones.ExtraDeck.entities.face_down_opponent",
        "QUERY_OVERLAY_CARD.material_identity",
        "observation_hash",
    }
    assert required_fields.issubset(field_names)

    event_classes = {"SUPPORTED", "NOT_NEEDED_FOR_M2_FIXTURES", "DEFERRED", "PRIVATE_INTERNAL"}
    event_rows = events["families"]
    assert event_rows
    assert all(row["classification"] in event_classes for row in event_rows)
    supported_messages = [row["message"] for row in event_rows if row["classification"] == "SUPPORTED"]
    for required_message in {"MSG_MOVE", "MSG_DRAW", "MSG_SHUFFLE_DECK", "MSG_CHAINING", "MSG_ADD_COUNTER"}:
        assert any(required_message in message.split("/") for message in supported_messages)
    print("observation_coverage=ok")


if __name__ == "__main__":
    main()
