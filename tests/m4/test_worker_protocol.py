from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import unittest

from tools.m4.worker_protocol_contract import (
    CANONICAL_DECK_HASHES,
    CANONICAL_PATCHSET_SHA256,
    CANONICAL_RULES_BUNDLE_ID,
    ProtocolContractError,
    UINT32_MAX,
    UINT64_MAX,
    parse_json_line,
    recover_job_id,
    validate_ready,
    validate_result,
)
from tools.m4.worker_protocol import (
    ProtocolValidationError as CoordinatorProtocolValidationError,
    encode_job,
    job_to_message,
    validate_result as validate_coordinator_result,
)


ROOT = Path(__file__).resolve().parents[2]
WORKER = Path(
    os.environ.get("YGO_M4_WORKER", ROOT / "build" / "windows-zig" / "ygo_m4_worker.exe")
)


def ready_fixture() -> dict[str, object]:
    return {
        "schema": "ocgforge.m4.worker.v1",
        "type": "ready",
        "protocol_version": "ocgforge.m4.worker.v1",
        "pid": 1234,
        "rules_bundle_id": CANONICAL_RULES_BUNDLE_ID,
        "core_patchset_sha256": CANONICAL_PATCHSET_SHA256,
        "deck_hashes": list(CANONICAL_DECK_HASHES),
        "format_id": "TCG_ADVANCED_2026_05_18",
        "duel_mode_name": "DUEL_MODE_MR5",
        "duel_flags": 190464,
        "compiler_identity": "GNU-14.2.0",
        "build_type": "Debug",
        "worker_identity": "ocgforge.m4.native_worker.v1",
    }


def result_fixture() -> dict[str, object]:
    zero_errors = {
        "retries": 0,
        "unsupported": 0,
        "automatic": 0,
        "truncated": 0,
        "core_errors": 0,
        "worker_errors": 0,
    }
    return {
        "schema": "ocgforge.m4.worker.v1",
        "type": "result",
        "status": "passed",
        "job_id": "m4-000001",
        "terminal": True,
        "winner": 0,
        "win_reason": 1,
        "engine_steps": 10,
        "interactive_decisions": 2,
        "semantic_action_count": 2,
        "gameplay_hash": "a" * 64,
        "trace_hash": "b" * 64,
        "simulation_elapsed_us": 100,
        "coordinator_elapsed_us": None,
        "errors": zero_errors,
        "timing_us": {
            "core_process": 10,
            "protocol_candidate": 20,
            "continuation": 0,
            "observation": 30,
            "trace_hash": 5,
            "serialization": 5,
            "other": 30,
            "trace_persistence": 0,
        },
        "counters": {
            "ocg_duel_process": 10,
            "ocg_duel_query": 0,
            "ocg_duel_query_location": 0,
            "ocg_duel_query_field": 0,
            "ocg_duel_query_count": 0,
            "script_reader_requests": 1,
            "script_loads": 1,
            "observations": 2,
            "entities_projected": 4,
            "candidate_sets": 2,
            "candidate_total": 4,
            "candidate_max": 2,
            "semantic_hashes": 1,
            "trace_bytes_serialized": 100,
        },
        "worker": {
            "pid": 1234,
            "restart_index": 0,
            "crashed": False,
            "restarted": False,
        },
        "failure_code": None,
        "error_message": None,
    }


