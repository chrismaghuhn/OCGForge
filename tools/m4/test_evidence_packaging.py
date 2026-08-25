from __future__ import annotations

import hashlib
from pathlib import Path
import tempfile
import unittest

from tools.m4.generate_m4_final_evidence import sha256 as packaged_evidence_sha256


ROOT = Path(__file__).resolve().parents[2]


class EvidencePackagingTests(unittest.TestCase):
    def test_lf_and_crlf_hash_to_repository_bytes_but_content_mutation_fails(self) -> None:
        lf_bytes = b"first line\nsecond line\n"
        crlf_bytes = b"first line\r\nsecond line\r\n"
        mutated_bytes = b"first line\r\nchanged line\r\n"
        with tempfile.TemporaryDirectory(
            prefix="ocgforge-m4-evidence-packaging-",
            dir=ROOT / "artifacts" / "m4",
        ) as directory:
            lf_path = Path(directory) / "lf.log"
            crlf_path = Path(directory) / "crlf.log"
            mutated_path = Path(directory) / "mutated.log"
            lf_path.write_bytes(lf_bytes)
            crlf_path.write_bytes(crlf_bytes)
            mutated_path.write_bytes(mutated_bytes)

            expected = hashlib.sha256(lf_bytes).hexdigest()
            self.assertEqual(packaged_evidence_sha256(lf_path), expected)
            self.assertEqual(packaged_evidence_sha256(crlf_path), expected)
            self.assertNotEqual(packaged_evidence_sha256(mutated_path), expected)


if __name__ == "__main__":
    unittest.main()
