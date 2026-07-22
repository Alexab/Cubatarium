#!/usr/bin/env python3
"""Run cruise + optional fly-stop flight-sim and update best reports."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
RUN = Path(__file__).with_name("flight_sim_run.py")
CHECKPOINT = Path(__file__).with_name("flight_sim_checkpoint.py")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--fly-stop", action="store_true")
    ap.add_argument("--commit-label", default="", help="phase label for checkpoint commit")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build" / "desktop-linux",
    )
    args = ap.parse_args()

    build_args = ["--build"] if args.build else []
    base = [
        sys.executable,
        str(RUN),
        "--world",
        args.world,
        "--seconds",
        "60",
        "--update-best",
        "--report",
        str(BIN / "flight_sim_gate_report_west_latest.json"),
        "--build-dir",
        str(args.build_dir),
        "--process-timeout",
        "180",
        *build_args,
    ]
    print("=== cruise ===", flush=True)
    rc = subprocess.call(base)
    if rc != 0:
        return rc

    if args.fly_stop:
        stop_cmd = [
            sys.executable,
            str(RUN),
            "--world",
            args.world,
            "--fly-stop",
            "--update-best",
            "--report",
            str(BIN / "flight_sim_gate_report_stop_latest.json"),
            "--build-dir",
            str(args.build_dir),
            "--process-timeout",
            "300",
        ]
        print("=== fly-stop ===", flush=True)
        rc = subprocess.call(stop_cmd)
        if rc != 0:
            return rc

    if args.commit_label:
        ck = [
            sys.executable,
            str(CHECKPOINT),
            "--label",
            args.commit_label,
            "--report",
            str(BIN / "flight_sim_gate_report_west_latest.json"),
        ]
        subprocess.call(ck)

    west = BIN / "flight_sim_gate_report_west_latest.json"
    if west.is_file():
        data = json.loads(west.read_text(encoding="utf-8"))
        m = data.get("metrics") or {}
        g = data.get("gates") or {}
        passed = sum(1 for v in g.values() if v)
        print(
            f"summary: gates {passed}/{len(g)} "
            f"pending={m.get('pending_light_focus_med')} "
            f"holes={m.get('holes_rate')}",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
