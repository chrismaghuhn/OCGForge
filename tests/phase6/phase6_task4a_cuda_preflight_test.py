import unittest
from unittest import mock

import torch

from tools.phase6 import task4_codec as codec
from tools.phase6 import task4_cuda


class Task4ACudaPreflightTests(unittest.TestCase):
    def test_unavailable_cuda_fails_closed_without_cpu_fallback(self):
        with mock.patch.object(torch.cuda, "is_available", return_value=False):
            with self.assertRaises(task4_cuda.CudaPreflightError) as raised:
                task4_cuda.require_task4_cuda()
        self.assertEqual(raised.exception.code, "CUDA_UNAVAILABLE")
        self.assertEqual(raised.exception.actual_optimizer_steps, 0)

    def test_unexpected_device_fails_closed(self):
        with mock.patch.object(torch.cuda, "is_available", return_value=True), \
             mock.patch.object(torch.cuda, "device_count", return_value=1), \
             mock.patch.object(torch.cuda, "get_device_name", return_value="other GPU"):
            with self.assertRaises(task4_cuda.CudaPreflightError) as raised:
                task4_cuda.require_task4_cuda()
        self.assertEqual(raised.exception.code, "CUDA_DEVICE_MISMATCH")
        self.assertEqual(raised.exception.actual_optimizer_steps, 0)

    @unittest.skipUnless(torch.cuda.is_available(), "CUDA is not available in this environment")
    def test_required_local_cuda_preflight_has_no_training_side_effect(self):
        result = task4_cuda.require_task4_cuda()
        self.assertEqual(result.device, torch.device("cuda:0"))
        self.assertEqual(result.gpu_name, "NVIDIA GeForce RTX 4060 Ti")
        self.assertTrue(result.torch_cuda_version)
        self.assertFalse(hasattr(result, "cuda_runtime"))
        self.assertEqual(result.actual_optimizer_steps, 0)
        self.assertFalse(result.cpu_fallback)
        self.assertEqual(
            result.execution_provenance_identity,
            codec.execution_provenance_identity_for(result.execution_provenance()),
        )
        self.assertTrue(result.cuda_preflight_identity.startswith(
            codec.CUDA_PREFLIGHT_ID_PREFIX
        ))
        manifest = task4_cuda.training_run_manifest_from_cuda_preflight(
            result,
            source_dataset_identity="1" * 64,
            dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
            card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
            training_code_commit="4" * 40,
            actual_optimizer_steps=0,
        )
        self.assertEqual(
            manifest.device_and_distributed_provenance_identity,
            result.execution_provenance_identity,
        )
        self.assertTrue(codec.training_run_identity(manifest).startswith(
            "phase6_training_run.v1."
        ))
        positive_manifest = task4_cuda.training_run_manifest_from_cuda_preflight(
            result,
            source_dataset_identity="1" * 64,
            dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
            card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
            training_code_commit="4" * 40,
            actual_optimizer_steps=1,
        )
        self.assertTrue(codec.training_run_identity(positive_manifest).startswith(
            "phase6_training_run.v1."
        ))


if __name__ == "__main__":
    unittest.main()
