#!/usr/bin/env python3
"""Run GPU visual regression autofly phases V_BLUE / V_DIG / V_FLICKER / V_EDGE.

Usage:
  python tools/visual_gpu_phase_run.py --phase-id V_BLUE --report bin/phase_V_BLUE.json
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SCENARIO = {
    "V_BLUE": "visual-blue",
    "V_DIG": "visual-dig",
    "V_FLICKER": "visual-flicker",
    "V_EDGE": "visual-edge",
}


def run(cmd: list[str], allow_exit_codes: set[int] | None = None) -> int:
    print("+", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, cwd=ROOT)
    if allow_exit_codes is None:
        allow_exit_codes = {0}
    if rc not in allow_exit_codes:
        raise SystemExit(rc)
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--phase-id", required=True, choices=sorted(SCENARIO))
    ap.add_argument("--report", required=True)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()

    if not args.skip_build:
        run(
            [
                "cmake",
                "--build",
                "build/desktop-msvc",
                "--config",
                "Release",
                "--target",
                "Cubatarium",
                "edit_mesh_remesh_policy_test",
                "--parallel",
                "8",
            ]
        )
        run([str(ROOT / "build" / "desktop-msvc" / "Release" / "edit_mesh_remesh_policy_test.exe")])

    scenario = SCENARIO[args.phase_id]
    run(
        [
            "python",
            "tools/flight_sim_run.py",
            "--world",
            args.world,
            "--scenario",
            scenario,
            "--phase-id",
            args.phase_id,
            "--report",
            args.report,
        ],
        allow_exit_codes={0, 1},
    )
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
