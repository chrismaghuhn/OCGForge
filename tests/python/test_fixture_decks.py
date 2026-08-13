import argparse
import hashlib
import sqlite3
from pathlib import Path


EXPECTED_HASHES = {
    "player_a.deck": "cfca09c881f9859f69f0ceab05fc9f1c71f37f9e8a5a72ac28f1f8b6c19e67bc",
    "player_b.deck": "dae2e76f2fd5f86c87da77c225556dce37d3121307d4b97635d4cf7d12196e97",
}


def read_deck(path: Path) -> list[int]:
    codes = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        codes.append(int(line))
    return codes


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--database", required=True, type=Path)
    parser.add_argument("--deck", required=True, type=Path, action="append")
    args = parser.parse_args()
    assert len(args.deck) == 2

    with sqlite3.connect(args.database) as connection:
        for deck_path in args.deck:
            codes = read_deck(deck_path)
            assert len(codes) >= 40, f"{deck_path} has fewer than 40 entries"
            digest = hashlib.sha256(deck_path.read_bytes()).hexdigest()
            assert digest == EXPECTED_HASHES[deck_path.name], (deck_path, digest)
            for code in codes:
                row = connection.execute("SELECT type FROM datas WHERE id = ?", (code,)).fetchone()
                assert row is not None, f"{code} missing from pinned BabelCDB"
                assert row[0] == 17, f"{code} is not a normal monster fixture card"

    print("fixture_decks=ok")


if __name__ == "__main__":
    main()
