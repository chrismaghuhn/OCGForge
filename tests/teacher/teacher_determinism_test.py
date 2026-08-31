from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run_probe(probe: Path) -> tuple[bytes, bytes]:
    result = subprocess.run(
        [str(probe), "--determinism-corpus"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    require(result.returncode == 0, result.stderr.decode("utf-8", errors="replace"))
    return result.stdout, result.stderr


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    probe = (ROOT / args.probe).resolve()
    require(probe.is_file(), f"Teacher probe is missing: {probe}")
    first_stdout, first_stderr = run_probe(probe)
    second_stdout, second_stderr = run_probe(probe)
    require(first_stdout == second_stdout, "independent Teacher outputs differ")
    require(first_stderr == second_stderr == b"", "determinism probe wrote diagnostics")
    output = first_stdout.decode("utf-8").replace("\r\n", "\n")
    require("determinism_corpus=PASS" in output,
            "determinism probe did not report PASS")
    for required in (
        "selected_score_vector=",
        "evaluation[0]\nkey=",
        "\nscore=",
        "completed_line_node_ids=",
        "public_resource_facts=",
    ):
        require(required in output, f"determinism corpus omitted {required}")
    print("teacher_determinism=PASS")


if __name__ == "__main__":
    main()
