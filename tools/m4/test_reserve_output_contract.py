"""Fail-closed contract check for the M4.3.5 reserve-output sidecar."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


PREFIX = "M4_PERFORMANCE_AUDIT "
FIELD = "future_m4_3_5_reserve_output"


def _load_sidecar(path: Path) -> dict[str, object]:
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith(PREFIX):
            value = json.loads(line[len(PREFIX) :])
            if isinstance(value, dict):
                return value
    raise AssertionError(f"{path} contains no {PREFIX.strip()} record")


def _require_uint(value: object, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise AssertionError(f"{label} must be a nonnegative integer")
    return value


def check(path: Path) -> dict[str, object]:
    sidecar = _load_sidecar(path)
    output = sidecar.get(FIELD)
    if not isinstance(output, dict):
        raise AssertionError(f"sidecar is missing object field {FIELD}")
    if output.get("mode") != "reserve_backed":
        raise AssertionError("reserve sidecar mode is not reserve_backed")
    calls = _require_uint(output.get("calls"), f"{FIELD}.calls")
    requested = _require_uint(output.get("requested_capacity"), f"{FIELD}.requested_capacity")
    final_bytes = _require_uint(output.get("final_bytes"), f"{FIELD}.final_bytes")
    final_capacity = _require_uint(output.get("final_capacity"), f"{FIELD}.final_capacity")
    growth = _require_uint(output.get("growth_events"), f"{FIELD}.growth_events")
    unused = _require_uint(output.get("unused_capacity"), f"{FIELD}.unused_capacity")
    if calls == 0 or requested == 0 or final_bytes == 0 or final_capacity == 0:
        raise AssertionError("reserve sidecar has no completed output")
    if final_bytes > final_capacity:
        raise AssertionError("final output is larger than its final capacity")
    if final_capacity < requested and growth == 0:
        raise AssertionError("output grew beyond its reserve without a growth event")
    if unused != final_capacity - final_bytes:
        raise AssertionError("unused capacity does not match final capacity/final bytes")
    return output


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sidecar", type=Path, required=True)
    args = parser.parse_args(arguments)
    try:
        output = check(args.sidecar)
    except (AssertionError, OSError, json.JSONDecodeError) as error:
        print(f"M4.3.5 reserve output contract failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"status": "PASS", "reserve_output": output}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
