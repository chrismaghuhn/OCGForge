"""Compare the focused M4.3.5 serialization fixtures across two build paths.

This runner deliberately stops at deterministic fixture and builder-test
equivalence. It does not start worker conformance or throughput measurement.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


_REPO_ROOT = Path(__file__).resolve().parents[2]
_FIXTURE_DIGEST_RE = re.compile(
    r"^m4_3_5_fixture=(?P<name>[^\s]+)"
    r" without_hash_bytes=(?P<without_hash_bytes>[0-9]+)"
    r" without_hash_sha256=(?P<without_hash_sha256>[0-9a-f]{64})"
    r" observation_hash=(?P<observation_hash>[0-9a-f]{64})"
    r" canonical_bytes=(?P<canonical_bytes>[0-9]+)"
    r" canonical_sha256=(?P<canonical_sha256>[0-9a-f]{64})$"
)
_FIXTURE_OK_LINE = "m4_3_5_fixture_test=ok"


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _fixture_digest_lines(stdout: bytes) -> tuple[list[str], list[str]]:
    text = stdout.decode("utf-8", errors="replace")
    lines = text.splitlines()
    digest_lines = [line for line in lines if line.startswith("m4_3_5_fixture=")]
    errors: list[str] = []
    for line in digest_lines:
        if _FIXTURE_DIGEST_RE.fullmatch(line) is None:
            errors.append(f"malformed fixture digest line: {line!r}")
    if len(digest_lines) != 2:
        errors.append(f"expected exactly 2 fixture digest lines, got {len(digest_lines)}")
    if _FIXTURE_OK_LINE not in lines:
        errors.append(f"missing {_FIXTURE_OK_LINE!r} marker")
    return digest_lines, errors


def _path_record(path: Path) -> dict[str, Any]:
    resolved = path.expanduser().resolve()
    return {
        "path": str(resolved),
        "exists": resolved.is_file(),
    }


def _run_executable(path: Path, *, check_fixture_contract: bool) -> dict[str, Any]:
    resolved = path.expanduser().resolve()
    record: dict[str, Any] = {
        **_path_record(resolved),
        "exitcode": None,
        "stdout_bytes": 0,
        "stderr_bytes": 0,
        "stdout_sha256": _sha256_bytes(b""),
        "stderr_sha256": _sha256_bytes(b""),
        "digest_lines": [],
        "errors": [],
        "_stdout_raw": b"",
        "_stderr_raw": b"",
    }
    if not resolved.is_file():
        record["errors"].append(f"missing executable: {resolved}")
        return record

    try:
        completed = subprocess.run(
            [str(resolved)],
            cwd=_REPO_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=240.0,
        )
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout if isinstance(error.stdout, bytes) else b""
        stderr = error.stderr if isinstance(error.stderr, bytes) else b""
        record["stdout_bytes"] = len(stdout)
        record["stderr_bytes"] = len(stderr)
        record["stdout_sha256"] = _sha256_bytes(stdout)
        record["stderr_sha256"] = _sha256_bytes(stderr)
        record["_stdout_raw"] = stdout
        record["_stderr_raw"] = stderr
        record["errors"].append("executable timed out after 240 seconds")
        return record
    except OSError as error:
        record["errors"].append(f"failed to execute {resolved}: {error}")
        return record

    stdout = completed.stdout
    stderr = completed.stderr
    record["exitcode"] = completed.returncode
    record["stdout_bytes"] = len(stdout)
    record["stderr_bytes"] = len(stderr)
    record["stdout_sha256"] = _sha256_bytes(stdout)
    record["stderr_sha256"] = _sha256_bytes(stderr)
    record["_stdout_raw"] = stdout
    record["_stderr_raw"] = stderr
    if completed.returncode != 0:
        record["errors"].append(f"nonzero exitcode: {completed.returncode}")
    if check_fixture_contract:
        digest_lines, contract_errors = _fixture_digest_lines(stdout)
        record["digest_lines"] = digest_lines
        record["errors"].extend(contract_errors)
    return record


def _pair_comparison(
    control: dict[str, Any],
    experiment: dict[str, Any],
    *,
    fixture: bool,
) -> dict[str, Any]:
    control_exitcode = control["exitcode"]
    experiment_exitcode = experiment["exitcode"]
    result: dict[str, Any] = {
        "exitcodes_equal": control_exitcode == experiment_exitcode,
        "both_exitcodes_zero": control_exitcode == 0 and experiment_exitcode == 0,
        "stdout_bytes_equal": control["stdout_bytes"] == experiment["stdout_bytes"],
        "stdout_sha256_equal": control["stdout_sha256"] == experiment["stdout_sha256"],
        "stdout_byte_exact": control["_stdout_raw"] == experiment["_stdout_raw"],
        "stderr_bytes_equal": control["stderr_bytes"] == experiment["stderr_bytes"],
        "stderr_sha256_equal": control["stderr_sha256"] == experiment["stderr_sha256"],
        "stderr_byte_exact": control["_stderr_raw"] == experiment["_stderr_raw"],
    }
    if fixture:
        result["digest_lines_equal"] = control["digest_lines"] == experiment["digest_lines"]
        result["fixture_contract_valid"] = not control["errors"] and not experiment["errors"]
    result["pass"] = all(result.values())
    return result


def _write_report(destination: Path, report: dict[str, Any]) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _public_record(record: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in record.items() if not key.startswith("_")}


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control-fixture", required=True, type=Path)
    parser.add_argument("--experiment-fixture", required=True, type=Path)
    parser.add_argument("--control-builder", required=True, type=Path)
    parser.add_argument("--experiment-builder", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    args = parser.parse_args(arguments)

    control_fixture = _run_executable(args.control_fixture, check_fixture_contract=True)
    experiment_fixture = _run_executable(args.experiment_fixture, check_fixture_contract=True)
    control_builder = _run_executable(args.control_builder, check_fixture_contract=False)
    experiment_builder = _run_executable(args.experiment_builder, check_fixture_contract=False)

    fixture_comparison = _pair_comparison(control_fixture, experiment_fixture, fixture=True)
    builder_comparison = _pair_comparison(control_builder, experiment_builder, fixture=False)
    all_errors = (
        control_fixture["errors"]
        + experiment_fixture["errors"]
        + control_builder["errors"]
        + experiment_builder["errors"]
    )
    report = {
        "schema": "ocgforge.m4.m4_3_5_reserve_serialization_fixture_comparison.v1",
        "scope": {
            "fixture_and_builder_tests_only": True,
            "worker_conformance": "NOT_RUN",
            "throughput_measurement": "NOT_RUN",
        },
        "control": {
            "fixture": _public_record(control_fixture),
            "builder": _public_record(control_builder),
        },
        "experiment": {
            "fixture": _public_record(experiment_fixture),
            "builder": _public_record(experiment_builder),
        },
        "digest_lines": {
            "control": control_fixture["digest_lines"],
            "experiment": experiment_fixture["digest_lines"],
        },
        "comparison": {
            "fixture": fixture_comparison,
            "builder": builder_comparison,
            "pass": fixture_comparison["pass"] and builder_comparison["pass"] and not all_errors,
        },
        "errors": all_errors,
    }
    _write_report(args.output_json, report)
    print(json.dumps(report["comparison"], ensure_ascii=False, sort_keys=True))
    return 0 if report["comparison"]["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
