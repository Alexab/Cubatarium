#!/usr/bin/env python3
"""Run one full-branch phase: build -> unit -> autofly -> gates.

Usage:
  python tools/gpu_full_branch_phase_run.py --phase-id D2a --report bin/phase_D2a.json
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str], allow_exit_codes: set[int] | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, cwd=ROOT)
    if allow_exit_codes is None:
        allow_exit_codes = {0}
    if rc not in allow_exit_codes:
        raise SystemExit(rc)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--phase-id", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--world", default="World_164")
    args = ap.parse_args()

    run(
        [
            "cmake",
            "--build",
            "build/desktop-msvc",
            "--config",
            "Release",
            "--target",
            "Cubatarium",
            "--parallel",
            "8",
        ]
    )
    # Use PowerShell executable available on host.
    run(["powershell", "-ExecutionPolicy", "Bypass", "-File", "tools/run_gpu_tail_unit_tests.ps1"])

    run(
        [
            "python",
            "tools/flight_sim_run.py",
            "--world",
            args.world,
            "--teleport-cruise",
            "--seconds",
            "130",
            "--fly-stop",
            "--fly-phase-sec",
            "45",
            "--stop-phase-sec",
            "60",
            "--idle-sec",
            "8",
            "--phase-id",
            args.phase_id,
            "--report",
            args.report,
        ],
        # flight_sim_run returns analyzer pass/fail code; gates below are the
        # authoritative phase pass decision.
        allow_exit_codes={0, 1},
    )
    run(["python", "tools/flight_sim_phase_gate.py", "--phase-id", "F2", "--report", args.report])
    run(
        [
            "python",
            "tools/flight_sim_phase_gate.py",
            "--phase-id",
            args.phase_id,
            "--report",
            args.report,
        ]
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

