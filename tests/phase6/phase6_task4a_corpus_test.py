import dataclasses
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
                self.assertEqual(
                    sample.ordered_candidate_domain_identity,
                    codec.ordered_candidate_domain_identity(
                        sample.routing_keys, sample.public_candidate_domain_digest
                    ),
                )
            self.assertTrue(
                any(sample.public_candidate_domain_digest is not None
                    for sample in corpus.samples),
                "fixed Teacher corpus did not exercise Phase-5 domain identity",
            )
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

            train = tuple(value for value in corpus.episode_ids
                          if codec.partition_for_episode(value) == "train")
            validation = tuple(value for value in corpus.episode_ids
                               if codec.partition_for_episode(value) == "validation")
            test = tuple(value for value in corpus.episode_ids
                         if codec.partition_for_episode(value) == "test")
            authority = codec.CorpusAdmissionAuthorityV1(
                expected_artifact_identity=codec.derived_corpus_content_identity(
                    codec.canonical_corpus_bytes(corpus)
                ),
                source_dataset_identity=corpus.source_dataset_identity,
                split_identity=corpus.split_identity,
                card_vocabulary_identity=corpus.card_vocabulary_identity,
                train_episode_ids=train,
                validation_episode_ids=validation,
                test_episode_ids=test,
                source_samples=tuple(codec.source_sample_authority(value)
                                      for value in corpus.samples),
            )
            self.assertEqual(codec.admit_corpus_artifact(artifact, authority), corpus)

            changed_source = dataclasses.replace(
                corpus, source_dataset_identity="f" * 64
            )
            changed_source_artifact = codec.encode_corpus_artifact(changed_source)
            changed_source_authority = dataclasses.replace(
                authority,
                expected_artifact_identity=codec.derived_corpus_content_identity(
                    codec.canonical_corpus_bytes(changed_source)
                ),
            )
            with self.assertRaises(codec.CodecError):
                codec.admit_corpus_artifact(changed_source_artifact, changed_source_authority)

            swapped_authority = dataclasses.replace(
                authority,
                train_episode_ids=authority.validation_episode_ids,
                validation_episode_ids=authority.train_episode_ids,
                split_identity=codec.split_identity(
                    authority.source_dataset_identity,
                    authority.validation_episode_ids,
                    authority.train_episode_ids,
                    authority.test_episode_ids,
                ),
            )
            with self.assertRaises(codec.CodecError):
                codec.admit_corpus_artifact(artifact, swapped_authority)

            changed_sample = dataclasses.replace(
                corpus.samples[0], model_input_identity="model_input.v1." + "9" * 64
            )
            changed_sample = dataclasses.replace(
                changed_sample,
                bc_sample_identity=codec.bc_sample_identity(changed_sample),
            )
            changed_samples = (changed_sample,) + corpus.samples[1:]
            changed_corpus = dataclasses.replace(corpus, samples=changed_samples)
            changed_sample_authority = dataclasses.replace(
                authority,
                expected_artifact_identity=codec.derived_corpus_content_identity(
                    codec.canonical_corpus_bytes(changed_corpus)
                ),
            )
            with self.assertRaises(codec.CodecError):
                codec.admit_corpus_artifact(
                    codec.encode_corpus_artifact(changed_corpus), changed_sample_authority
                )

            numeric_marker = artifact.find(b"\x3f\x80\x00\x00")
            self.assertGreater(numeric_marker, 68, "corpus has no canonical numeric row")
            mutated = bytearray(artifact)
            mutated[numeric_marker + 3] ^= 1
            with self.assertRaises(codec.CodecError):
                codec.decode_corpus_artifact(bytes(mutated))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
