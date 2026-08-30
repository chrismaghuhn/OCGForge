from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASSIGNMENT = "participant_policy_assignment.v1." + ("b" * 64)
CANDIDATES = ("effect_choice:7", "yes_no:0", "yes_no:1")


def fail(message: str) -> None:
    raise AssertionError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def default_probe() -> Path:
    candidates = (
        ROOT / "build" / "windows-zig" / "policy_random_legal_probe.exe",
        ROOT / "build" / "dev-windows-zig" / "policy_random_legal_probe.exe",
        ROOT / "build" / "windows-zig-clean2" / "policy_random_legal_probe.exe",
        ROOT / "build" / "release-windows-zig" / "policy_random_legal_probe.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    fail("policy_random_legal_probe executable is missing")


def run_probe(probe: Path, *, stream: str = "player0", episode_id: str = "0" * 64,
              repeat: int = 6, interleave_shadow: bool = False) -> tuple[bytes, dict]:
    command = [
        str(probe),
        "--root", "0x0123456789abcdef",
        "--assignment", ASSIGNMENT,
        "--stream", stream,
        "--episode-id", episode_id,
        "--repeat", str(repeat),
    ]
    for candidate in CANDIDATES:
        command.extend(("--candidate", candidate))
    if interleave_shadow:
        command.append("--interleave-shadow")
    result = subprocess.run(command, cwd=ROOT, capture_output=True, check=False)
    require(result.returncode == 0,
            f"policy probe failed with exit {result.returncode}: {result.stderr.decode()}")
    require(not result.stderr, "policy probe wrote diagnostics on a successful run")
    try:
        payload = json.loads(result.stdout.decode("utf-8"))
    except json.JSONDecodeError as error:
        fail(f"policy probe did not emit JSON: {error}")
    require(set(payload) == {"selections"},
            "policy probe emitted data outside selected keys and cursor spans")
    selections = payload["selections"]
    require(isinstance(selections, list) and len(selections) == repeat,
            "policy probe returned the wrong selection count")
    previous_post = None
    for index, selection in enumerate(selections):
        require(set(selection) == {"public_action_key", "pre_cursor", "post_cursor"},
                "policy probe emitted non-policy-visible selection data")
        require(isinstance(selection["public_action_key"], str) and selection["public_action_key"],
                "policy probe emitted an empty selected public key")
        require(isinstance(selection["pre_cursor"], int) and isinstance(selection["post_cursor"], int),
                "policy probe emitted a non-integer cursor span")
        require(selection["post_cursor"] > selection["pre_cursor"],
                "policy probe did not advance the cursor for a non-singleton domain")
        if index == 0:
            require(selection["pre_cursor"] == 0, "new policy did not start at cursor zero")
        else:
            require(selection["pre_cursor"] == previous_post,
                    "policy cursor spans were not contiguous")
        previous_post = selection["post_cursor"]
    return result.stdout, payload


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path)
    args = parser.parse_args()
    probe = (args.probe or default_probe()).resolve()
    require(probe.is_file(), f"policy probe executable is missing: {probe}")

    first_bytes, first = run_probe(probe)
    second_bytes, second = run_probe(probe)
    require(first_bytes == second_bytes and first == second,
            "independent policy probe processes produced different output")

    episode_bytes, episode = run_probe(probe, episode_id="1" * 64)
    require(episode_bytes == first_bytes and episode == first,
            "changing only environment episode identity changed policy output")

    interleaved_bytes, interleaved = run_probe(probe, interleave_shadow=True)
    require(interleaved_bytes == first_bytes and interleaved == first,
            "interleaving an isolated policy episode changed policy output")

    fresh_bytes, fresh = run_probe(probe, repeat=1)
    require(fresh["selections"][0] == first["selections"][0] and fresh_bytes != b"",
            "a newly constructed policy did not reproduce the isolated first decision")

    participant_bytes, participant = run_probe(probe, stream="player1")
    require(participant["selections"][0]["pre_cursor"] == 0,
            "the second participant policy did not start at cursor zero")
    require(participant_bytes != first_bytes,
            "distinct policy streams produced identical fixed probe output")

    print("policy_determinism=ok")


if __name__ == "__main__":
    main()