class WorkerProtocolContractTests(unittest.TestCase):
    def test_ready_matching(self) -> None:
        validate_ready(ready_fixture())

    def test_ready_rejects_wrong_protocol(self) -> None:
        message = ready_fixture()
        message["protocol_version"] = "ocgforge.m4.worker.invalid"
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)

    def test_ready_rejects_wrong_rules_bundle(self) -> None:
        message = ready_fixture()
        message["rules_bundle_id"] = "rules-invalid"
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)

    def test_ready_rejects_wrong_patchset_sha(self) -> None:
        message = ready_fixture()
        message["core_patchset_sha256"] = "patchset-invalid"
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)

    def test_ready_rejects_wrong_ordered_deck_hashes(self) -> None:
        message = ready_fixture()
        message["deck_hashes"] = list(reversed(CANONICAL_DECK_HASHES))
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)

    def test_ready_rejects_missing_or_extra_keys(self) -> None:
        message = ready_fixture()
        del message["build_type"]
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)
        message = ready_fixture()
        message["unexpected"] = True
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)

    def test_ready_rejects_wrong_worker_identity(self) -> None:
        message = ready_fixture()
        message["worker_identity"] = "ocgforge.m4.other_worker.v1"
        with self.assertRaises(ProtocolContractError):
            validate_ready(message)

    def test_result_rejects_job_id_mismatch(self) -> None:
        message = result_fixture()
        message["job_id"] = "m4-000002"
        with self.assertRaises(ProtocolContractError):
            validate_result(message, expected_job_id="m4-000001")

    def test_valid_failed_result_is_explicit(self) -> None:
        message = result_fixture()
        message.update(
            {
                "status": "failed",
                "terminal": False,
                "winner": None,
                "win_reason": None,
                "gameplay_hash": None,
                "trace_hash": None,
                "failure_code": "canonical_identity_mismatch",
                "error_message": "request identity does not match worker",
            }
        )
        message["errors"] = {
            **message["errors"],
            "worker_errors": 1,
        }
        validate_result(message, expected_job_id="m4-000001")

    def test_failed_result_requires_a_nonzero_error_counter(self) -> None:
        message = result_fixture()
        message.update(
            {
                "status": "failed",
                "terminal": False,
                "winner": None,
                "win_reason": None,
                "gameplay_hash": None,
                "trace_hash": None,
                "failure_code": "synthetic_failure",
                "error_message": "synthetic failure",
            }
        )
        with self.assertRaises(ProtocolContractError):
            validate_result(message, expected_job_id="m4-000001")

    def test_simulation_timing_requires_positive_pass_time_and_bounded_buckets(self) -> None:
        zero_pass = result_fixture()
        zero_pass["simulation_elapsed_us"] = 0
        with self.assertRaises(ProtocolContractError):
            validate_result(zero_pass, expected_job_id="m4-000001")

        zero_with_bucket = result_fixture()
        zero_with_bucket["simulation_elapsed_us"] = 0
        zero_with_bucket["timing_us"]["core_process"] = 1
        with self.assertRaises(ProtocolContractError):
            validate_result(zero_with_bucket, expected_job_id="m4-000001")

        over_budget = result_fixture()
        over_budget["simulation_elapsed_us"] = 99
        with self.assertRaises(ProtocolContractError):
            validate_result(over_budget, expected_job_id="m4-000001")

        failed_zero = result_fixture()
        failed_zero.update(
            {
                "status": "failed",
                "terminal": False,
                "winner": None,
                "win_reason": None,
                "gameplay_hash": None,
                "trace_hash": None,
                "simulation_elapsed_us": 0,
                "failure_code": "worker_validation",
                "error_message": "invalid job",
            }
        )
        failed_zero["errors"] = {**failed_zero["errors"], "worker_errors": 1}
        failed_zero["timing_us"] = {
            key: 0 for key in failed_zero["timing_us"]
        }
        validate_result(failed_zero, expected_job_id="m4-000001")

    def test_coordinator_rejects_worker_metadata_and_timing_overrides(self) -> None:
        for field, value in (
            ("coordinator", {}),
            ("coordinator_errors", {}),
            ("stderr", "worker diagnostics"),
        ):
            message = result_fixture()
            message[field] = value
            with self.subTest(field=field), self.assertRaises(
                CoordinatorProtocolValidationError
            ):
                validate_coordinator_result(message, expected_job_id="m4-000001")

        message = result_fixture()
        message["coordinator_elapsed_us"] = 17
        with self.assertRaises(CoordinatorProtocolValidationError):
            validate_coordinator_result(message, expected_job_id="m4-000001")

    def test_coordinator_still_accepts_valid_failed_worker_result(self) -> None:
        message = result_fixture()
        message.update(
            {
                "status": "failed",
                "terminal": False,
                "winner": None,
                "win_reason": None,
                "gameplay_hash": None,
                "trace_hash": None,
                "failure_code": "unsupported",
                "error_message": "deliberately unsupported test job",
            }
        )
        message["errors"] = {**message["errors"], "unsupported": 1}
        validate_coordinator_result(message, expected_job_id="m4-000001")

    def test_passed_result_rejects_nonzero_integrity_counter(self) -> None:
        message = result_fixture()
        message["errors"] = {
            **message["errors"],
            "unsupported": 1,
        }
        with self.assertRaises(ProtocolContractError):
            validate_result(message, expected_job_id="m4-000001")

    def test_passed_result_requires_winner_and_reason(self) -> None:
        for key in ("winner", "win_reason"):
            message = result_fixture()
            message[key] = None
            with self.subTest(key=key), self.assertRaises(ProtocolContractError):
                validate_result(message, expected_job_id="m4-000001")

    def test_passed_result_requires_64_hex_gameplay_hash(self) -> None:
        message = result_fixture()
        message["gameplay_hash"] = "not-a-sha256"
        with self.assertRaises(ProtocolContractError):
            validate_result(message, expected_job_id="m4-000001")
        message = result_fixture()
        message["trace_hash"] = None
        validate_result(message, expected_job_id="m4-000001")

    def test_result_rejects_negative_unsigned_value(self) -> None:
        message = result_fixture()
        message["engine_steps"] = -1
        with self.assertRaises(ProtocolContractError):
            validate_result(message, expected_job_id="m4-000001")

    def test_unsigned_fields_reject_uint64_overflow(self) -> None:
        ready = ready_fixture()
        ready["pid"] = 2**64
        with self.assertRaises(ProtocolContractError):
            validate_ready(ready)

        result = result_fixture()
        result["engine_steps"] = 2**64
        with self.assertRaises(ProtocolContractError):
            validate_result(result, expected_job_id="m4-000001")

        result = result_fixture()
        result["timing_us"]["core_process"] = 2**64
        with self.assertRaises(ProtocolContractError):
            validate_result(result, expected_job_id="m4-000001")

        result = result_fixture()
        result["counters"]["candidate_total"] = 2**64
        with self.assertRaises(ProtocolContractError):
            validate_result(result, expected_job_id="m4-000001")

    def test_job_unsigned_fields_reject_uint64_overflow(self) -> None:
        job = {
            "job_id": "m4-unsigned",
            "seed": 1,
            "starting_player": 0,
            "max_steps": 2200,
            "focus_codes": [],
        }
        for key in ("seed", "starting_player", "max_steps"):
            invalid = {**job, key: 2**64}
            with self.subTest(key=key), self.assertRaises(ValueError):
                job_to_message(invalid)

        invalid = {**job, "focus_codes": [2**64]}
        with self.assertRaises(ValueError):
            job_to_message(invalid)

    def test_outgoing_job_rejects_nonfinite_or_malformed_rules_identity(self) -> None:
        job = {
            "job_id": "m4-rules-identity",
            "seed": 1,
            "starting_player": 0,
            "max_steps": 2200,
            "focus_codes": [],
        }
        for value in (None, True, 1, "", float("nan"), float("inf"), float("-inf")):
            invalid = {**job, "canonical_rules_id": value}
            with self.subTest(value=repr(value)), self.assertRaises(ValueError):
                encode_job(invalid)

        encoded = encode_job({**job, "canonical_rules_id": CANONICAL_RULES_BUNDLE_ID})
        parsed = parse_json_line(encoded)
        self.assertEqual(parsed["canonical_rules_id"], CANONICAL_RULES_BUNDLE_ID)

    def test_outgoing_trace_output_matches_native_optional_string_contract(self) -> None:
        job = {
            "job_id": "m4-trace-output",
            "seed": 1,
            "starting_player": 0,
            "max_steps": 2200,
            "focus_codes": [],
        }

        self.assertNotIn("trace_output", job_to_message(job))
        self.assertNotIn("trace_output", job_to_message({**job, "trace_output": None}))

        empty = job_to_message({**job, "trace_output": ""})
        self.assertIn("trace_output", empty)
        self.assertEqual(empty["trace_output"], "")
        self.assertEqual(
            parse_json_line(encode_job({**job, "trace_output": ""}))["trace_output"],
            "",
        )

        for value in (False, 0, 0.0, [], {}, Path("trace.json"), object()):
            with self.subTest(value=repr(value)), self.assertRaises(ValueError):
                encode_job({**job, "trace_output": value})

    def test_native_uint32_boundaries_are_enforced(self) -> None:
        ready = ready_fixture()
        ready["pid"] = UINT32_MAX
        validate_ready(ready)
        ready["pid"] = UINT32_MAX + 1
        with self.assertRaises(ProtocolContractError):
            validate_ready(ready)

        for field in ("engine_steps", "interactive_decisions", "semantic_action_count"):
            valid = result_fixture()
            valid[field] = UINT32_MAX
            validate_result(valid, expected_job_id="m4-000001")
            invalid = result_fixture()
            invalid[field] = UINT32_MAX + 1
            with self.subTest(field=field), self.assertRaises(ProtocolContractError):
                validate_result(invalid, expected_job_id="m4-000001")

        for field in ("pid", "restart_index"):
            valid = result_fixture()
            valid["worker"][field] = UINT32_MAX
            validate_result(valid, expected_job_id="m4-000001")
            invalid = result_fixture()
            invalid["worker"][field] = UINT32_MAX + 1
            with self.subTest(field=f"worker.{field}"), self.assertRaises(
                ProtocolContractError
            ):
                validate_result(invalid, expected_job_id="m4-000001")

        job = {
            "job_id": "m4-uint32",
            "seed": (1 << 64) - 1,
            "starting_player": 0,
            "max_steps": UINT32_MAX,
            "focus_codes": [UINT32_MAX],
        }
        message = job_to_message(job)
        self.assertEqual(message["max_steps"], UINT32_MAX)
        self.assertEqual(message["focus_codes"], [UINT32_MAX])
        for field in ("max_steps",):
            invalid = {**job, field: UINT32_MAX + 1}
            with self.subTest(field=field), self.assertRaises(ValueError):
                job_to_message(invalid)
        invalid = {**job, "focus_codes": [UINT32_MAX + 1]}
        with self.assertRaises(ValueError):
            job_to_message(invalid)

    def test_native_uint64_fields_retain_uint64_bounds(self) -> None:
        job = {
            "job_id": "m4-uint64",
            "seed": (1 << 64) - 1,
            "starting_player": 0,
            "max_steps": 2200,
            "focus_codes": [],
        }
        self.assertEqual(job_to_message(job)["seed"], (1 << 64) - 1)
        invalid = {**job, "seed": 1 << 64}
        with self.assertRaises(ValueError):
            job_to_message(invalid)

    def test_outgoing_surrogate_pairs_normalize_before_utf8_encoding(self) -> None:
        job = {
            "job_id": "m4-surrogate-pair",
            "seed": 1,
            "starting_player": 0,
            "max_steps": 2200,
            "setup_script": "\ud83d\ude00",
            "replay_actions": ["\ud83d\ude00"],
            "focus_codes": [],
        }

        message = job_to_message(job)
        self.assertEqual(message["setup_script"], "\U0001f600")
        self.assertEqual(message["replay_actions"], ["\U0001f600"])
        encoded = encode_job(job)
        self.assertIn('"setup_script":"\U0001f600"', encoded)
        self.assertIn('"replay_actions":["\U0001f600"]', encoded)
        self.assertEqual(encoded.encode("utf-8").decode("utf-8"), encoded)

    def test_json_fixture_rejects_unpaired_surrogates_but_accepts_valid_unicode(self) -> None:
        unpaired = json.dumps({"nested": ["valid", "\ud800"]})
        with self.assertRaises(ProtocolContractError):
            parse_json_line(unpaired)

    def test_json_parser_rejects_all_negative_numeric_tokens(self) -> None:
        for token in ("-0", "-1", "-0.5", "-1e2"):
            with self.subTest(token=token), self.assertRaises(ProtocolContractError):
                parse_json_line(f'{{"value":{token}}}')

        valid_unicode = json.dumps({"nested": ["\U0001f600", "\ud83d\ude00"]})
        parsed = parse_json_line(valid_unicode)
        self.assertEqual(parsed["nested"], ["\U0001f600", "\U0001f600"])

    def test_json_parser_rejects_positive_decimal_and_exponent_tokens(self) -> None:
        for token in ("0.0", "1.5", "1e2", "1E+2"):
            with self.subTest(token=token), self.assertRaises(ProtocolContractError):
                parse_json_line(f'{{"value":{token}}}')

    def test_json_parser_rejects_uint64_overflow_and_accepts_integer_bounds(self) -> None:
        for token in (str(UINT64_MAX + 1), str(UINT64_MAX + 10)):
            with self.subTest(token=token), self.assertRaises(ProtocolContractError):
                parse_json_line(f'{{"value":{token}}}')

        parsed = parse_json_line(
            f'{{"zero":0,"one":1,"maximum":{UINT64_MAX},"text":"\U0001f600"}}'
        )
        self.assertEqual(parsed, {"zero": 0, "one": 1, "maximum": UINT64_MAX, "text": "\U0001f600"})

    def test_json_fixture_rejects_duplicate_and_trailing_data(self) -> None:
        with self.assertRaises(ProtocolContractError):
            parse_json_line('{"schema":1,"schema":2}')
        with self.assertRaises(ProtocolContractError):
            parse_json_line('{"schema":"ocgforge.m4.worker.v1"} trailing')

    def test_malformed_json_without_recoverable_job_id_is_not_a_result(self) -> None:
        malformed = '{"type":"job","job_id":'
        with self.assertRaises(ProtocolContractError):
            parse_json_line(malformed)
        self.assertIsNone(recover_job_id(malformed))

    def test_malformed_json_recovers_only_root_job_id_read_before_failure(self) -> None:
        root_id_then_nested_failure = (
            '{"type":"job","job_id":"root-id",'
            '"nested":{"job_id":"nested-id","broken":'
        )
        nested_id_only_then_failure = (
            '{"type":"job","nested":{"job_id":"nested-id","broken":'
        )
        invalid_root_types = (
            '{"type":"job","job_id":123,"broken":',
            '{"type":"job","job_id":null,"broken":',
            '{"type":"job","job_id":[],"broken":',
        )
        late_root_id = '{"type":"job","broken":,"job_id":"late-id"}'

        for malformed in (
            root_id_then_nested_failure,
            nested_id_only_then_failure,
            *invalid_root_types,
            late_root_id,
        ):
            with self.subTest(malformed=malformed), self.assertRaises(
                ProtocolContractError
            ):
                parse_json_line(malformed)

        self.assertEqual(recover_job_id(root_id_then_nested_failure), "root-id")
        self.assertIsNone(recover_job_id(nested_id_only_then_failure))
        for malformed in invalid_root_types:
            with self.subTest(malformed=malformed):
                self.assertIsNone(recover_job_id(malformed))
        self.assertIsNone(recover_job_id(late_root_id))

    def test_deep_malformed_json_is_a_protocol_error(self) -> None:
        deep = "[" * 3000 + "0" + "]" * 3000
        with self.assertRaises(ProtocolContractError):
            parse_json_line(deep)


@unittest.skipUnless(WORKER.is_file(), f"native worker not found: {WORKER}")
class NativeWorkerProtocolRegressionTests(unittest.TestCase):
    def test_recovery_is_root_job_id_only(self) -> None:
        top_level_id_then_nested_failure = (
            '{"schema":"ocgforge.m4.worker.v1","type":"job",'
            '"job_id":"top-level","nested":{"job_id":"nested","broken":'
        )
        nested_id_only_then_failure = (
            '{"schema":"ocgforge.m4.worker.v1","type":"job",'
            '"nested":{"job_id":"nested","broken":'
        )
        completed = subprocess.run(
            [str(WORKER)],
            input=top_level_id_then_nested_failure + "\n" + nested_id_only_then_failure + "\n",
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        messages = [json.loads(line) for line in completed.stdout.splitlines()]
        self.assertEqual(len(messages), 3, messages)
        validate_ready(messages[0])
        validate_result(messages[1], expected_job_id="top-level")
        self.assertEqual(messages[1]["status"], "failed")
        self.assertEqual(messages[2]["type"], "protocol_error")
        self.assertIsNone(messages[2]["job_id"])


if __name__ == "__main__":
    unittest.main()
