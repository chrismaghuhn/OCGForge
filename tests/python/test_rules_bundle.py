import json
import tempfile
import unittest
from pathlib import Path

from tools import rules_bundle


class RulesBundleTests(unittest.TestCase):
    def test_bundle_id_is_sha256_of_canonical_inputs_without_bundle_id(self):
        inputs = {
            "sources": {
                "core": {"commit": "a"},
                "database": {"sha256": "b"},
            },
            "duel_flags": {"value": 1},
        }

        bundle_id = rules_bundle.bundle_id_for(inputs)

        self.assertEqual(64, len(bundle_id))
        self.assertEqual(bundle_id, rules_bundle.bundle_id_for(inputs))
        self.assertNotEqual(
            bundle_id,
            rules_bundle.bundle_id_for({**inputs, "duel_flags": {"value": 2}}),
        )

    def test_checkout_hash_is_order_independent(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            (root / "b.txt").write_bytes(b"beta")
            (root / "nested").mkdir()
            (root / "nested" / "a.txt").write_bytes(b"alpha")

            first = rules_bundle.checkout_hash(root)
            (root / "nested" / "a.txt").touch()
            second = rules_bundle.checkout_hash(root)

        self.assertEqual(first, second)

    def test_lock_rejects_floating_refs(self):
        lock = {
            "schema_version": 1,
            "bundle_id": "0" * 64,
            "sources": {
                "core": {
                    "repository": "https://example.invalid/core.git",
                    "commit": "main",
                }
            },
        }

        with self.assertRaises(rules_bundle.BundleVerificationError):
            rules_bundle.validate_lock_shape(lock)


if __name__ == "__main__":
    unittest.main()
