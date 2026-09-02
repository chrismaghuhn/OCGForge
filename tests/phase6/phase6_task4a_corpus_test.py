import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.phase6 import task4_codec as codec


class Task4ACorpusTests(unittest.TestCase):
    def test_fixed_admitted_probe_emits_valid_derived_corpus(self):
        probe = Path(sys.argv[1]) if len(sys.argv) > 1 else None
        self.assertIsNotNone(probe, "corpus probe path is required")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "smoke-corpus.p6c"
            completed = subprocess.run(
                [str(probe), "--output", str(output)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            artifact = output.read_bytes()
            corpus = codec.decode_corpus_artifact(artifact)
            self.assertEqual(
                corpus.derivation_contract_identity,
                codec.NUMERIC_PROJECTION_CONTRACT_ID,
            )
            self.assertTrue(corpus.source_dataset_identity)
            self.assertTrue(corpus.split_identity.startswith("phase6_dataset_split.v1."))
            self.assertTrue(corpus.episode_ids)
            self.assertTrue(corpus.samples)
            self.assertTrue(any(sample.partition == "train" for sample in corpus.samples))
            for sample in corpus.samples:
                self.assertTrue(sample.bc_sample_identity.startswith("bc_sample.v1."))
                self.assertTrue(sample.model_input_identity.startswith("model_input.v1."))
                self.assertEqual(len(sample.candidate_rows), len(sample.routing_keys))
                self.assertEqual(
                    sample.routing_keys[sample.candidate_ordinal],
                    sample.selected_public_action_key,
                )
                self.assertTrue(sample.state_rows)
                self.assertTrue(all(len(row) == codec.STATE_ROW_WIDTH for row in sample.state_rows))
                self.assertTrue(all(len(row) == codec.CANDIDATE_ROW_WIDTH for row in sample.candidate_rows))

            numeric_marker = artifact.find(b"\x3f\x80\x00\x00")
            self.assertGreater(numeric_marker, 68, "corpus has no canonical numeric row")
            mutated = bytearray(artifact)
            mutated[numeric_marker + 3] ^= 1
            with self.assertRaises(codec.CodecError):
                codec.decode_corpus_artifact(bytes(mutated))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
