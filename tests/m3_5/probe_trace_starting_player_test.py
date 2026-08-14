from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile


def read_trace(path: Path) -> tuple[dict, dict]:
    manifest = None
    summary = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            if line.startswith("# m3_summary="):
                summary = json.loads(line[len("# m3_summary="):])
            continue
        if manifest is None:
            manifest = json.loads(line)
    if manifest is None or summary is None:
        raise AssertionError(f"trace did not contain manifest and summary: {path}")
    return manifest, summary


def run_case(probe: Path, selected_player: int | None, expected_player: int, output: Path) -> None:
    command = [str(probe), "--m3-fixed-matchup", "--max-steps", "1", "--output", str(output)]
    if selected_player is not None:
        command.extend(("--starting-player", str(selected_player)))
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode != 0:
        raise AssertionError(
            f"probe failed for selected_player={selected_player}: "
            f"exit={completed.returncode}\nstdout={completed.stdout}\nstderr={completed.stderr}"
        )
    manifest, summary = read_trace(output)
    if manifest.get("starting_player") != expected_player:
        raise AssertionError(
            f"manifest recorded {manifest.get('starting_player')} instead of {expected_player} "
            f"for selected_player={selected_player}"
        )
    if summary.get("starting_player") != expected_player:
        raise AssertionError(
            f"summary recorded {summary.get('starting_player')} instead of {expected_player} "
            f"for selected_player={selected_player}"
        )


def main() -> int:
    probe = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="m35-starting-player-trace-") as directory:
        root = Path(directory)
        run_case(probe, None, 0, root / "default.jsonl")
        run_case(probe, 0, 0, root / "explicit-zero.jsonl")
        run_case(probe, 1, 1, root / "explicit-one.jsonl")
    print("m35_probe_trace_starting_player=pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
