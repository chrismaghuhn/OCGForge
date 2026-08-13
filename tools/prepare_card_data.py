#!/usr/bin/env python3
"""Materialize only fixture card data from the pinned SQLite CDB."""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path


def read_codes(paths: list[Path]) -> list[int]:
    codes: list[int] = []
    for path in paths:
        for raw_line in path.read_text(encoding="utf-8").splitlines():
            line = raw_line.split("#", 1)[0].strip()
            if line:
                codes.append(int(line, 10))
    return sorted(set(codes))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--deck", type=Path, action="append", required=True)
    args = parser.parse_args()
    codes = read_codes(args.deck)
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
            stream.write(
                f"{code}|{alias}|{setcode}|{card_type}|{level}|{attribute}|{race}|"
                f"{atk}|{defense}|0|0|0\n"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
