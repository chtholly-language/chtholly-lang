# SPDX-License-Identifier: MIT OR Apache-2.0
"""Cancellation fixture: the parent owns the process runner's Job/group."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from process import run


if __name__ == "__main__":
    fixture, heartbeat = map(Path, sys.argv[1:])
    observed = run([str(fixture), "--tree", str(heartbeat)], heartbeat.parent, timeout=30)
    sys.exit(2 if observed.failure else 0)
