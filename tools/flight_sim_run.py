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


def load_best(path: Path) -> dict | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def gates_pass_count(result: dict) -> int:
    g = result.get("gates") or {}
    return sum(1 for v in g.values() if v)


def gates_stop_pass_count(result: dict) -> int:
    g = result.get("gates_stop") or {}
    return sum(1 for v in g.values() if v)


def is_better(result: dict, best: dict | None) -> bool:
    """True only when cruise gates improve vs baseline (never on first 4/8 run)."""
    rc = gates_pass_count(result)
    rsc = gates_stop_pass_count(result)
    if rc < 6 or rsc < 4:
        return False
    if best is None:
        return result.get("pass", False)
    bc = gates_pass_count(best)
    if rc > bc:
        return True
    if rc < bc:
        return False
    rsc = gates_stop_pass_count(result)
    bsc = gates_stop_pass_count(best)
    if rsc > bsc:
        return True
    if rsc < bsc:
        return False
    rm = result.get("metrics") or {}
    bm = best.get("metrics") or {}
    rp = rm.get("pending_light_focus_med")
    bp = bm.get("pending_light_focus_med")
    if rp is not None and bp is not None and rp < bp - 4.0:
        return True
    rw = rm.get("wall_ms_med")
    bw = bm.get("wall_ms_med")
    if rw is not None and bw is not None and rw < bw - 2.0:
        return True
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--no-fly", action="store_true")
    ap.add_argument(
        "--fly-stop",
        action="store_true",
        help="fly phase then release W for stop-recovery (AppRunner --fly-stop)",
    )
    ap.add_argument("--fly-phase-sec", type=float, default=50.0)
    ap.add_argument("--stop-phase-sec", type=float, default=50.0)
    ap.add_argument("--idle-sec", type=float, default=8.0)
    ap.add_argument(
        "--sprint",
        action="store_true",
        help="hold sprint during fly (covers more chunks like manual)",
    )
    ap.add_argument(
        "--visible",
        action="store_true",
        help="show GLFW window (default hidden; GL context still exists hidden)",
    )
    ap.add_argument(
        "--resume",
        action="store_true",
        default=True,
        help="resume from save position (default; manual flight)",
    )
    ap.add_argument(
        "--teleport-cruise",
        action="store_true",
        help="teleport to fixed cruise start chunk (legacy auto west)",
    )
    ap.add_argument("--report", type=Path, default=BIN / "flight_sim_gate_report.json")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build" / ("desktop-msvc" if sys.platform == "win32" else "desktop-linux"),
    )
    ap.add_argument(
        "--update-best",
        action="store_true",
        help="copy report to west_best / stop_best when improved",
    )
    ap.add_argument(
        "--replay-manual",
        action="store_true",
        help="replay the World_164 manual westbound stop-flight profile",
    )
    args = ap.parse_args()

    if args.replay_manual:
        args.world = "World_164"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = True
        args.sprint = False
        args.idle_sec = max(args.idle_sec, 45.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 90.0)

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

    if args.fly_stop:
        min_sec = args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0
        if args.seconds < min_sec:
            args.seconds = min_sec

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
    if args.fly_stop:
        sim_cmd.append("--fly-stop")
        sim_cmd.extend(["--fly-phase", str(args.fly_phase_sec)])
        sim_cmd.extend(["--stop-phase", str(args.stop_phase_sec)])
    sim_cmd.extend(["--idle", str(args.idle_sec)])
    if args.sprint:
        sim_cmd.append("--sprint")
    if args.visible:
        sim_cmd.append("--visible")
    if args.teleport_cruise:
        sim_cmd.append("--teleport-cruise")
    else:
        sim_cmd.append("--no-teleport-cruise")

    print("running:", " ".join(sim_cmd), flush=True)
    rc = subprocess.call(sim_cmd, cwd=str(BIN))
    perf = newest_perf(t0)
    if perf is None:
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
    analyze_cmd = [
        sys.executable,
        str(ANALYZE),
        str(perf),
        "--report",
        str(args.report),
    ]
    if args.replay_manual or args.fly_stop:
        analyze_cmd.append("--manual-idle")
    ana = subprocess.call(analyze_cmd)
    if args.report.is_file() and args.update_best:
        result = json.loads(args.report.read_text(encoding="utf-8"))
        if args.fly_stop:
            best_path = BIN / "flight_sim_gate_report_stop_best.json"
        else:
            best_path = BIN / "flight_sim_gate_report_west_best.json"
        best = load_best(best_path)
        if is_better(result, best):
            best_path.write_text(
                args.report.read_text(encoding="utf-8"), encoding="utf-8"
            )
            print(f"updated best: {best_path}", flush=True)
    if rc != 0:
        print(f"flight-sim process exit={rc}", file=sys.stderr)
        return rc
    return ana


if __name__ == "__main__":
    raise SystemExit(main())
