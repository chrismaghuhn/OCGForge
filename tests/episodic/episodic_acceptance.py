#!/usr/bin/env python3
"""Repository entry point for the explicit Episodic V2 acceptance runner."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.episodic.acceptance import main


if __name__ == "__main__":
    raise SystemExit(main())
