"""Thin command-line entry point for the Task-4B smoke runner."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

from . import task4b_runner


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = task4b_runner.run_task4b_smoke(
            build_dir=args.build_dir,
            output_dir=args.output_dir,
        )
    except task4b_runner.Task4BSmokeError as error:
        print(error.report_json, file=sys.stderr)
        return 1
    print(result.report_json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
