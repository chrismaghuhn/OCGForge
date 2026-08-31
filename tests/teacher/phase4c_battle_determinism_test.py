import argparse
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    args = parser.parse_args()
    for mode, required in (
        ("--snapshot-corpus", (b"MODE=snapshot-corpus", b"SNAPSHOT_BYTES_HEX=")),
        ("--lethal-corpus", (b"MODE=lethal-corpus", b"LETHAL_BYTES_HEX=")),
    ):
        outputs = []
        for _ in range(2):
            completed = subprocess.run(
                [args.probe, mode],
                check=False,
                capture_output=True,
            )
            if completed.returncode != 0:
                raise SystemExit(
                    f"{mode} probe failed with exit code {completed.returncode}"
                )
            if completed.stderr:
                raise SystemExit(f"{mode} probe wrote to stderr")
            outputs.append(completed.stdout)
        if outputs[0] != outputs[1]:
            raise SystemExit(f"{mode} output differed across fresh processes")
        if any(marker not in outputs[0] for marker in required):
            raise SystemExit(f"{mode} output omitted required public evidence")
    print("phase4c_battle_determinism=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
