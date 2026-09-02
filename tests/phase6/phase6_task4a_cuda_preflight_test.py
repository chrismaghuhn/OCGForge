import dataclasses
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
        self.assertTrue(result.torch_cuda_version_reported)
        self.assertFalse(hasattr(result, "torch_cuda_build"))
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
        base_manifest = codec.default_training_run_manifest(
            source_dataset_identity="1" * 64,
            dataset_split_identity="phase6_dataset_split.v1." + "2" * 64,
            card_vocabulary_identity="model_card_vocabulary.v1." + "3" * 64,
            training_code_commit="4" * 40,
            actual_optimizer_steps=0,
        )
        manifest = task4_cuda.finalize_training_run_manifest_from_cuda_preflight(
            result, base_manifest
        )
        self.assertEqual(manifest.framework_version, result.framework_version)
        self.assertEqual(
            manifest.device_and_distributed_provenance_identity,
            result.execution_provenance_identity,
        )
        evidence = task4_cuda.smoke_evidence_from_cuda_preflight(
            result,
            manifest,
            actual_optimizer_steps=0,
        )
        self.assertEqual(evidence.training_run_identity, codec.training_run_identity(manifest))
        self.assertEqual(
            evidence.device_and_distributed_provenance_identity,
            result.execution_provenance_identity,
        )
        self.assertEqual(evidence.actual_optimizer_steps, 0)
        checkpoint_identity = "phase6_checkpoint.v1." + "9" * 64
        positive_manifest = (
            task4_cuda.finalize_training_run_manifest_from_cuda_preflight(
                result,
                base_manifest,
                final_exported_checkpoint_identity=checkpoint_identity,
            )
        )
        positive_evidence = task4_cuda.smoke_evidence_from_cuda_preflight(
            result,
            positive_manifest,
            actual_optimizer_steps=1,
            fresh_checkpoint_reload=True,
            deterministic_frozen_inference=True,
            gpu_memory_before=1,
            gpu_memory_peak=2,
            gpu_memory_after=1,
        )
        self.assertEqual(positive_evidence.actual_optimizer_steps, 1)
        self.assertEqual(
            positive_evidence.training_run_identity,
            codec.training_run_identity(positive_manifest),
        )
        self.assertEqual(
            positive_evidence.cuda_preflight_identity,
            result.cuda_preflight_identity,
        )
        with self.assertRaises(TypeError):
            task4_cuda.smoke_evidence_from_cuda_preflight(
                result,
                positive_manifest,
                training_run_identity="phase6_training_run.v1." + "8" * 64,
                actual_optimizer_steps=1,
                fresh_checkpoint_reload=True,
                deterministic_frozen_inference=True,
                gpu_memory_before=1,
                gpu_memory_peak=2,
                gpu_memory_after=1,
            )
        with self.assertRaises(task4_cuda.CudaPreflightError):
            task4_cuda.smoke_evidence_from_cuda_preflight(
                result,
                manifest,
                actual_optimizer_steps=1,
                fresh_checkpoint_reload=True,
                deterministic_frozen_inference=True,
                gpu_memory_before=1,
                gpu_memory_peak=2,
                gpu_memory_after=1,
            )
        with self.assertRaises(task4_cuda.CudaPreflightError):
            task4_cuda.smoke_evidence_from_cuda_preflight(
                result,
                positive_manifest,
                actual_optimizer_steps=1,
                fresh_checkpoint_reload=False,
                deterministic_frozen_inference=True,
                gpu_memory_before=1,
                gpu_memory_peak=2,
                gpu_memory_after=1,
            )
        forged_provenance = dataclasses.replace(
            positive_manifest,
            device_and_distributed_provenance_identity=(
                "phase6_execution_provenance.v1." + "a" * 64
            ),
        )
        with self.assertRaises(task4_cuda.CudaPreflightError):
            task4_cuda.smoke_evidence_from_cuda_preflight(
                result, forged_provenance
            )
        with self.assertRaises(task4_cuda.CudaPreflightError):
            task4_cuda.smoke_evidence_from_cuda_preflight(
                dataclasses.replace(result, _attestation=None),
                positive_manifest,
                actual_optimizer_steps=1,
                fresh_checkpoint_reload=True,
                deterministic_frozen_inference=True,
                gpu_memory_before=1,
                gpu_memory_peak=2,
                gpu_memory_after=1,
            )


if __name__ == "__main__":
    unittest.main()
