import unittest

import torch

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4_model


class Task4AModelTests(unittest.TestCase):
    def test_model_returns_exact_n_logits_without_routing_inputs(self):
        torch.manual_seed(1729)
        model = task4_model.Phase6TorchCandidateScorer()
        self.assertEqual(
            tuple(name for name, _ in model.named_parameters()), codec.PARAMETER_ORDER
        )
        for count in (1, 24, 25, 129):
            state_rows = torch.zeros((3, codec.STATE_ROW_WIDTH), dtype=torch.float32)
            candidate_rows = torch.zeros((count, codec.CANDIDATE_ROW_WIDTH), dtype=torch.float32)
            candidate_rows[:, 0] = torch.arange(count, dtype=torch.float32)
            with torch.no_grad():
                logits = model(state_rows, candidate_rows)
            self.assertEqual(tuple(logits.shape), (count,))
            self.assertEqual(logits.dtype, torch.float32)
            self.assertTrue(torch.isfinite(logits).all().item())

    def test_physical_capacity_smaller_than_n_fails_closed(self):
        model = task4_model.Phase6TorchCandidateScorer()
        with self.assertRaises(task4_model.Task4ModelError):
            model(
                torch.zeros((1, codec.STATE_ROW_WIDTH)),
                torch.zeros((24, codec.CANDIDATE_ROW_WIDTH)),
                physical_candidate_capacity=23,
            )

    def test_padded_rows_do_not_contribute_to_semantic_loss(self):
        logits = torch.tensor([[1.0, 3.0, 1000.0, -1000.0]], dtype=torch.float32)
        mask = torch.tensor([[1, 1, 0, 0]], dtype=torch.bool)
        labels = torch.tensor([1], dtype=torch.long)
        loss = task4_model.exact_domain_cross_entropy_from_padded(logits, labels, mask)
        expected = torch.nn.functional.cross_entropy(logits[:, :2], labels)
        self.assertTrue(torch.allclose(loss, expected))

        changed_padding = logits.clone()
        changed_padding[0, 2:] = torch.tensor([-1e20, 1e20])
        changed_loss = task4_model.exact_domain_cross_entropy_from_padded(
            changed_padding, labels, mask
        )
        self.assertTrue(torch.equal(loss, changed_loss))
        self.assertTrue(torch.isfinite(loss).item())

    def test_nonfinite_logits_and_loss_fail_closed(self):
        with self.assertRaises(task4_model.Task4ModelError):
            task4_model.validate_logits(torch.tensor([float("nan")]))
        with self.assertRaises(task4_model.Task4ModelError):
            task4_model.validate_logits(torch.tensor([float("inf")]))
        with self.assertRaises(task4_model.Task4ModelError):
            task4_model.exact_domain_cross_entropy_from_padded(
                torch.tensor([[float("nan"), 1.0]]),
                torch.tensor([0]),
                torch.tensor([[1, 1]], dtype=torch.bool),
            )


if __name__ == "__main__":
    unittest.main()
