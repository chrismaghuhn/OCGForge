import unittest
from unittest import mock

import torch

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
        self.assertEqual(result.actual_optimizer_steps, 0)
        self.assertFalse(result.cpu_fallback)


if __name__ == "__main__":
    unittest.main()
