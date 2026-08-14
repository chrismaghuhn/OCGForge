from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .catalog import PinnedCatalog
from .decks import ParsedDeck, parse_ydk_file
from .locked_lists import LOCKED_DECKS, LockedDeck


FORMAT_ID = "TCG_ADVANCED_2026_05_18"
MATCHUP_ID = "ocgforge.matchup.swordsoul_salamangreat.v1"


def _load_lock(path: str | Path) -> dict[str, Any]:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def _bundle_fields(lock: dict[str, Any]) -> dict[str, Any]:
    inputs = lock["rule_affecting_inputs"]
    return {
        "rules_bundle_id": lock["bundle_id"],
        "format_id": lock["format_id"],
        "duel_mode": lock["duel_mode"],
        "duel_flags": lock["duel_flags"]["value"],
        "ocgcore_commit": inputs["core"]["commit"],
        "core_patchset_id": inputs["core"]["patchset"]["id"],
        "core_patchset_sha256": inputs["core"]["patchset"]["sha256"],
        "cardscripts_commit": inputs["card_scripts"]["commit"],
        "babelcdb_commit": inputs["database"]["commit"],
        "ocg_api_version": inputs["core"]["api_version"],
    }


def _validate_names(deck: ParsedDeck, spec: LockedDeck, catalog: PinnedCatalog) -> None:
    expected_main = spec.expand(spec.main)
    expected_extra = spec.expand(spec.extra)
    actual_main = tuple(catalog.by_code(code).name if catalog.by_code(code) else None for code in deck.main)
    actual_extra = tuple(catalog.by_code(code).name if catalog.by_code(code) else None for code in deck.extra)
    if actual_main != expected_main or actual_extra != expected_extra:
        raise ValueError(f"locked deck names do not match passcodes for {spec.deck_id}")


def _slot_rows(deck_id: str, deck: ParsedDeck, spec: LockedDeck, catalog: PinnedCatalog) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    expected_zones = (("main", deck.main, spec.expand(spec.main)), ("extra", deck.extra, spec.expand(spec.extra)))
    for zone, codes, names in expected_zones:
        for index, (code, declared_name) in enumerate(zip(codes, names)):
            row = catalog.by_code(code)
            row_dict: dict[str, object] = {
                "deck_id": deck_id,
                "zone": zone,
                "slot": index,
                "passcode": code,
                "declared_name": declared_name,
                "cdb_row_exists": row is not None,
                "cdb_name": row.name if row else None,
                "zone_type_valid": False,
            }
            if row:
                row_dict["zone_type_valid"] = not row.is_extra_deck_capable if zone == "main" else row.is_extra_deck_capable
                row_dict["static"] = row.as_dict()
            rows.append(row_dict)
    return rows


