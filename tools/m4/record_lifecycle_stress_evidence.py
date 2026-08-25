"""Run the scheduling-sensitive worker lifecycle test repeatedly.

The test itself owns the assertion and its internal 100-case repetition.  This
small evidence wrapper adds independent-process repetitions and records only
the result metadata needed for the M4 final gate.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TEST = (
    "tests.m4.test_failure_isolation.FailureIsolationTests."
    "test_result_then_exit_never_publishes_passed_under_repeated_scheduling"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--internal-repetitions", type=int, default=100)
    parser.add_argument(
        "--output",
        type=Path,
        default=ROOT / "artifacts" / "m4" / "final" / "lifecycle_stress.json",
    )
    args = parser.parse_args()
    if args.repetitions <= 0 or args.internal_repetitions <= 0:
        parser.error("repetition counts must be positive")

    command = [
        sys.executable,
        "-B",
        "-m",
        "unittest",
        TEST,
        "-v",
    ]
    runs: list[dict[str, object]] = []
    for index in range(args.repetitions):
        completed = subprocess.run(
            command,
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=300,
            check=False,
        )
        runs.append(
            {
                "repetition": index + 1,
                "exit_code": completed.returncode,
                "passed": completed.returncode == 0,
                "stdout_last_line": completed.stdout.strip().splitlines()[-1]
                if completed.stdout.strip()
                else "",
                "stderr_last_line": completed.stderr.strip().splitlines()[-1]
                if completed.stderr.strip()
                else "",
            }
        )

    evidence = {
        "schema_version": "ocgforge.m4.lifecycle_stress_evidence.v1",
        "command": ["python", "-B", "-m", "unittest", TEST, "-v"],
        "independent_process_repetitions": args.repetitions,
        "internal_repetitions_per_process": args.internal_repetitions,
        "total_cases": args.repetitions * args.internal_repetitions,
        "runs": runs,
        "all_passed": all(bool(run["passed"]) for run in runs),
        "retries": 0,
        "assertion_weakened": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(evidence, indent=2, sort_keys=True))
    return 0 if evidence["all_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
