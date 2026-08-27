#!/usr/bin/env python3
"""RED test for deterministic public-only selection policies in the probe."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()

    for policy in ("cycle", "hash"):
        completed = subprocess.run(
            [
                str(args.probe),
                "--seed",
                "2",
                "--starting-player",
                "0",
                "--max-actions",
                "1",
                "--selection-policy",
                policy,
                "--policy-salt",
                "17",
                "--stop-on-continuation",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            raise AssertionError(f"{policy}: {completed.stderr.strip()}")
        value = json.loads(completed.stdout)
        if not value["actions"] or value["frames"][0]["candidate_count"] == 0:
            raise AssertionError(f"{policy}: public probe did not execute a complete public selection")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
