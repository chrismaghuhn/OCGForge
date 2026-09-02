import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import torch

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4_model


class Task4AAdmittedModelTests(unittest.TestCase):
    def test_one_admitted_train_sample_reaches_forward_without_synthetic_label(self):
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
            corpus = codec.decode_corpus_artifact(output.read_bytes())
        sample = next((value for value in corpus.samples if value.partition == "train"), None)
        self.assertIsNotNone(sample, "admitted corpus has no train sample")
        self.assertEqual(
            sample.routing_keys[sample.candidate_ordinal],
            sample.selected_public_action_key,
        )
        model = task4_model.Phase6TorchCandidateScorer()
        state_rows = torch.tensor(sample.state_rows, dtype=torch.float32)
        candidate_rows = torch.tensor(sample.candidate_rows, dtype=torch.float32)
        with torch.no_grad():
            logits = model(state_rows, candidate_rows)
        self.assertEqual(tuple(logits.shape), (len(sample.candidate_rows),))
        self.assertTrue(torch.isfinite(logits).all().item())
        mask = torch.ones((1, len(sample.candidate_rows)), dtype=torch.bool)
        labels = torch.tensor([sample.candidate_ordinal], dtype=torch.long)
        loss = task4_model.exact_domain_cross_entropy_from_padded(
            logits.unsqueeze(0), labels, mask
        )
        self.assertTrue(torch.isfinite(loss).item())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
