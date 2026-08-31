from __future__ import annotations

import argparse
import importlib
import importlib.util
import json
from importlib.metadata import PackageNotFoundError
from importlib.metadata import version as installed_package_version
from pathlib import Path
import sys
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_REPORT = ROOT / "docs" / "p4c" / "p4c_acceptance.json"
DEFAULT_MARKDOWN = ROOT / "docs" / "p4c" / "P4C_ACCEPTANCE.md"
VALIDATOR_PATH = ROOT / "tests" / "teacher" / "phase4c_acceptance_test.py"

DEFAULT_PROJECT = "ocgforge-phase4-evaluation"
TRACKIO_TARGET_VERSION = "0.37.0"
PHASE4C_ACCEPTED_SOURCE_HEAD = "9fe935531b63aaaf9535201dd4daf3f25e0f1a93"
EXPORTER_SCHEMA = "ocgforge.trackio.phase4_evaluation_export.v1"

PUBLIC_CONFIG_KEYS = frozenset(
    {
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
)

NUMERIC_METRIC_KEYS = frozenset(
    {
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
)


class ValidationFailure(RuntimeError):
    """The accepted Phase-4C evidence could not be validated."""


class TrackioUnavailable(RuntimeError):
    """The optional, pinned Trackio dependency is not usable."""


class TrackioExportFailure(RuntimeError):
    """Trackio did not complete the one-run export sequence."""


def _load_validator_module() -> Any:
    spec = importlib.util.spec_from_file_location(
        "ocgforge_phase4c_acceptance_validator", VALIDATOR_PATH
    )
    if spec is None or spec.loader is None:
        raise ValidationFailure("could not load the Phase-4C acceptance validator")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_validated_report(report_path: Path, markdown_path: Path) -> dict[str, Any]:
    """Validate and load one accepted Phase-4C report without invoking CLI main()."""
    report_path = Path(report_path)
    markdown_path = Path(markdown_path)
    try:
        validator = _load_validator_module()
        validator.validate_files(
            report_path,
            markdown_path,
            expected_source_head=PHASE4C_ACCEPTED_SOURCE_HEAD,
        )
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if not isinstance(report, dict) or report.get("status") != "PASS":
            raise AssertionError("accepted report is not overall PASS")
        return report
    except Exception as error:
        raise ValidationFailure("Phase-4C acceptance validation failed") from error


def _sidecar_influence_metric(value: str) -> int:
    if value == "NO":
        return 0
    if value == "YES":
        return 1
    raise ValueError("invalid sidecar influence value")


def project_report(report: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    """Project validated Phase-4C evidence into public presentation values."""
    environment = report["environment"]
    teacher = report["teacher"]
    battle_contracts = report["battle_contracts"]
    gates = report["gates"]
    matrix = report["fixed_matchup_matrix"]
    task5_metrics = report["task5_metrics"]
    command_evidence = report["command_evidence"]

    config = {
        "ocgforge_source_head": report["source_head"],
        "ocgforge_source_base": report["source_base"],
        "matchup_id": environment["matchup_id"],
        "rules_bundle_id": environment["rules_bundle_id"],
        "format_id": environment["format_id"],
        "duel_mode": environment["duel_mode"],
        "duel_flags": environment["duel_flags"],
        "teacher_producer_identity": teacher["producer_implementation_identity"],
        "battle_snapshot_schema": battle_contracts["snapshot_schema"],
        "provable_lethal_schema": battle_contracts["lethal_schema"],
        "integration_decision": report["integration_decision"],
        "positive_lethal_capability": report["positive_lethal_capability"],
        "acceptance_schema": report["schema_version"],
        "acceptance_status": report["status"],
        "trackio_version": TRACKIO_TARGET_VERSION,
        "exporter_schema": EXPORTER_SCHEMA,
    }

    metrics = {
        "gates_pass_count": sum(item["status"] == "PASS" for item in gates),
        "gates_total_count": len(gates),
        "matrix_rows_pass_count": sum(item["status"] == "PASS" for item in matrix),
        "matrix_rows_total_count": len(matrix),
        "record_count": task5_metrics["record_count"],
        "battle_decision_record_count": task5_metrics["battle_decision_record_count"],
        "battle_command_candidate_count": task5_metrics[
            "battle_command_candidate_count"
        ],
        "sidecar_invalid_count": task5_metrics["sidecar_invalid_count"],
        "proven_lethal_count": task5_metrics["proven_lethal_count"],
        "lower_bound_present_count": task5_metrics["lower_bound_present_count"],
        "command_record_count": len(command_evidence),
        "failed_command_record_count": sum(
            item["status"] == "FAIL" for item in command_evidence
        ),
        "sidecar_influences_gameplay": _sidecar_influence_metric(
            task5_metrics["sidecar_influences_gameplay"]
        ),
    }
    return {"config": config, "metrics": metrics}


def _dry_run_payload(
    project: str, run_name: str, projection: Mapping[str, Mapping[str, Any]]
) -> dict[str, Any]:
    return {
        "config": projection["config"],
        "metrics": projection["metrics"],
        "project": project,
        "run_name": run_name,
    }


def _canonical_json(payload: Mapping[str, Any]) -> str:
    return json.dumps(
        payload,
        allow_nan=False,
        ensure_ascii=False,
        indent=2,
        sort_keys=True,
    ) + "\n"


def _load_trackio_adapter() -> Any:
    try:
        installed_version = installed_package_version("trackio")
    except PackageNotFoundError as error:
        raise TrackioUnavailable(
            "Trackio 0.37.0 is optional; install with "
            "python -m pip install trackio==0.37.0"
        ) from error
    if installed_version != TRACKIO_TARGET_VERSION:
        raise TrackioUnavailable(
            f"Trackio {TRACKIO_TARGET_VERSION} is required; found {installed_version}"
        )
    try:
        return importlib.import_module("trackio")
    except ImportError as error:
        raise TrackioUnavailable(
            "Trackio 0.37.0 could not be imported after installation"
        ) from error


def export_to_trackio(
    project: str,
    run_name: str,
    projection: Mapping[str, Mapping[str, Any]],
    adapter: Any,
) -> None:
    """Send one validated projection through the documented Trackio API."""
    try:
        adapter.init(
            project=project,
            name=run_name,
            config=projection["config"],
            embed=False,
        )
    except Exception as error:
        raise TrackioExportFailure("Trackio init failed") from error

    try:
        adapter.log(projection["metrics"], step=0)
    except Exception as error:
        raise TrackioExportFailure("Trackio log failed") from error
    finally:
        try:
            adapter.finish()
        except Exception as error:
            raise TrackioExportFailure("Trackio finish failed") from error


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Export validated Phase-4 evidence as optional Trackio metrics."
    )
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--markdown", type=Path, default=DEFAULT_MARKDOWN)
    parser.add_argument("--project", default=DEFAULT_PROJECT)
    parser.add_argument("--run-name")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        report = load_validated_report(args.report, args.markdown)
        projection = project_report(report)
    except (ValidationFailure, KeyError, TypeError, ValueError):
        print("EXPORT=FAIL")
        print("TRACKIO_INIT=NOT_RUN")
        return 1

    run_name = args.run_name or f"phase4c-{report['source_head'][:12]}"
    if args.dry_run:
        sys.stdout.write(_canonical_json(_dry_run_payload(args.project, run_name, projection)))
        return 0

    try:
        adapter = _load_trackio_adapter()
    except TrackioUnavailable as error:
        print("TRACKIO_EXPORT=FAIL")
        print("TRACKIO_INIT=NOT_RUN")
        print(f"ERROR={error}", file=sys.stderr)
        return 1

    try:
        export_to_trackio(args.project, run_name, projection, adapter)
    except TrackioExportFailure as error:
        print("TRACKIO_EXPORT=FAIL")
        print("TRACKIO_INIT=FAIL")
        print(f"ERROR={error}", file=sys.stderr)
        return 1

    print("TRACKIO_EXPORT=PASS")
    print(f"PROJECT={args.project}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