def build_matchup_audit(*, deck_a: str | Path, deck_b: str | Path, database: str | Path, scripts: str | Path, lock: str | Path) -> dict[str, object]:
    parsed = {
        "deck_a": parse_ydk_file(deck_a, require_main=40, require_extra=15),
        "deck_b": parse_ydk_file(deck_b, require_main=40, require_extra=15),
    }
    catalog = PinnedCatalog(database, scripts)
    for deck_id, spec in LOCKED_DECKS.items():
        _validate_names(parsed[deck_id], spec, catalog)

    slots = _slot_rows("deck_a", parsed["deck_a"], LOCKED_DECKS["deck_a"], catalog)
    slots.extend(_slot_rows("deck_b", parsed["deck_b"], LOCKED_DECKS["deck_b"], catalog))
    unique_codes = sorted({int(row["passcode"]) for row in slots})
    unique_cards: list[dict[str, object]] = []
    for code in unique_codes:
        row = catalog.by_code(code)
        if row is None:
            unique_cards.append({"passcode": code, "status": "MISSING_CDB", "script_required": False})
            continue
        script = catalog.script(row)
        status = "PASS_STATIC_ONLY" if (not row.script_required or script.load_result == "PASS") else "MISSING_SCRIPT"
        unique_cards.append({
            "passcode": code,
            "cdb_name": row.name,
            "cdb_row_exists": True,
            "script_required": row.script_required,
            "script": script.as_dict(),
            "static": row.as_dict(),
            "status": status,
        })

    bundle = _bundle_fields(_load_lock(lock))
    manifest = {
        "matchup_id": MATCHUP_ID,
        "format_id": FORMAT_ID,
        "deck_a_id": LOCKED_DECKS["deck_a"].deck_id,
        "deck_b_id": LOCKED_DECKS["deck_b"].deck_id,
        "main_deck_passcodes": {key: list(value.main) for key, value in parsed.items()},
        "extra_deck_passcodes": {key: list(value.extra) for key, value in parsed.items()},
        "main_deck_count": {key: len(value.main) for key, value in parsed.items()},
        "extra_deck_count": {key: len(value.extra) for key, value in parsed.items()},
        "deck_a_sha256": parsed["deck_a"].sha256,
        "deck_b_sha256": parsed["deck_b"].sha256,
        "unique_passcodes": unique_codes,
        "side_deck_policy": "none",
        **bundle,
    }
    return {**manifest, "manifest": manifest, "slots": slots, "unique_cards": unique_cards}


def _json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _compatibility_markdown(audit: dict[str, object]) -> str:
    manifest = audit["manifest"]
    slots = audit["slots"]
    unique_cards = audit["unique_cards"]
    lines = [
        "# M3 Card Compatibility",
        "",
        f"Matchup: {manifest['matchup_id']}",
        f"Rules bundle: {manifest['rules_bundle_id']}",
        "",
        f"110-slot audit rows: {len(slots)}",
        f"Unique passcodes: {len(unique_cards)}",
        "",
        "## Slot audit",
        "",
        "| Deck | Zone | Slot | Passcode | Declared name | CDB name | CDB row | Zone type |",
        "| --- | --- | ---: | ---: | --- | --- | --- | --- |",
    ]
    for row in slots:
        lines.append(
            f"| {row['deck_id']} | {row['zone']} | {row['slot']} | {row['passcode']} | "
            f"{row['declared_name']} | {row['cdb_name']} | {row['cdb_row_exists']} | {row['zone_type_valid']} |"
        )
    lines.extend([
        "",
        "## Unique-card audit",
        "",
        "| Passcode | Name | Script required | Script result | Status |",
        "| ---: | --- | --- | --- | --- |",
    ])
    for row in unique_cards:
        script = row.get("script", {})
        lines.append(
            f"| {row['passcode']} | {row.get('cdb_name', '')} | {row.get('script_required', False)} | "
            f"{script.get('load_result', 'N/A')} | {row['status']} |"
        )
    lines.extend([
        "",
        "## Canonical duplicate-name policy",
        "",
        "Exact-name alternatives are retained as audit evidence. The selected passcode is the lowest script-backed pinned row for required effect/procedure cards.",
        "",
    ])
    return "\n".join(lines)


def write_audit_reports(
    audit: dict[str, object],
    *,
    manifest: str | Path,
    compatibility_json: str | Path,
    compatibility_markdown: str | Path,
) -> None:
    manifest_path = Path(manifest)
    json_path = Path(compatibility_json)
    markdown_path = Path(compatibility_markdown)
    for path in (manifest_path, json_path, markdown_path):
        path.parent.mkdir(parents=True, exist_ok=True)
    compatibility = {
        "manifest": audit["manifest"],
        "slots": audit["slots"],
        "unique_cards": audit["unique_cards"],
    }
    manifest_path.write_bytes(_json_bytes(audit["manifest"]))
    json_path.write_bytes(_json_bytes(compatibility))
    markdown_path.write_text(_compatibility_markdown(audit), encoding="utf-8", newline="\n")
