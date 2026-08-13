#!/usr/bin/env python3
"""Fetch exact rule snapshots into the repository-local ignored cache."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.rules_bundle import BundleVerificationError, load_lock, verify_checkout


def run(command: list[str], cwd: Path | None = None) -> None:
    subprocess.run(command, cwd=cwd, check=True)


def fetch_source(source: dict[str, object], cache_root: Path) -> None:
    checkout = cache_root / str(source["cache_directory"])
    commit = str(source["commit"])
    repository = str(source["repository"])
    if checkout.exists():
        raise BundleVerificationError(
            f"cache checkout already exists; refusing to overwrite: {checkout}"
        )
    checkout.parent.mkdir(parents=True, exist_ok=True)
    run(["git", "clone", "--filter=blob:none", "--no-checkout", repository, str(checkout)])
    run(["git", "checkout", "--detach", commit], cwd=checkout)
    if source.get("recursive_submodules"):
        run(["git", "submodule", "update", "--init", "--recursive"], cwd=checkout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    try:
        lock = load_lock(args.lock)
        args.cache.mkdir(parents=True, exist_ok=True)
        for source in lock["sources"].values():
            checkout = args.cache / str(source["cache_directory"])
            if not checkout.exists():
                fetch_source(source, args.cache)
        verify_checkout(lock, args.cache)
    except (BundleVerificationError, subprocess.CalledProcessError, OSError) as exc:
        print(f"rules bundle fetch failed: {exc}", file=sys.stderr)
        return 2
    print(lock["bundle_id"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
