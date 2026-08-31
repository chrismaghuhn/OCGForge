from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", default="build/dev-windows/teacher_probe.exe")
    args = parser.parse_args()
    probe = (ROOT / args.probe).resolve()
    require(probe.is_file(), f"Teacher probe is missing: {probe}")
    result = subprocess.run(
        [str(probe), "--paired-world"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    require(result.returncode == 0, result.stderr.decode("utf-8", errors="replace"))
    require(result.stderr == b"", "paired-world probe wrote diagnostics on success")
    output = result.stdout.decode("utf-8")
    require("paired_world=PASS" in output, "paired-world probe did not report PASS")
    print("teacher_paired_world=PASS")


if __name__ == "__main__":
    main()
