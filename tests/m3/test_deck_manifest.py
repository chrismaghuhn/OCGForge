import hashlib
import unittest
from pathlib import Path

from tools.m3.decks import DeckFormatError, parse_ydk


class DeckManifestTests(unittest.TestCase):
    def test_parse_ydk_preserves_sections_and_hashes_exact_source_bytes(self):
        source = "#created by OCGForge\n#main\n1\n2\n#extra\n3\n!side\n"
        deck = parse_ydk(source.encode("utf-8"), require_main=2, require_extra=1)

        self.assertEqual(deck.main, (1, 2))
        self.assertEqual(deck.extra, (3,))
        self.assertEqual(deck.side, ())
        self.assertEqual(deck.sha256, hashlib.sha256(source.encode("utf-8")).hexdigest())

    def test_parse_ydk_rejects_missing_extra_cards(self):
        with self.assertRaises(DeckFormatError):
            parse_ydk(b"#main\n1\n2\n#extra\n", require_main=2, require_extra=1)

    def test_parse_ydk_rejects_nonempty_side_deck(self):
        with self.assertRaises(DeckFormatError):
            parse_ydk(b"#main\n1\n#extra\n2\n!side\n3\n", require_main=1, require_extra=1)

    def test_locked_deck_files_have_exact_shape(self):
        root = Path(__file__).resolve().parents[2]
        for filename in ("swordsoul_tenyi_ml_v1.ydk", "salamangreat_ml_v1.ydk"):
            path = root / "fixtures" / "decks" / filename
            self.assertTrue(path.exists(), filename)
            deck = parse_ydk(path.read_bytes(), require_main=40, require_extra=15)
            self.assertEqual(len(deck.main), 40)
            self.assertEqual(len(deck.extra), 15)
            self.assertEqual(deck.side, ())


if __name__ == "__main__":
    unittest.main()
