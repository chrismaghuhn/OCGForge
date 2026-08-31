from __future__ import annotations

import builtins
import contextlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
EXPORTER_PATH = ROOT / "tools" / "observability" / "phase4_trackio_export.py"
REPORT_PATH = ROOT / "docs" / "p4c" / "p4c_acceptance.json"
MARKDOWN_PATH = ROOT / "docs" / "p4c" / "P4C_ACCEPTANCE.md"
ACCEPTED_SOURCE_HEAD = "9fe935531b63aaaf9535201dd4daf3f25e0f1a93"


def load_exporter():
    spec = importlib.util.spec_from_file_location("phase4_trackio_export", EXPORTER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load the Trackio exporter")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class FakeTrackio:
    def __init__(self) -> None:
        self.calls: list[tuple[str, object]] = []

    def init(self, **kwargs: object) -> object:
        self.calls.append(("init", kwargs))
        return object()

    def log(self, metrics: dict[str, int], *, step: int) -> None:
        self.calls.append(("log", (metrics, step)))

    def finish(self) -> None:
        self.calls.append(("finish", None))


class ExporterPresenceTests(unittest.TestCase):
    def test_exporter_module_is_present(self) -> None:
        self.assertTrue(EXPORTER_PATH.is_file(), "Trackio exporter module is missing")


@unittest.skipUnless(EXPORTER_PATH.is_file(), "implementation is not present yet")
class Phase4TrackioExportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.exporter = load_exporter()

    def accepted_projection(self) -> dict[str, object]:
        report = self.exporter.load_validated_report(REPORT_PATH, MARKDOWN_PATH)
        return self.exporter.project_report(report)

    def test_accepted_phase4c_report_projects_successfully(self) -> None:
        projection = self.accepted_projection()
        self.assertIn("config", projection)
        self.assertIn("metrics", projection)

    def test_exporter_schema_is_exact(self) -> None:
        self.assertEqual(
            self.exporter.EXPORTER_SCHEMA,
            "ocgforge.trackio.phase4_evaluation_export.v1",
        )

    def test_public_config_allowlist_is_exact(self) -> None:
        projection = self.accepted_projection()
        expected = {
            "ocgforge_source_head",
            "ocgforge_source_base",
            "matchup_id",
            "rules_bundle_id",
            "format_id",
            "duel_mode",
            "duel_flags",
            "teacher_producer_identity",
            "battle_snapshot_schema",
            "provable_lethal_schema",
            "integration_decision",
            "positive_lethal_capability",
            "acceptance_schema",
            "acceptance_status",
            "trackio_version",
            "exporter_schema",
        }
        self.assertEqual(set(projection["config"]), expected)

    def test_numeric_metric_allowlist_is_exact(self) -> None:
        projection = self.accepted_projection()
        expected = {
            "gates_pass_count",
            "gates_total_count",
            "matrix_rows_pass_count",
            "matrix_rows_total_count",
            "record_count",
            "battle_decision_record_count",
            "battle_command_candidate_count",
            "sidecar_invalid_count",
            "proven_lethal_count",
            "lower_bound_present_count",
            "command_record_count",
            "failed_command_record_count",
            "sidecar_influences_gameplay",
        }
        self.assertEqual(set(projection["metrics"]), expected)
        self.assertTrue(all(type(value) is int for value in projection["metrics"].values()))

    def test_current_report_has_fifteen_of_fifteen_gates(self) -> None:
        metrics = self.accepted_projection()["metrics"]
        self.assertEqual(metrics["gates_pass_count"], 15)
        self.assertEqual(metrics["gates_total_count"], 15)

    def test_current_report_has_four_passing_matrix_rows(self) -> None:
        metrics = self.accepted_projection()["metrics"]
        self.assertEqual(metrics["matrix_rows_pass_count"], 4)
        self.assertEqual(metrics["matrix_rows_total_count"], 4)

    def test_current_report_has_128_records(self) -> None:
        self.assertEqual(self.accepted_projection()["metrics"]["record_count"], 128)

    def test_current_report_has_sixteen_battle_records(self) -> None:
        self.assertEqual(
            self.accepted_projection()["metrics"]["battle_decision_record_count"],
            16,
        )

    def test_current_report_has_thirty_two_battle_candidates(self) -> None:
        self.assertEqual(
            self.accepted_projection()["metrics"]["battle_command_candidate_count"],
            32,
        )

    def test_current_report_has_zero_sidecar_invalid_records(self) -> None:
        self.assertEqual(self.accepted_projection()["metrics"]["sidecar_invalid_count"], 0)

    def test_current_report_has_zero_proven_lethal_records(self) -> None:
        self.assertEqual(self.accepted_projection()["metrics"]["proven_lethal_count"], 0)

    def test_current_report_has_zero_lower_bound_records(self) -> None:
        self.assertEqual(self.accepted_projection()["metrics"]["lower_bound_present_count"], 0)

    def test_current_report_has_45_command_records(self) -> None:
        self.assertEqual(self.accepted_projection()["metrics"]["command_record_count"], 45)

    def test_current_report_has_zero_failed_command_records(self) -> None:
        self.assertEqual(self.accepted_projection()["metrics"]["failed_command_record_count"], 0)

    def test_sidecar_influence_maps_to_zero(self) -> None:
        self.assertEqual(
            self.accepted_projection()["metrics"]["sidecar_influences_gameplay"],
            0,
        )

    def test_projected_json_has_no_forbidden_private_or_runtime_fields(self) -> None:
        projection = self.accepted_projection()
        serialized = json.dumps(projection, sort_keys=True)
        for forbidden in (
            "CoreHost",
            "PlayerObservation",
            "private observation",
            "semantic_key",
            "response_bytes",
            "SubmissionToken",
            "passcode",
            "observation locator",
            "physical locator",
            "PID",
            "hostname",
            "wall time",
            "cache path",
            "C:\\",
            "C:/",
            "run_id",
        ):
            self.assertNotIn(forbidden, serialized)

    def test_current_head_may_differ_from_frozen_accepted_source_head(self) -> None:
        current_head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=ROOT,
            text=True,
        ).strip()
        self.assertNotEqual(current_head, ACCEPTED_SOURCE_HEAD)
        report = self.exporter.load_validated_report(REPORT_PATH, MARKDOWN_PATH)
        self.assertEqual(report["source_head"], ACCEPTED_SOURCE_HEAD)

    def test_mutated_report_source_head_is_rejected(self) -> None:
        report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
        report["source_head"] = "0" * 40
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "report.json"
            report_path.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            with self.assertRaises(self.exporter.ValidationFailure):
                self.exporter.load_validated_report(report_path, MARKDOWN_PATH)

    def test_malformed_report_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "report.json"
            report_path.write_text("{\"status\": \"PASS\"}\n", encoding="utf-8")
            with self.assertRaises(self.exporter.ValidationFailure):
                self.exporter.load_validated_report(report_path, MARKDOWN_PATH)

    def test_report_status_must_remain_pass(self) -> None:
        report = json.loads(REPORT_PATH.read_text(encoding="utf-8"))
        report["status"] = "FAIL"
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "report.json"
            report_path.write_text(
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            with self.assertRaises(self.exporter.ValidationFailure):
                self.exporter.load_validated_report(report_path, MARKDOWN_PATH)

    def test_invalid_report_fails_before_trackio_initialization(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report_path = Path(directory) / "report.json"
            report_path.write_text("not-json\n", encoding="utf-8")
            stdout = io.StringIO()
            with mock.patch.object(
                self.exporter,
                "_load_trackio_adapter",
                side_effect=AssertionError("Trackio must not initialize"),
            ):
                with contextlib.redirect_stdout(stdout):
                    exit_code = self.exporter.main(
                        [
                            "--report",
                            str(report_path),
                            "--markdown",
                            str(MARKDOWN_PATH),
                        ]
                    )
        self.assertEqual(exit_code, 1)
        self.assertEqual(stdout.getvalue(), "EXPORT=FAIL\nTRACKIO_INIT=NOT_RUN\n")

    def test_existing_validator_file_validation_is_called_programmatically(self) -> None:
        calls: list[tuple[Path, Path, str | None]] = []

        class FakeValidator:
            def validate_files(
                self,
                report_path: Path,
                markdown_path: Path,
                expected_source_head: str | None = None,
            ) -> None:
                calls.append((report_path, markdown_path, expected_source_head))

            def main(self) -> None:
                raise AssertionError("validator CLI main must not be called")

        with mock.patch.object(self.exporter, "_load_validator_module", return_value=FakeValidator()):
            report = self.exporter.load_validated_report(REPORT_PATH, MARKDOWN_PATH)
        self.assertEqual(report["status"], "PASS")
        self.assertEqual(calls, [(REPORT_PATH, MARKDOWN_PATH, ACCEPTED_SOURCE_HEAD)])

    def test_dry_run_does_not_import_or_initialize_trackio(self) -> None:
        stdout = io.StringIO()
        original_import = builtins.__import__

        def reject_trackio(name: str, *args: object, **kwargs: object):
            if name == "trackio":
                raise AssertionError("dry-run imported Trackio")
            return original_import(name, *args, **kwargs)

        with mock.patch("builtins.__import__", side_effect=reject_trackio):
            with contextlib.redirect_stdout(stdout):
                exit_code = self.exporter.main(["--dry-run"])
        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(set(payload), {"config", "metrics", "project", "run_name"})

    def test_dry_run_uses_deterministic_default_project_and_run_name(self) -> None:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            exit_code = self.exporter.main(["--dry-run"])
        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["project"], "ocgforge-phase4-evaluation")
        self.assertEqual(payload["run_name"], "phase4c-9fe935531b63")

    def test_dry_run_accepts_custom_project_and_run_name(self) -> None:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            exit_code = self.exporter.main(
                [
                    "--dry-run",
                    "--project",
                    "pilot-project",
                    "--run-name",
                    "pilot-run",
                ]
            )
        self.assertEqual(exit_code, 0)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["project"], "pilot-project")
        self.assertEqual(payload["run_name"], "pilot-run")

    def test_repeated_dry_run_in_fresh_processes_is_byte_identical(self) -> None:
        command = [sys.executable, "-B", str(EXPORTER_PATH), "--dry-run"]
        first = subprocess.run(command, cwd=ROOT, capture_output=True, check=False)
        second = subprocess.run(command, cwd=ROOT, capture_output=True, check=False)
        self.assertEqual(first.returncode, 0, first.stderr.decode())
        self.assertEqual(second.returncode, 0, second.stderr.decode())
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(first.stderr, second.stderr)

    def test_trackio_adapter_receives_one_init_one_log_step_zero_and_one_finish(self) -> None:
        projection = self.accepted_projection()
        fake = FakeTrackio()
        with mock.patch.dict(
            os.environ,
            {"TRACKIO_SPACE_ID": "", "TRACKIO_SERVER_URL": ""},
            clear=False,
        ):
            self.exporter.export_to_trackio(
                project="ocgforge-phase4-evaluation",
                run_name="phase4c-9fe935531b63",
                projection=projection,
                adapter=fake,
            )
        self.assertEqual([name for name, _ in fake.calls], ["init", "log", "finish"])
        self.assertEqual(
            fake.calls[0],
            (
                "init",
                {
                    "project": "ocgforge-phase4-evaluation",
                    "name": "phase4c-9fe935531b63",
                    "config": projection["config"],
                    "embed": False,
                    "auto_log_gpu": False,
                    "auto_log_cpu": False,
                },
            ),
        )
        self.assertEqual(fake.calls[1], ("log", (projection["metrics"], 0)))

    def test_trackio_system_auto_logging_flags_are_exactly_false(self) -> None:
        projection = self.accepted_projection()
        fake = FakeTrackio()
        with mock.patch.dict(
            os.environ,
            {"TRACKIO_SPACE_ID": "", "TRACKIO_SERVER_URL": ""},
            clear=False,
        ):
            self.exporter.export_to_trackio(
                project="ocgforge-phase4-evaluation",
                run_name="phase4c-9fe935531b63",
                projection=projection,
                adapter=fake,
        )
        init_kwargs = fake.calls[0][1]
        self.assertIs(init_kwargs.get("auto_log_gpu"), False)
        self.assertIs(init_kwargs.get("auto_log_cpu"), False)

    def assert_remote_environment_rejected(self, variable: str, value: str) -> None:
        fake = FakeTrackio()
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, {variable: value}, clear=False):
            with mock.patch.object(self.exporter, "_load_trackio_adapter", return_value=fake):
                with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                    exit_code = self.exporter.main([])
        self.assertEqual(exit_code, 1)
        self.assertEqual(stdout.getvalue(), "TRACKIO_EXPORT=FAIL\nTRACKIO_INIT=NOT_RUN\n")
        self.assertEqual(
            stderr.getvalue(),
            "ERROR=remote Trackio configuration is forbidden for this local-only pilot\n",
        )
        self.assertNotIn(value, stdout.getvalue() + stderr.getvalue())
        self.assertEqual(fake.calls, [])

    def test_trackio_space_id_rejects_real_export_before_init(self) -> None:
        self.assert_remote_environment_rejected(
            "TRACKIO_SPACE_ID", "space-secret-value"
        )

    def test_trackio_server_url_rejects_real_export_before_init(self) -> None:
        self.assert_remote_environment_rejected(
            "TRACKIO_SERVER_URL", "https://secret.example.invalid/token"
        )

    def test_dry_run_remains_independent_of_remote_environment(self) -> None:
        stdout = io.StringIO()
        with mock.patch.dict(
            os.environ,
            {
                "TRACKIO_SPACE_ID": "space-secret-value",
                "TRACKIO_SERVER_URL": "https://secret.example.invalid/token",
            },
            clear=False,
        ):
            with contextlib.redirect_stdout(stdout):
                exit_code = self.exporter.main(["--dry-run"])
        self.assertEqual(exit_code, 0)
        self.assertEqual(json.loads(stdout.getvalue())["project"], "ocgforge-phase4-evaluation")
        self.assertNotIn("space-secret-value", stdout.getvalue())
        self.assertNotIn("secret.example.invalid", stdout.getvalue())


if __name__ == "__main__":
    unittest.main()
