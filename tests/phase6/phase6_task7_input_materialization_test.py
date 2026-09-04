import dataclasses
import os
import subprocess
import sys
import unittest

from tools.phase6 import task7_materialization as task7

EXECUTABLE_ARGUMENTS = tuple(
    argument for argument in sys.argv[1:] if argument.lower().endswith((".exe", ".com"))
)
PROBE_PATH = EXECUTABLE_ARGUMENTS[0] if EXECUTABLE_ARGUMENTS else os.path.join(
    "build", "dev-windows", "phase6_task7_materialization_probe.exe"
)
PAIRED_PROBE_PATH = EXECUTABLE_ARGUMENTS[1] if len(EXECUTABLE_ARGUMENTS) > 1 else os.path.join(
    os.path.dirname(PROBE_PATH), "phase6_task7_input_materialization_test.exe"
)


class Task7InputMaterializationTests(unittest.TestCase):
    def _probe_sample(self, *arguments):
        if not os.path.exists(PROBE_PATH):
            self.skipTest("Task7 C++ probe is not built")
        output = subprocess.check_output([PROBE_PATH, *map(str, arguments)], text=True).splitlines()
        fields = dict(line.split("=", 1) for line in output if "=" in line)
        self.assertEqual(fields["task7_config"], task7.CONFIGURATION_IDENTITY)
        sample = task7.decode_canonical_sample(bytes.fromhex(fields["task7_sample_hex"]))
        self.assertEqual(sample.configuration_identity, task7.CONFIGURATION_IDENTITY)
        return sample

    def test_configuration_kat_and_exact_limb_helpers(self):
        payload = task7.canonical_configuration_bytes()
        self.assertEqual(len(payload), task7.CONFIG_CANONICAL_BYTES_LENGTH)
        self.assertEqual(task7.canonical_configuration_digest(payload), task7.CONFIG_CANONICAL_BYTES_SHA256)
        self.assertEqual(
            task7.configuration_identity(),
            "phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a",
        )
        self.assertEqual(task7.u8_limbs(255), (255,))
        self.assertEqual(task7.u16_limbs(65535), (65535,))
        self.assertEqual(task7.u32_limbs(0), (0, 0))
        self.assertEqual(task7.u32_limbs(1), (0, 1))
        self.assertEqual(task7.u32_limbs((1 << 24) - 1), (255, 65535))
        self.assertEqual(task7.u32_limbs(1 << 24), (256, 0))
        self.assertEqual(task7.u32_limbs(0xFFFFFFFE), (65535, 65534))
        self.assertEqual(task7.u32_limbs(0xFFFFFFFF), (65535, 65535))
        self.assertEqual(task7.u64_limbs(1 << 24), (0, 0, 256, 0))
        self.assertEqual(task7.u64_limbs(1), (0, 0, 0, 1))
        self.assertEqual(task7.u64_limbs((1 << 32) - 1), (0, 0, 65535, 65535))
        self.assertEqual(task7.u64_limbs(1 << 32), (0, 1, 0, 0))
        self.assertEqual(task7.u64_limbs((1 << 32) + 1), (0, 1, 0, 1))
        self.assertEqual(task7.u64_limbs(0xFFFFFFFFFFFFFFFF), (65535, 65535, 65535, 65535))
        self.assertEqual(task7.i32_limbs(-(1 << 31)), (32768, 0))
        self.assertEqual(task7.i32_limbs(-1), (65535, 65535))
        self.assertEqual(task7.i32_limbs(0), (0, 0))
        self.assertEqual(task7.i32_limbs(1), (0, 1))
        self.assertEqual(task7.i32_limbs((1 << 31) - 1), (32767, 65535))

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_cpp_canonical_bytes_decode_to_typed_tensors(self):
        sample = self._probe_sample()
        self.assertEqual(len(sample.tables), 23)
        self.assertEqual(sample.candidate_count, 1)
        self.assertEqual(sample.table("candidates").row_mask.dtype, task7.torch.bool)
        self.assertEqual(sample.table("candidates").column("action_kind_code").values.dtype, task7.torch.int64)
        self.assertEqual(sample.table("candidates").column("source_index").presence.dtype, task7.torch.bool)
        self.assertEqual(sample.table("candidates").column("source_index").values.shape, (1, 2))
        self.assertEqual(len(sample.routing_keys), 1)
        self.assertEqual(len(sample.public_locator_tokens), 0)
        for table in sample.tables:
            self.assertEqual(table.row_mask.dtype, task7.torch.bool)
            self.assertEqual(table.sample_offsets.dtype, task7.torch.int64)
            if table.parent_offsets is not None:
                self.assertEqual(table.parent_offsets.dtype, task7.torch.int64)
        for _, _, tensor in sample.learner_tensors:
            self.assertIn(tensor.dtype, (task7.torch.int64, task7.torch.bool))
        self.assertTrue(all("public_action" not in name and "routing" not in name
                            for name, _, _ in sample.learner_tensors))

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_optional_and_reference_forms_are_type_separated(self):
        sample = self._probe_sample("--rich")
        self.assertEqual(sample.table("relationships").column("source").values.form, "R")
        self.assertEqual(sample.table("chain_links").column("source").values.form, "OR")
        self.assertEqual(sample.table("candidates").column("source_reference").values.form, "CR")
        self.assertEqual(sample.table("visible_events").column("entity").values.form, "HR")
        self.assertIsNone(sample.table("relationships").column("source").values.kind_code)
        self.assertIsNone(sample.table("chain_links").column("source").values.kind_code)
        candidate_source = sample.table("candidates").column("source_reference").values
        self.assertIsNotNone(candidate_source.kind_code)
        self.assertEqual(candidate_source.kind_code.tolist(), [[0]])
        self.assertEqual(sample.table("candidates").column("target_reference").values.kind_code.tolist(), [[1]])
        self.assertTrue(bool(candidate_source.outer_present[0].item()))
        self.assertTrue(bool(candidate_source.current_entity_present[0].item()))
        self.assertTrue(bool(sample.table("chain_links").column("source").values.outer_present[0].item()))
        self.assertFalse(bool(sample.table("chain_links").column("source").values.outer_present[1].item()))
        self.assertTrue(bool(sample.table("visible_events").column("entity").values.outer_present[0].item()))
        self.assertFalse(bool(sample.table("visible_events").column("entity").values.outer_present[1].item()))
        absent_candidate = self._probe_sample().table("candidates").column("source_reference").values
        self.assertFalse(bool(absent_candidate.outer_present[0].item()))
        self.assertEqual(absent_candidate.kind_code.tolist(), [[0]])
        self.assertEqual(sample.table("entity_properties").parent_offsets.tolist(), [0, 2, 4])
        self.assertEqual(sample.table("chain_targets").parent_offsets.tolist(), [0, 1, 1])
        self.assertEqual(sample.table("visible_event_targets").parent_offsets.tolist(), [0, 2, 2])
        globals_length = sample.table("globals").column("chain_length").values
        chain_length = sample.table("chain_state").column("length").values
        self.assertNotEqual(globals_length.tolist(), chain_length.tolist())
        self.assertFalse(bool(sample.table("globals").column("winner").presence[0].item()))
        self.assertTrue(bool(sample.table("globals").column("win_reason").presence[0].item()))
        properties = sample.table("entity_properties")
        self.assertFalse(bool(properties.column("property_present").values[0].item()))
        self.assertTrue(bool(properties.column("base_attack").presence[2].item()))
        self.assertEqual(task7.reconstruct_i32(properties.column("base_attack").values[2].tolist()), 0)
        self.assertEqual(task7.reconstruct_i32(properties.column("attack").values[2].tolist()), -(1 << 31))
        self.assertEqual(task7.reconstruct_i32(properties.column("defense").values[2].tolist()), -1)
        self.assertEqual(task7.reconstruct_i32(properties.column("base_defense").values[2].tolist()), (1 << 31) - 1)
        self.assertEqual(
            task7.reconstruct_limbs(
                sample.table("globals").column("duel_flags").values[0].tolist()
            ),
            0xFFFFFFFFFFFFFFFF,
        )
        entity_ids = sample.table("entities").column("card_vocabulary_id").values
        self.assertEqual((int(entity_ids[0, 0]) << 16) + int(entity_ids[0, 1]), 1)
        self.assertEqual((int(entity_ids[1, 0]) << 16) + int(entity_ids[1, 1]), 2)
        self.assertEqual(task7.reconstruct_limbs((65535, 65534)), 0xFFFFFFFE)
        self.assertEqual(task7.reconstruct_i32((65535, 65535)), -1)
        self.assertNotEqual((False, (0, 0)), (True, (0, 0)))

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_every_optional_primitive_family_keeps_presence(self):
        zero_limbs = {
            "U8": task7.u8_limbs(0),
            "U16": task7.u16_limbs(0),
            "U32": task7.u32_limbs(0),
            "U64": task7.u64_limbs(0),
            "I32": task7.i32_limbs(0),
        }
        for source_type, limbs in zero_limbs.items():
            self.assertNotEqual((False, limbs), (True, limbs), source_type)
        sample = self._probe_sample("--rich")
        self.assertTrue(bool(sample.table("entities").column("owner").presence[0].item()))
        self.assertEqual(task7.reconstruct_limbs(sample.table("entities").column("owner").values[0].tolist()), 0)
        self.assertTrue(bool(sample.table("sample_header").column("public_observation_context_kind_code").presence[0].item()))
        self.assertTrue(bool(sample.table("entity_properties").column("left_scale").presence[2].item()))
        self.assertTrue(bool(sample.table("visible_events").column("effect_description").presence[0].item()))
        self.assertEqual(task7.reconstruct_limbs(sample.table("visible_events").column("effect_description").values[0].tolist()), 0)

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_candidate_cardinality_and_source_order(self):
        for count in (1, 24, 25, 129):
            sample = self._probe_sample(count)
            self.assertEqual(sample.candidate_count, count)
            source_index = sample.table("candidates").column("source_index")
            self.assertEqual(source_index.values.shape, (count, 2))
            self.assertEqual(source_index.presence.dtype, task7.torch.bool)
            self.assertEqual([task7.reconstruct_limbs(row.tolist()) for row in source_index.values], list(range(count)))
            self.assertEqual(len(sample.routing_keys), count)

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_padding_round_trip_and_capacity_failure(self):
        sample = self._probe_sample("--rich")
        exact_widths = {table.identity: table.row_count for table in sample.tables
                        if table.kind != "singleton"}
        self.assertEqual(sample.pad(exact_widths).unpad().canonical_bytes,
                         sample.canonical_bytes)
        widths = {table.identity: table.row_count + 2 for table in sample.tables
                  if table.kind != "singleton"}
        padded = sample.pad(widths)
        self.assertTrue(bool(padded.table("candidates").row_mask[0].item()))
        self.assertTrue(not bool(padded.table("candidates").row_mask[-1].item()))
        self.assertEqual(padded.table("entity_properties").parent_offsets.tolist(), [0, 2, 4, 4, 4])
        self.assertEqual(padded.table("property_link_markers").parent_offsets.tolist(), [0, 0, 0, 2, 4, 4, 4])
        self.assertEqual(padded.table("chain_targets").parent_offsets.tolist(), [0, 1, 1, 1, 1])
        self.assertEqual(padded.table("visible_event_targets").parent_offsets.tolist(), [0, 2, 2, 2, 2])
        padded_entities = padded.table("entities")
        self.assertFalse(bool(padded_entities.row_mask[-1].item()))
        self.assertEqual(
            task7.reconstruct_limbs(padded_entities.column("card_vocabulary_id").values[-1].tolist()),
            0,
        )
        self.assertTrue(bool(sample.table("entities").row_mask[0].item()))
        self.assertEqual(
            task7.reconstruct_limbs(sample.table("entities").column("card_vocabulary_id").values[0].tolist()),
            1,
        )
        self.assertEqual(padded.unpad().canonical_bytes, sample.canonical_bytes)
        padded.table("candidates").column("action_kind_code").values[0, 0] += 1
        with self.assertRaises(task7.Task7MaterializationError):
            padded.unpad()
        malformed_offsets = sample.pad(widths)
        malformed_offsets.table("entity_properties").parent_offsets[-1] += 1
        with self.assertRaises(task7.Task7MaterializationError):
            malformed_offsets.unpad()
        malformed_sample_offsets = sample.pad(widths)
        malformed_sample_offsets.table("entities").sample_offsets[-1] += 1
        with self.assertRaises(task7.Task7MaterializationError):
            malformed_sample_offsets.unpad()
        malformed_numeric = sample.pad(widths)
        malformed_numeric.table("entities").column("card_vocabulary_id").values[-1, 0] = 1
        with self.assertRaises(task7.Task7MaterializationError):
            malformed_numeric.unpad()
        malformed_presence = sample.pad(widths)
        malformed_presence.table("globals").column("winner").presence[-1] = True
        with self.assertRaises(task7.Task7MaterializationError):
            malformed_presence.unpad()
        malformed_reference = sample.pad(widths)
        malformed_reference.table("relationships").column("source").values.public_locator_ordinal[-1, 0] = 1
        with self.assertRaises(task7.Task7MaterializationError):
            malformed_reference.unpad()
        locator_index = next(index for index, table in enumerate(padded.tables)
                             if table.identity == "public_locator_control_sidecar")
        locator_table = padded.tables[locator_index]
        locator_column = locator_table.column("public_locator_token")
        bad_locator_column = dataclasses.replace(
            locator_column, values=tuple(locator_column.values[:-1]) + ("leak",)
        )
        bad_locator_table = dataclasses.replace(locator_table, columns=(bad_locator_column,))
        bad_tables = list(padded.tables)
        bad_tables[locator_index] = bad_locator_table
        malformed_string = dataclasses.replace(padded, tables=tuple(bad_tables))
        with self.assertRaises(task7.Task7MaterializationError):
            malformed_string.unpad()
        with self.assertRaises(task7.Task7MaterializationError):
            sample.pad({"candidates": 0})

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_multi_sample_padding_preserves_each_source(self):
        first = self._probe_sample(1, "--rich")
        second = self._probe_sample(25, "--rich")
        batch = task7.decode_canonical_batch((first.canonical_bytes, second.canonical_bytes))
        padded = batch.pad()
        self.assertEqual(padded.widths["candidates"], 25)
        self.assertEqual(int(padded.table(0, "candidates").row_mask.sum().item()), 1)
        self.assertEqual(int(padded.table(1, "candidates").row_mask.sum().item()), 25)
        restored = padded.unpad()
        self.assertEqual(tuple(sample.canonical_bytes for sample in restored.samples),
                         (first.canonical_bytes, second.canonical_bytes))
        first_alone = first.pad({table.identity: table.row_count for table in first.tables
                                 if table.kind != "singleton"})
        first_in_batch = padded.samples[0]
        self.assertEqual(first_alone.unpad().canonical_bytes,
                         first_in_batch.unpad().canonical_bytes)

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_real_paired_hidden_world_materialization_is_equal(self):
        if not os.path.exists(PAIRED_PROBE_PATH):
            self.skipTest("Task7 paired-world probe is not built")
        output = subprocess.check_output([PAIRED_PROBE_PATH, "--paired-probe"], text=True).splitlines()
        fields = dict(line.split("=", 1) for line in output if "=" in line)
        self.assertEqual(fields["task7_config"], task7.CONFIGURATION_IDENTITY)
        left = task7.decode_canonical_sample(bytes.fromhex(fields["task7_paired_sample_hex_a"]))
        right = task7.decode_canonical_sample(bytes.fromhex(fields["task7_paired_sample_hex_b"]))
        self.assertEqual(left.canonical_bytes, right.canonical_bytes)
        self.assertEqual(left.model_input_identity, right.model_input_identity)
        self.assertEqual(left.card_vocabulary_identity, right.card_vocabulary_identity)
        self.assertEqual(left.public_observation_digest, right.public_observation_digest)
        self.assertEqual(left.public_candidate_domain_digest, right.public_candidate_domain_digest)
        self.assertEqual(left.routing_keys, right.routing_keys)
        self.assertEqual(left.candidate_count, right.candidate_count)
        for left_table, right_table in zip(left.tables, right.tables):
            self.assertEqual(left_table.identity, right_table.identity)
            self.assertTrue(task7.torch.equal(left_table.sample_offsets, right_table.sample_offsets))
            self.assertTrue(task7.torch.equal(left_table.row_mask, right_table.row_mask))
            if left_table.parent_offsets is not None:
                self.assertTrue(task7.torch.equal(left_table.parent_offsets, right_table.parent_offsets))
            for left_column, right_column in zip(left_table.columns, right_table.columns):
                if isinstance(left_column.values, task7.Task7ReferenceColumnV1):
                    self.assertTrue(all(task7.torch.equal(a, b) for (_, a), (_, b)
                                        in zip(left_column.values.learner_tensors(), right_column.values.learner_tensors())))
                elif hasattr(left_column.values, "dtype"):
                    self.assertTrue(task7.torch.equal(left_column.values, right_column.values))
                else:
                    self.assertEqual(left_column.values, right_column.values)
                if left_column.presence is not None:
                    self.assertTrue(task7.torch.equal(left_column.presence, right_column.presence))

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_corrupt_canonical_bytes_fail_closed(self):
        sample = self._probe_sample("--rich")
        payload = bytearray(sample.canonical_bytes)
        payload[4] ^= 0x01
        with self.assertRaises(task7.Task7MaterializationError):
            task7.decode_canonical_sample(payload)
        with self.assertRaises(task7.Task7MaterializationError):
            task7.decode_canonical_sample(sample.canonical_bytes + b"\x00")

    @unittest.skipIf(task7.torch is None, "PyTorch is not available in this Python runtime")
    def test_fresh_process_repeat_is_byte_identical(self):
        first = self._probe_sample(25, "--rich")
        second = self._probe_sample(25, "--rich")
        self.assertEqual(first.configuration_identity, second.configuration_identity)
        self.assertEqual(first.canonical_bytes, second.canonical_bytes)
        self.assertEqual(first.routing_keys, second.routing_keys)
        for left, right in zip(first.learner_tensors, second.learner_tensors):
            self.assertEqual(left[:2], right[:2])
            self.assertTrue(task7.torch.equal(left[2], right[2]))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
