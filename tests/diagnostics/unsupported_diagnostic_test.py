import argparse
import json
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=Path)
    args = parser.parse_args()

    result = subprocess.run(
        [str(args.probe), "--force-unsupported"],
        text=True,
        capture_output=True,
        check=False,
    )
    assert result.returncode == 3, result
    marker = "UNSUPPORTED_OR_MALFORMED_DECISION "
    line = next(line for line in result.stderr.splitlines() if line.startswith(marker))
    diagnostic = json.loads(line[len(marker) :])

    for field in (
        "message_type",
        "raw_message_sha256",
        "step_index",
        "player",
        "rules_bundle_id",
        "deck_hashes",
        "seed_bundle",
        "recent_trace_context",
    ):
        assert field in diagnostic, field
    assert len(diagnostic["deck_hashes"]) == 2
    assert len(diagnostic["seed_bundle"]) == 4
    assert isinstance(diagnostic["recent_trace_context"], list)
    assert result.stdout == ""
    print("unsupported_diagnostic=ok")


if __name__ == "__main__":
    main()
