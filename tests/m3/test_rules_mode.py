from __future__ import annotations

import json
from pathlib import Path
import unittest

from tools.m3.rules_mode import (
    CANONICAL_DUEL_FLAGS,
    CANONICAL_DUEL_MODE,
    CANONICAL_FORMAT_ID,
    load_canonical_environment,
)


ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = ROOT / "third_party" / "rules_bundle.lock.json"
class RulesModeTest(unittest.TestCase):
    def test_locked_format_resolves_to_canonical_mr5(self) -> None:
        environment = load_canonical_environment(LOCK_PATH)
        self.assertEqual(environment["format_id"], CANONICAL_FORMAT_ID)
        self.assertEqual(environment["duel_mode_name"], CANONICAL_DUEL_MODE)
        self.assertEqual(environment["duel_flags"], CANONICAL_DUEL_FLAGS)

    def test_locked_environment_identity_includes_canonical_mode(self) -> None:
        lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
        inputs = lock["rule_affecting_inputs"]
        self.assertIn("format_id", inputs)
        self.assertIn("duel_mode", inputs)
        self.assertIn("duel_flags", inputs)

    def test_acceptance_cmake_has_no_divergent_mr5_environment_override(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("YGO_M3_DUEL_FLAGS=", cmake)
