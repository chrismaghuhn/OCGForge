#!/usr/bin/env python3
"""Materialize only fixture card data from the pinned SQLite CDB."""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path


TYPE_LINK = 0x04000000
TYPE_PENDULUM = 0x01000000


def read_codes(paths: list[Path]) -> list[int]:
    codes: list[int] = []
    for path in paths:
        section: str | None = None
        for raw_line in path.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line:
                continue
            if line == "#main":
                section = "main"
                continue
            if line == "#extra":
                section = "extra"
                continue
            if line == "!side":
                section = "side"
                continue
            if line.startswith("#"):
                continue
            if section == "side":
                raise ValueError(f"fixture side deck must be empty: {path}")
            codes.append(int(line, 10))
    return sorted(set(codes))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--deck", type=Path, action="append", required=True)
    parser.add_argument("--code", type=int, action="append", default=[])
    args = parser.parse_args()
    codes = sorted(set(read_codes(args.deck)) | set(args.code))
    if not codes:
        raise SystemExit("fixture decks contain no card passcodes")

    query = "select id, alias, setcode, type, level, attribute, race, atk, def " \
        "from datas where id in (" + ",".join("?" for _ in codes) + ") order by id"
    with sqlite3.connect(args.database) as connection:
        rows = connection.execute(query, codes).fetchall()
    found = {row[0] for row in rows}
    missing = sorted(set(codes) - found)
    if missing:
        raise SystemExit(f"fixture passcodes missing from database: {missing}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("# code|alias|setcode|type|level|attribute|race|atk|def|lscale|rscale|link_marker\n")
        for code, alias, setcode, card_type, level, attribute, race, atk, defense in rows:
            raw_level = int(level)
            decoded_level = raw_level & 0xFF
            lscale = (raw_level >> 16) & 0xFF if card_type & TYPE_PENDULUM else 0
            rscale = (raw_level >> 24) & 0xFF if card_type & TYPE_PENDULUM else 0
            link_marker = int(defense) if card_type & TYPE_LINK else 0
            decoded_defense = 0 if card_type & TYPE_LINK else int(defense)
            stream.write(
                f"{code}|{alias}|{setcode}|{card_type}|{decoded_level}|{attribute}|{race}|"
                f"{atk}|{decoded_defense}|{lscale}|{rscale}|{link_marker}\n"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
