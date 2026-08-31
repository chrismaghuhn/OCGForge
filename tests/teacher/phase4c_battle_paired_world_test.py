import argparse
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    completed = subprocess.run(
        [args.probe, "--paired-world"],
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise SystemExit(f"paired-world probe failed with exit code {completed.returncode}")
    if completed.stderr:
        raise SystemExit("paired-world probe wrote to stderr")
    required = (
        b"PUBLIC_OBSERVATION_EQUAL=PASS",
        b"PUBLIC_CANDIDATES_EQUAL=PASS",
        b"SNAPSHOT_EQUAL=PASS",
        b"LETHAL_EQUAL=PASS",
        b"HIDDEN_VALUES_IN_OUTPUT=NONE",
    )
    missing = [marker.decode() for marker in required if marker not in completed.stdout]
    if missing:
        raise SystemExit(f"paired-world probe omitted evidence: {missing}")
    print("phase4c_battle_paired_world=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
