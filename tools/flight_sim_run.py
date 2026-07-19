#!/usr/bin/env python3
"""Build (optional), run Cubatarium --flight-sim, analyze perf gates."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
EXE = BIN / "Cubatarium.exe"
ANALYZE = Path(__file__).with_name("flight_sim_analyze.py")


def newest_perf(after_ts: float) -> Path | None:
    logs = BIN / "logs"
    if not logs.is_dir():
        return None
    cands = [
        p
        for p in logs.glob("perf_*.jsonl")
        if p.stat().st_mtime >= after_ts - 1.0
    ]
    if not cands:
        return None
    return max(cands, key=lambda p: p.stat().st_mtime)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--no-fly", action="store_true")
    ap.add_argument("--report", type=Path, default=BIN / "flight_sim_gate_report.json")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build" / "desktop-linux",
    )
    args = ap.parse_args()

    if args.build:
        cmd = [
            "cmake",
            "--build",
            str(args.build_dir),
            "--parallel",
            "8",
            "-j7",
            "--target",
            "Cubatarium",
        ]
        print("building:", " ".join(cmd), flush=True)
        rc = subprocess.call(cmd)
        if rc != 0:
            return rc

    if not EXE.is_file():
        print(f"FAIL: missing {EXE}", file=sys.stderr)
        return 2

    t0 = time.time()
    sim_cmd = [
        str(EXE),
        "--flight-sim",
        "--world",
        args.world,
        "--seconds",
        str(args.seconds),
        "--report",
        str(BIN / "flight_sim_report.json"),
    ]
    if args.no_fly:
        sim_cmd.append("--no-fly")
        sim_cmd.append("--no-hold-forward")
    else:
        sim_cmd.extend(["--fly", "--hold-forward"])

    print("running:", " ".join(sim_cmd), flush=True)
    rc = subprocess.call(sim_cmd, cwd=str(BIN))
    perf = newest_perf(t0)
    if perf is None:
        # fallback: path from report
        report_path = BIN / "flight_sim_report.json"
        if report_path.is_file():
            data = json.loads(report_path.read_text(encoding="utf-8"))
            p = data.get("perf_jsonl") or ""
            if p and Path(p).is_file():
                perf = Path(p)
    if perf is None:
        print("FAIL: no perf jsonl produced", file=sys.stderr)
        return 1

    print(f"analyzing {perf}", flush=True)
    ana = subprocess.call(
        [
            sys.executable,
            str(ANALYZE),
            str(perf),
            "--report",
            str(args.report),
        ]
    )
    if rc != 0:
        print(f"flight-sim process exit={rc}", file=sys.stderr)
        return rc
    return ana


if __name__ == "__main__":
    raise SystemExit(main())
