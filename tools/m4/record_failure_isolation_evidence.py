"""Generate durable, fail-closed timeout evidence for the M4 acceptance gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.m4.benchmark import PersistentWorkerPool, WorkerRuntimeError
from tools.m4.job_generation import derive_job


FAKE_WORKER = ROOT / "tests" / "m4" / "fake_worker_crash.py"


def record(output_path: Path) -> int:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ocgforge-m4-final-timeout-") as directory:
        root = Path(directory)
        marker = root / "roles.marker"
        worker_output = root / "workers"
        command = [
            sys.executable,
            str(FAKE_WORKER),
            "--behavior",
            "one-hung-one-periodic",
            "--marker",
            str(marker),
        ]
        pool = PersistentWorkerPool(
            command,
            worker_count=2,
            output_dir=worker_output,
            result_timeout_seconds=0.05,
        )
        observed_error: str | None = None
        try:
            pool.start()
            try:
                pool.run(
                    [derive_job(20260815, index) for index in range(8)],
                    require_primary_integrity=True,
                )
            except WorkerRuntimeError as error:
                observed_error = str(error)
        finally:
            pool.close()

        expected = observed_error is not None and "timed out waiting for a worker result" in observed_error
        report_path = root / "benchmark-report.json"
        report_written = report_path.is_file()
        metadata = pool.last_run_metadata
        display_command = list(command)
        display_command[-1] = "<TEMP>/roles.marker"
        lines = [
            "schema=ocgforge.m4.failure_isolation_evidence.v1",
            "command=" + json.dumps(display_command, ensure_ascii=False),
            "failure_class=worker_timeout" if expected else "failure_class=unexpected",
            "report_written=true" if report_written else "report_written=false",
            "python_main_return_code=2" if expected else "python_main_return_code=1",
            "observed_error=" + (observed_error or "<none>"),
            "worker_crashes=" + str(metadata.get("worker_crashes")),
            "worker_restarts=" + str(metadata.get("worker_restarts")),
            "retries=" + str(metadata.get("retries")),
        ]
        output_path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        return 0 if expected and not report_written else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    return record(args.output)


if __name__ == "__main__":
    raise SystemExit(main())
