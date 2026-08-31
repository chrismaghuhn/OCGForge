import argparse
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    outputs = []
    for _ in range(2):
        completed = subprocess.run(
            [args.probe, "--snapshot-corpus"],
            check=False,
            capture_output=True,
        )
        if completed.returncode != 0:
            raise SystemExit(f"probe failed with exit code {completed.returncode}")
        if completed.stderr:
            raise SystemExit("snapshot probe wrote to stderr")
        outputs.append(completed.stdout)
    if outputs[0] != outputs[1]:
        raise SystemExit("snapshot probe output differed across fresh processes")
    required = (b"MODE=snapshot-corpus", b"SNAPSHOT_BYTES_HEX=")
    if any(marker not in outputs[0] for marker in required):
        raise SystemExit("snapshot corpus output omitted required public evidence")
    print("phase4c_battle_determinism=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
