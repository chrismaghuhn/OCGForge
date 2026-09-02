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
            authority_output = Path(directory) / "smoke-corpus.authority.p6a"
            completed = subprocess.run(
                [str(probe), "--output", str(output), "--authority", str(authority_output)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            artifact = output.read_bytes()
            authority = codec.decode_corpus_authority_artifact(authority_output.read_bytes())
            corpus = codec.admit_corpus_artifact(artifact, authority)
        sample = next((value for value in corpus.samples if value.partition == "train"), None)
        self.assertIsNotNone(sample, "admitted corpus has no train sample")
        self.assertEqual(
            sample.routing_keys[sample.candidate_ordinal],
            sample.selected_public_action_key,
        )
        numeric_input = codec.make_numeric_model_input(
            model_input_identity=sample.model_input_identity,
            state_rows=sample.state_rows,
            candidate_rows=sample.candidate_rows,
            routing_keys=sample.routing_keys,
            public_candidate_domain_digest=sample.public_candidate_domain_digest,
            public_semantic_decision_id=sample.public_semantic_decision_id,
            perspective_player=sample.perspective_player,
            decision_index=sample.decision_index,
        )
        self.assertEqual(
            numeric_input.ordered_candidate_domain_identity,
            sample.ordered_candidate_domain_identity,
        )
        model = task4_model.Phase6TorchCandidateScorer()
        state_rows = torch.tensor(numeric_input.state_rows, dtype=torch.float32)
        candidate_rows = torch.tensor(numeric_input.candidate_rows, dtype=torch.float32)
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
