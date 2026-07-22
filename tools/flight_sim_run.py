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
PHASE_HISTORY = BIN / "flight_sim_phase_history.jsonl"


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


def kill_cubatarium_orphans() -> int:
    """Force-kill any Cubatarium.exe trees. Returns number of taskkill attempts."""
    if sys.platform != "win32":
        return 0
    r = subprocess.run(
        ["taskkill", "/F", "/T", "/IM", "Cubatarium.exe"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return 0 if r.returncode == 128 else 1


def exe_writable(timeout_sec: float = 5.0) -> bool:
    """True if bin/Cubatarium.exe can be replaced (not locked)."""
    if not EXE.is_file():
        return True
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            with open(EXE, "ab"):
                return True
        except OSError:
            time.sleep(0.25)
    return False


def preflight_cleanup() -> None:
    kill_cubatarium_orphans()
    time.sleep(0.3)
    kill_cubatarium_orphans()
    if not exe_writable(5.0):
        raise SystemExit(
            "FAIL: Cubatarium.exe still locked after kill — abort before build/sim"
        )


def kill_process_tree(pid: int) -> None:
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        try:
            import os
            import signal

            os.killpg(os.getpgid(pid), signal.SIGKILL)
        except (ProcessLookupError, PermissionError, OSError):
            pass


def run_with_timeout(cmd: list[str], cwd: Path, timeout_sec: float) -> int:
    proc = subprocess.Popen(cmd, cwd=str(cwd))
    try:
        return proc.wait(timeout=timeout_sec)
    except subprocess.TimeoutExpired:
        print(
            f"WARN: flight-sim hung after {timeout_sec:.0f}s, killing pid={proc.pid}",
            flush=True,
        )
        kill_process_tree(proc.pid)
        kill_cubatarium_orphans()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        return 124


def append_phase_history(entry: dict) -> None:
    BIN.mkdir(parents=True, exist_ok=True)
    with PHASE_HISTORY.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")


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


def annotate_report_hang(report: Path, hang_killed: bool, process_rc: int) -> None:
    if not report.is_file():
        return
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return
    data["hang_killed"] = hang_killed
    data["process_rc"] = process_rc
    if hang_killed:
        data["pass"] = False
    report.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


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
        help="replay World_164 manual profile: resume save pos (no teleport), "
        "level pitch, hold-space altitude, fly-stop",
    )
    ap.add_argument(
        "--hold-space",
        action="store_true",
        help="hold Space while flying (climb / maintain altitude)",
    )
    ap.add_argument(
        "--pitch",
        type=float,
        default=None,
        help="autopilot pitch degrees (default: exe -2, replay-manual 0)",
    )
    ap.add_argument(
        "--process-timeout",
        type=float,
        default=0.0,
        help="max wall seconds for Cubatarium (0 = seconds + 120 grace)",
    )
    ap.add_argument(
        "--phase-id",
        default="",
        help="optional label written to flight_sim_phase_history.jsonl",
    )
    ap.add_argument(
        "--skip-preflight",
        action="store_true",
        help="do not kill orphan Cubatarium before run (debug only)",
    )
    args = ap.parse_args()

    if args.replay_manual:
        args.world = "World_164"
        args.fly_stop = True
        args.resume = True
        # Resume save focus (manual 190126 ~-484) — do NOT teleport to (-47,5).
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        args.idle_sec = max(args.idle_sec, 45.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 90.0)

    if not args.skip_preflight:
        print("preflight: killing orphan Cubatarium.exe (if any)", flush=True)
        preflight_cleanup()

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
        if not args.skip_preflight:
            preflight_cleanup()
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
    if args.hold_space:
        sim_cmd.append("--hold-space")
    if args.pitch is not None:
        sim_cmd.extend(["--pitch", str(args.pitch)])
    if args.visible:
        sim_cmd.append("--visible")
    if args.teleport_cruise:
        sim_cmd.append("--teleport-cruise")
    else:
        sim_cmd.append("--no-teleport-cruise")

    process_timeout = args.process_timeout
    if process_timeout <= 0.0:
        process_timeout = args.seconds + 120.0
    if args.fly_stop:
        process_timeout = max(process_timeout, 420.0)

    print("running:", " ".join(sim_cmd), flush=True)
    rc = run_with_timeout(sim_cmd, BIN, process_timeout)
    hang_killed = rc == 124
    kill_cubatarium_orphans()

    perf = newest_perf(t0)
    if perf is None:
        report_path = BIN / "flight_sim_report.json"
        if report_path.is_file():
            data = json.loads(report_path.read_text(encoding="utf-8"))
            p = data.get("perf_jsonl") or ""
            if p and Path(p).is_file():
                perf = Path(p)

    ana = 1
    if perf is None:
        print("FAIL: no perf jsonl produced", file=sys.stderr)
        append_phase_history(
            {
                "phase": args.phase_id or "unspecified",
                "rc": rc,
                "hang_killed": hang_killed,
                "perf": None,
                "report": str(args.report),
            }
        )
        return 3 if hang_killed else 1

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
    annotate_report_hang(args.report, hang_killed, rc)

    metrics_summary: dict = {}
    if args.report.is_file():
        try:
            result = json.loads(args.report.read_text(encoding="utf-8"))
            metrics_summary = {
                "pass": result.get("pass"),
                "hang_killed": result.get("hang_killed"),
                "gates_pass_count": gates_pass_count(result),
                "gates_stop_pass_count": gates_stop_pass_count(result),
                "metrics": {
                    k: (result.get("metrics") or {}).get(k)
                    for k in (
                        "pending_light_focus_med",
                        "post_stop_pending_med",
                        "post_stop_not_ready_end",
                        "stop_not_ready_delta",
                        "post_stop_black_sticky_max",
                        "stop_wall_med",
                        "chunks_traveled",
                    )
                },
            }
            if args.update_best and not hang_killed:
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
        except (json.JSONDecodeError, OSError):
            pass

    append_phase_history(
        {
            "phase": args.phase_id or "unspecified",
            "rc": rc,
            "ana": ana,
            "hang_killed": hang_killed,
            "perf": str(perf),
            "report": str(args.report),
            "summary": metrics_summary,
        }
    )

    if hang_killed:
        print("flight-sim process HANG-KILLED exit=124", file=sys.stderr)
        return 3
    if rc != 0:
        print(f"flight-sim process exit={rc}", file=sys.stderr)
        return rc
    return ana


if __name__ == "__main__":
    raise SystemExit(main())
