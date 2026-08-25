"""Run the required M4.3.4 gates and persist immutable log evidence."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from datetime import datetime, timezone
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
GATE_COMMANDS: dict[str, list[str]] = {
    "ctests": ["ctest", "--test-dir", "build/m4-3-4-shape", "--output-on-failure"],
    "repository_python_tests": [sys.executable, "-m", "unittest", "discover", "-s", "tests/python", "-v"],
    "m3_python_tests": [sys.executable, "-m", "unittest", "discover", "-s", "tests/m3", "-v"],
    "m4_python_tests": [sys.executable, "-m", "unittest", "discover", "-s", "tests/m4", "-v"],
    "privacy_tests": [
        "ctest",
        "--test-dir",
        "build/m4-3-4-shape",
        "--output-on-failure",
        "-R",
        "^(privacy_projection_test|continuation_privacy_test|m3_real_deck_privacy_test)$",
    ],
    "candidate_observation_consistency": [
        "ctest",
        "--test-dir",
        "build/m4-3-4-shape",
        "--output-on-failure",
        "-R",
        "^(observation_builder_test|m4_worker_integration_test)$",
    ],
    "worker_count_semantic_gate": [
        sys.executable,
        "-m",
        "unittest",
        "tests.m4.test_worker_integration.NativeWorkerIntegrationTests.test_worker_counts_preserve_semantics_and_trace_hashes",
        "-v",
    ],
}


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _run_gate(name: str, command: list[str], log_dir: Path) -> dict[str, Any]:
    started = datetime.now(timezone.utc).isoformat()
    start = time.perf_counter()
    completed = subprocess.run(
        command,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        timeout=3600.0,
    )
    elapsed_us = int((time.perf_counter() - start) * 1_000_000)
    stdout_path = log_dir / f"{name}.stdout.txt"
    stderr_path = log_dir / f"{name}.stderr.txt"
    stdout_path.write_bytes(completed.stdout)
    stderr_path.write_bytes(completed.stderr)
    return {
        "command": command,
        "started_utc": started,
        "elapsed_us": elapsed_us,
        "returncode": completed.returncode,
        "stdout_path": str(stdout_path.resolve()),
        "stdout_bytes": stdout_path.stat().st_size,
        "stdout_sha256": _sha256(stdout_path),
        "stderr_path": str(stderr_path.resolve()),
        "stderr_bytes": stderr_path.stat().st_size,
        "stderr_sha256": _sha256(stderr_path),
    }


def main() -> int:
    artifact_dir = ROOT / "artifacts" / "m4" / "m4-3-4" / "gate_evidence"
    log_dir = artifact_dir / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    records = {name: _run_gate(name, command, log_dir) for name, command in GATE_COMMANDS.items()}
    manifest = {
        "schema": "ocgforge.m4.m4_3_4_gate_evidence.v1",
        "repository_root": str(ROOT.resolve()),
        "gates": records,
        "all_exit_codes_zero": all(record["returncode"] == 0 for record in records.values()),
    }
    manifest_path = artifact_dir / "gate_evidence.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(manifest_path.resolve()), "all_exit_codes_zero": manifest["all_exit_codes_zero"]}))
    return 0 if manifest["all_exit_codes_zero"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
