#!/usr/bin/env python3
"""Run SimpleAB suite off/on for CUBA_STREAM_SIMPLE (perf-root P3)."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(phase: str, simple: bool) -> int:
    env = os.environ.copy()
    if simple:
        env["CUBA_STREAM_SIMPLE"] = "1"
    else:
        env.pop("CUBA_STREAM_SIMPLE", None)
    cmd = [
        sys.executable,
        str(ROOT / "tools" / "flight_sim_suite.py"),
        "--only",
        "land-stand",
        "land-cruise",
        "fz-cold-enter",
        "--phase-id",
        phase,
    ]
    print("+", " ".join(cmd), "CUBA_STREAM_SIMPLE=", env.get("CUBA_STREAM_SIMPLE", "0"))
    return subprocess.call(cmd, cwd=str(ROOT), env=env)


def main() -> int:
    rc_off = run("SimpleAB-off", False)
    rc_on = run("SimpleAB-on", True)
    print(f"SimpleAB-off exit={rc_off} SimpleAB-on exit={rc_on}")
    print("Fill bin/research_perf_root/03_policy_strip.md from suite_reports.")
    return 0 if rc_off == 0 else rc_off


if __name__ == "__main__":
    sys.exit(main())
