import hashlib
import tempfile
import unittest
from pathlib import Path

from tools import ocgcore_patchset


class OcgcorePatchsetTests(unittest.TestCase):
    def test_patchset_hash_is_deterministic_and_ordered(self):
        patchset = {
            "id": "ocgforge.ocgcore.api_hardening.v1",
            "ordered_patches": [
                {"name": "0001-a.patch", "sha256": "a" * 64},
                {"name": "0002-b.patch", "sha256": "b" * 64},
            ],
        }

        expected = hashlib.sha256(ocgcore_patchset.canonical_json(patchset)).hexdigest()

        self.assertEqual(expected, ocgcore_patchset.patchset_hash_for(patchset))
        self.assertEqual(expected, ocgcore_patchset.patchset_hash_for(patchset))
        self.assertNotEqual(
            expected,
            ocgcore_patchset.patchset_hash_for(
                {**patchset, "ordered_patches": list(reversed(patchset["ordered_patches"]))}
            ),
        )

    def test_patchset_manifest_records_exact_file_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "0001-a.patch").write_bytes(b"first patch\n")
            (root / "0002-b.patch").write_bytes(b"second patch\n")

            manifest = ocgcore_patchset.build_patchset_manifest(
                root,
                ["0001-a.patch", "0002-b.patch"],
                "ocgforge.ocgcore.api_hardening.v1",
            )

            ocgcore_patchset.validate_patchset_files(manifest, root)
            self.assertEqual(
                hashlib.sha256(b"first patch\n").hexdigest(),
                manifest["ordered_patches"][0]["sha256"],
            )
            self.assertEqual(
                hashlib.sha256(b"second patch\n").hexdigest(),
                manifest["ordered_patches"][1]["sha256"],
            )

    def test_extra_patch_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "0001-a.patch").write_bytes(b"first patch\n")
            manifest = ocgcore_patchset.build_patchset_manifest(
                root, ["0001-a.patch"], "ocgforge.test.v1"
            )
            (root / "unexpected.patch").write_bytes(b"unexpected\n")
            with self.assertRaises(ocgcore_patchset.PatchsetError):
                ocgcore_patchset.validate_patchset_files(manifest, root)

    def test_changed_patch_bytes_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            patch = root / "0001-a.patch"
            patch.write_bytes(b"first patch\n")
            manifest = ocgcore_patchset.build_patchset_manifest(
                root, [patch.name], "ocgforge.test.v1"
            )
            patch.write_bytes(b"changed patch\n")
            with self.assertRaises(ocgcore_patchset.PatchsetError):
                ocgcore_patchset.validate_patchset_files(manifest, root)


if __name__ == "__main__":
    unittest.main()
