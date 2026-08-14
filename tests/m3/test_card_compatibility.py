import json
import unittest
from pathlib import Path

from tools.m3.audit import build_matchup_audit, write_audit_reports


class CardCompatibilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[2]
        cls.audit = build_matchup_audit(
            deck_a=cls.root / "fixtures" / "decks" / "swordsoul_tenyi_ml_v1.ydk",
            deck_b=cls.root / "fixtures" / "decks" / "salamangreat_ml_v1.ydk",
            database=cls.root / ".cache" / "rules_bundle" / "babelcdb" / "cards.cdb",
            scripts=cls.root / ".cache" / "rules_bundle" / "cardscripts",
            lock=cls.root / "third_party" / "rules_bundle.lock.json",
        )

    def test_audit_retains_all_slots_and_unique_codes(self):
        self.assertEqual(len(self.audit["slots"]), 110)
        self.assertEqual(len(self.audit["unique_passcodes"]), 50)
        self.assertEqual(self.audit["main_deck_count"], {"deck_a": 40, "deck_b": 40})
        self.assertEqual(self.audit["extra_deck_count"], {"deck_a": 15, "deck_b": 15})

    def test_every_locked_slot_resolves_to_the_declared_card_name(self):
        self.assertTrue(all(row["cdb_row_exists"] for row in self.audit["slots"]))
        self.assertTrue(all(row["declared_name"] == row["cdb_name"] for row in self.audit["slots"]))

    def test_effect_cards_have_pinned_script_resolution(self):
        effect_rows = [row for row in self.audit["unique_cards"] if row["script_required"]]
        self.assertEqual(len(effect_rows), 50)
        self.assertTrue(all(row["script"]["load_result"] == "PASS" for row in effect_rows))

    def test_duplicate_name_resolution_uses_script_backed_primary(self):
        rows = {row["cdb_name"]: row for row in self.audit["unique_cards"]}
        self.assertEqual(rows["Incredible Ecclesia, the Virtuous"]["passcode"], 55273560)
        self.assertEqual(rows["Ash Blossom & Joyous Spring"]["passcode"], 14558127)
        self.assertEqual(rows["Salamangreat Heatleo"]["passcode"], 41463181)

    def test_status_vocabulary_is_machine_checkable(self):
        allowed = {
            "PASS_STATIC_ONLY",
            "PASS_ENGINE_PATH",
            "PENDING_ENGINE_PATH",
            "MISSING_CDB",
            "MISSING_SCRIPT",
            "SCRIPT_LOAD_FAILURE",
            "ENGINE_FAILURE",
        }
        self.assertTrue(all(row["status"] in allowed for row in self.audit["unique_cards"]))

    def test_audit_reports_are_machine_verifiable_and_deterministic(self):
        import tempfile

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_audit_reports(
                self.audit,
                manifest=root / "manifest.json",
                compatibility_json=root / "compatibility.json",
                compatibility_markdown=root / "compatibility.md",
            )
            manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
            compatibility = json.loads((root / "compatibility.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["matchup_id"], "ocgforge.matchup.swordsoul_salamangreat.v1")
            self.assertEqual(len(compatibility["slots"]), 110)
            self.assertIn("# M3 Card Compatibility", (root / "compatibility.md").read_text(encoding="utf-8"))

    def test_cli_audit_writes_the_locked_reports(self):
        import tempfile

        from tools.m3.cli import main

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            arguments = [
                "audit",
                "--deck-a",
                str(self.root / "fixtures" / "decks" / "swordsoul_tenyi_ml_v1.ydk"),
                "--deck-b",
                str(self.root / "fixtures" / "decks" / "salamangreat_ml_v1.ydk"),
                "--database",
                str(self.root / ".cache" / "rules_bundle" / "babelcdb" / "cards.cdb"),
                "--scripts",
                str(self.root / ".cache" / "rules_bundle" / "cardscripts"),
                "--lock",
                str(self.root / "third_party" / "rules_bundle.lock.json"),
                "--manifest",
                str(output / "manifest.json"),
                "--json",
                str(output / "compatibility.json"),
                "--markdown",
                str(output / "compatibility.md"),
            ]
            self.assertEqual(main(arguments), 0)
            self.assertTrue((output / "manifest.json").exists())

    def test_mechanics_and_api_inventories_are_complete_and_fail_closed(self):
        docs = self.root / "docs" / "m3"
        coverage = json.loads((docs / "mechanics_coverage.json").read_text(encoding="utf-8"))
        self.assertEqual(coverage["fixture_count"], 45)
        self.assertEqual(len(coverage["rows"]), 45)
        statuses = set(coverage["status_values"])
        self.assertTrue(all(row["status"] in statuses for row in coverage["rows"]))
        self.assertEqual(
            coverage["classification_counts"],
            {
                "ENGINE_VERIFIED": 38,
                "PROTOCOL_VERIFIED": 7,
                "PUBLIC_API_LIMITATION": 0,
                "NOT_APPLICABLE_FIXED_MATCHUP": 0,
                "PENDING": 0,
            },
        )
        for row in coverage["rows"]:
            if row["status"] == "ENGINE_VERIFIED":
                self.assertTrue(row["evidence_source"], row["key"])
                self.assertTrue(row["engine_evidence"], row["key"])
                self.assertTrue(row["observation_evidence"], row["key"])
                self.assertTrue(row["observation_hash_chain_sha256"], row["key"])
            elif row["status"] == "PUBLIC_API_LIMITATION":
                self.assertTrue(row["public_api_gap_id"], row["key"])
        api_gaps = json.loads((docs / "public_api_gaps.json").read_text(encoding="utf-8"))
        self.assertIn("INDIVIDUAL_XYZ_MATERIAL_QUERY",
                      {row["gap_id"] for row in api_gaps["rows"]})
        self.assertIn("START_PLAYER_SELECTION_CONTROL",
                      {row["gap_id"] for row in api_gaps["rows"]})
        self.assertTrue((docs / "FIXED_MATCHUP.md").exists())
        matrix = json.loads((docs / "m3_acceptance_matrix.json").read_text(encoding="utf-8"))
        self.assertEqual(matrix["schema_version"], "ocgforge.m3.acceptance_matrix.v1")
        self.assertGreaterEqual(len(matrix["rows"]), 45)
        self.assertEqual(matrix["recommendation"], "M3 FINAL PASS")
        rule_mode = next(row for row in matrix["rows"] if row["criterion_id"] == "M3-RULE-MODE")
        self.assertEqual(rule_mode["status"], "PASS")

    def test_mechanics_closure_reconsiders_every_baseline_pending_row(self):
        closure = json.loads((self.root / "docs" / "m3" / "m3_1_closure.json").read_text(encoding="utf-8"))
        self.assertEqual(len(closure["rows"]), 16)
        self.assertTrue(all(row["current_status"] == "PENDING_ENGINE_FIXTURE" for row in closure["rows"]))
        self.assertTrue(all(row["new_status"] in closure["closure_policy"]["allowed_final_classifications"]
                            for row in closure["rows"]))
        self.assertEqual(sum(row["new_status"] == "PENDING" for row in closure["rows"]), 0)
        subpaths = closure["subpath_classifications"]
        self.assertEqual({row["classification"] for row in subpaths}, {"NOT_APPLICABLE_FIXED_MATCHUP"})
        self.assertTrue(all(row["rationale"] and row["source_evidence"] for row in subpaths))

    def test_locked_tcg_duel_mode_audit_is_explicit(self):
        audit = json.loads((self.root / "docs" / "m3" / "rules_mode_audit.json").read_text(encoding="utf-8"))
        self.assertEqual(audit["format_id"], "TCG_ADVANCED_2026_05_18")
        self.assertEqual(audit["before"]["duel_flags"]["value"], 0)
        self.assertEqual(audit["required_modern_mode"]["hex"], "0x2E800")
        self.assertEqual(audit["canonical"]["duel_flags"]["value"], 0x2E800)
        self.assertEqual(audit["status"], "RESOLVED_CONFIGURATION_CORRECTION")
        self.assertEqual(audit["identity_change"]["previous_m3_2_bundle_id"], audit["identity_change"]["old_rules_bundle_id"])
        self.assertNotEqual(audit["identity_change"]["old_rules_bundle_id"], audit["before"]["rules_bundle_id"])
        self.assertEqual(audit["identity_change"]["new_rules_bundle_id"], audit["canonical"]["rules_bundle_id"])


if __name__ == "__main__":
    unittest.main()
