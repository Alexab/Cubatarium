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
DIAG = Path(__file__).with_name("flight_sim_diag.py")
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
    """Backward-compatible wrapper; prefer annotate_report_run."""
    annotate_report_run(report, hang_killed, process_rc, perf_jsonl=None)


def annotate_report_run(
    report: Path,
    hang_killed: bool,
    process_rc: int,
    perf_jsonl: Path | None,
    info_log: Path | None = None,
) -> None:
    if not report.is_file():
        return
    if DIAG.is_file():
        import importlib.util

        spec = importlib.util.spec_from_file_location("flight_sim_diag", DIAG)
        if spec and spec.loader:
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            mod.annotate_report_run(
                report, process_rc, hang_killed, perf_jsonl, info_log
            )
            return
    annotate_report_hang_legacy(report, hang_killed, process_rc)


def annotate_report_hang_legacy(report: Path, hang_killed: bool, process_rc: int) -> None:
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
        "--warmup-sec",
        type=float,
        default=5.0,
        help="analyze skip of early periods (land-cruise raises to ≥20)",
    )
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
        "--replay-edge",
        action="store_true",
        help="replay World_164 edge autofly route (-47,5): teleport-cruise + fly-stop",
    )
    ap.add_argument(
        "--land-cruise",
        action="store_true",
        help="inland land cruise (manual corridor ~-485,50): teleport + hold-space "
        "+ cruise-eye-y + fly-stop; analyze --manual-idle",
    )
    ap.add_argument(
        "--land-stand",
        action="store_true",
        help="inland land stand (manual 170154 forever-hole): short east fly then "
        "stop≥60s on one chunk; ARCH_D3_LAND miss_end/stale",
    )
    ap.add_argument(
        "--land-south",
        action="store_true",
        help="inland −Z stand (manual 190350): from (-483,54) yaw 270 short fly "
        "then stop≥60s; residual stale/void blacks east/north autofly miss",
    )
    ap.add_argument(
        "--land-south-short",
        action="store_true",
        help="manual 190350 mid-heal repro: same −Z corridor as land-south but "
        "stop≈10s (catches stale/void before long heal)",
    )
    ap.add_argument(
        "--cruise-cx",
        type=float,
        default=None,
        help="teleport cruise start chunk X (default: ocean -47 or land -485)",
    )
    ap.add_argument(
        "--cruise-cz",
        type=float,
        default=None,
        help="teleport cruise start chunk Z (default: ocean 5 or land 50)",
    )
    ap.add_argument(
        "--cruise-eye-y",
        type=float,
        default=None,
        help="absolute eye Y for land cruise (overrides sea+alt when set)",
    )
    ap.add_argument(
        "--yaw",
        type=float,
        default=None,
        help="autopilot yaw degrees (default: exe 180 west)",
    )
    ap.add_argument(
        "--hold-space",
        action="store_true",
        help="hold Space while flying (climb / maintain altitude)",
    )
    ap.add_argument(
        "--min-alt-above-sea",
        type=float,
        default=None,
        help="AppRunner MinAltitudeAboveSea (ocean void telem needs ≤~12 so "
        "DarkFaceVoidNearN sphere 24m sees sea faces; default exe 28)",
    )
    ap.add_argument(
        "--pitch",
        type=float,
        default=None,
        help="autopilot pitch degrees (default: exe -2, replay-manual/land 0)",
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
    ap.add_argument(
        "--scenario",
        default="",
        choices=[
            "",
            "break-stand",
            "visual-blue",
            "visual-dig",
            "visual-flicker",
            "visual-edge",
            "land-cruise",
            "land-cruise-resume",
            "land-stand",
            "land-south",
            "land-south-short",
            "idle-clean",
            "idle-warm",
            "idle-edit-smoke",
            "fly-clean",
            "ocean-cruise",
            "ocean-cruise-enter",
            "ocean-cruise-stress",
            "ocean-cruise-short",
        ],
        help="named scenario (break-stand / visual-* / land-* / idle-* / fly-clean / ocean-cruise*)",
    )
    ap.add_argument("--break-phase-sec", type=float, default=20.0)
    ap.add_argument("--break-interval-sec", type=float, default=1.0)
    ap.add_argument("--yaw-sweep-sec", type=float, default=3.0)
    ap.add_argument(
        "--repeat",
        type=int,
        default=1,
        help="run scenario N times; write report, report_2..N, and report_agg.json",
    )
    args = ap.parse_args()

    if args.scenario == "break-stand":
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 55.0
        args.idle_sec = max(args.idle_sec, 8.0)
        min_break = args.idle_sec + args.break_phase_sec + 5.0
        if args.seconds < min_break:
            args.seconds = min_break

    if args.scenario == "visual-blue":
        # Resume near-sea World_164 focus; yaw sweep 0/90/180/270.
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 0.0
        args.idle_sec = max(args.idle_sec, 5.0)
        min_blue = args.idle_sec + args.yaw_sweep_sec * 4.0 + 5.0
        if args.seconds < min_blue:
            args.seconds = min_blue

    if args.scenario == "visual-dig":
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 55.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.break_phase_sec = max(args.break_phase_sec, 20.0)
        min_dig = args.idle_sec + args.break_phase_sec + 5.0
        if args.seconds < min_dig:
            args.seconds = min_dig

    if args.scenario == "visual-flicker":
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.teleport_cruise = True
        args.resume = False
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 30.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 20.0)

    if args.scenario == "visual-edge":
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.teleport_cruise = True
        args.resume = False
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 30.0)

    if args.scenario == "land-cruise":
        args.land_cruise = True
    if args.scenario == "land-cruise-resume":
        args.land_cruise_resume = True
    if args.scenario == "land-stand":
        args.land_stand = True
    if args.scenario == "land-south":
        args.land_south = True
    if args.scenario == "land-south-short":
        args.land_south_short = True

    if args.scenario == "idle-clean":
        # Clean idle perf: land −Z corridor, short fly, long stand (≥60s), no edit.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 20.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 60.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "idle-warm":
        # Debtful stand near manual focus (-482,72): longer fly accumulates remesh.
        args.world = args.world or "World_164"
        args.fly_stop = True
        if "--resume" in sys.argv:
            args.resume = True
            args.teleport_cruise = False
        else:
            args.resume = False
            args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            # South over land (same as land-cruise): yaw 270 gave opaque~4/blue.
            args.yaw = 90.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 40.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 40.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 60.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "idle-edit-smoke":
        # Stand + forced dig pulse for control-lag / physics_block regression.
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 55.0
        args.idle_sec = max(args.idle_sec, 15.0)
        args.break_phase_sec = max(args.break_phase_sec, 8.0)
        args.break_interval_sec = min(args.break_interval_sec, 1.0)
        # Idle → edit → post-edit stand (~30s) inside break window + tail.
        min_edit = args.idle_sec + args.break_phase_sec + 30.0 + 5.0
        if args.seconds < min_edit:
            args.seconds = min_edit
        args.warmup_sec = max(args.warmup_sec, 8.0)

    if args.scenario == "fly-clean":
        # Moving cruise stress: fly ≥40s; judge move-segment sync/wall, not stop.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 40.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 40.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 20.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 15.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    def _apply_ocean_cruise_base():
        # Era31 void-debt parity: DarkFaceVoidNearN is a 24m sphere — sea+28 +
        # HoldSpace climb made autofly blind (void_max=0) while manual saw 774+.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.sprint = False
        args.hold_space = False
        if args.min_alt_above_sea is None:
            args.min_alt_above_sea = 10.0
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 180.0
        # Manual SoT corridor 122032/153653 (−550…−555, ~110), not (−525,100).
        if args.cruise_cx is None:
            args.cruise_cx = -550.0
        if args.cruise_cz is None:
            args.cruise_cz = 110.0

    if args.scenario == "ocean-cruise":
        # Ocean west cruise FillWater horizon heal stress (void/VB/fluid).
        # No cruise_eye_y — AppRunner sea+min_alt clamp.
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = True
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 65.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 60.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "ocean-cruise-enter":
        # Full enter path (no teleport) — reproduces manual residency buildup.
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = False
        args.idle_sec = max(args.idle_sec, 45.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 65.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 60.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "ocean-cruise-stress":
        # Cold teleport + short idle + sprint — void/holes parity with manual.
        # (Warm resume + idle≥12 + sea+28 hid void; OCEAN_CRUISE_STRESS DoD.)
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = True
        args.sprint = True
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 3.0
        else:
            args.idle_sec = max(args.idle_sec, 3.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 90.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 75.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        # Keep early void peak in fly segment (warmup 16 ate period 0–1).
        if "--warmup-sec" not in sys.argv:
            args.warmup_sec = 8.0
        else:
            args.warmup_sec = min(args.warmup_sec, 8.0)

    if args.scenario == "ocean-cruise-short":
        # Stop-debt snapshot (land_south_short lesson): shorter idle keeps void.
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = True
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 3.0
        else:
            args.idle_sec = max(args.idle_sec, 3.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 65.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 60.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

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

    if args.land_cruise:
        # Inland corridor matching manual 084551…142306 (not ocean -47,5).
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            # South over land (L2): opaque_med~700. West (180) at eye-y 96
            # often sparse/blue_screen (L1/L3/L4 opaque_med~2–4).
            args.yaw = 90.0
        if args.cruise_cx is None:
            args.cruise_cx = -485.0
        if args.cruise_cz is None:
            args.cruise_cz = 50.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 45.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        # Skip cold-spawn miss in analyze (land_fix_P1e: miss=1 for ~12s at
        # teleport). Do not raise idle — longer idle raised wall/dirty (P1f).
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if getattr(args, "land_cruise_resume", False):
        # Manual/autofly parity: World_174 spawn corridor, resume save pos,
        # cooperative enter — no teleport (Era37 P3).
        args.world = args.world or "World_174"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 90.0
        if args.cruise_cx is None:
            args.cruise_cx = 2.0
        if args.cruise_cz is None:
            args.cruise_cz = -10.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 64.0
        args.idle_sec = max(args.idle_sec, 12.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 45.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.land_stand:
        # Forever-hole repro (manual 170154): short east fly then stand ≥60s.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            # East (−491→−484 in manual 170154).
            args.yaw = 0.0
        if args.cruise_cx is None:
            args.cruise_cx = -485.0
        if args.cruise_cz is None:
            args.cruise_cz = 50.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.land_south or args.land_south_short:
        # Manual 190350: (−483,54)→(−485,47) (−Z). Autofly yaw 90 = +Z (L2
        # "south"); yaw 270 = −Z to match that corridor / residual blacks.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        # Argparse default fly-phase=50 stretches past the manual stand chunk;
        # keep short unless user overrode --fly-phase-sec.
        if "--fly-phase-sec" not in sys.argv:
            # short: ≥25s so chunks_traveled≥3 (fly20 sometimes only 2).
            args.fly_phase_sec = 25.0 if args.land_south_short else 20.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        if args.land_south_short:
            # Mid-heal snapshot (~manual 190350 ~8s stop). Shorter idle so
            # emerge/void debt still visible at stop (teleport+idle8 was too clean).
            if "--idle-sec" not in sys.argv:
                args.idle_sec = 3.0
            if "--stop-phase-sec" not in sys.argv:
                args.stop_phase_sec = 10.0
            else:
                args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.replay_edge:
        args.world = "World_164"
        args.fly_stop = True
        args.teleport_cruise = True
        args.resume = False
        args.sprint = False
        args.hold_space = False
        if args.pitch is None:
            args.pitch = -2.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )

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

    repeats = max(1, int(args.repeat or 1))
    base_report = args.report
    last_rc = 0
    run_reports: list[Path] = []

    for rep in range(1, repeats + 1):
        if repeats > 1:
            if rep == 1:
                report_path = base_report
            else:
                stem = base_report.stem
                report_path = base_report.with_name(f"{stem}_{rep}{base_report.suffix}")
            args.report = report_path
            print(f"=== repeat {rep}/{repeats} → {report_path} ===", flush=True)
        else:
            report_path = base_report

        t0 = time.time()
        if not args.skip_preflight:
            kill_cubatarium_orphans()

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
        break_scenarios = ("break-stand", "visual-dig", "idle-edit-smoke")
        if args.scenario in break_scenarios:
            sim_cmd.append("--break-stand")
            sim_cmd.extend(["--break-phase", str(args.break_phase_sec)])
            sim_cmd.extend(["--break-interval", str(args.break_interval_sec)])
            sim_cmd.append("--no-fly")
            sim_cmd.append("--no-hold-forward")
        elif args.scenario == "visual-blue":
            sim_cmd.append("--yaw-sweep")
            sim_cmd.extend(["--yaw-sweep-sec", str(args.yaw_sweep_sec)])
            sim_cmd.append("--no-fly")
            sim_cmd.append("--no-hold-forward")
        elif args.no_fly:
            sim_cmd.append("--no-fly")
            sim_cmd.append("--no-hold-forward")
        else:
            sim_cmd.extend(["--fly", "--hold-forward"])
        if args.fly_stop and args.scenario not in (
            "break-stand",
            "visual-dig",
            "visual-blue",
            "idle-edit-smoke",
        ):
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
        if args.yaw is not None:
            sim_cmd.extend(["--yaw", str(args.yaw)])
        if args.visible:
            sim_cmd.append("--visible")
        if args.teleport_cruise:
            sim_cmd.append("--teleport-cruise")
        else:
            sim_cmd.append("--no-teleport-cruise")
        if args.cruise_cx is not None:
            sim_cmd.extend(["--cruise-cx", str(args.cruise_cx)])
        if args.cruise_cz is not None:
            sim_cmd.extend(["--cruise-cz", str(args.cruise_cz)])
        if args.cruise_eye_y is not None:
            sim_cmd.extend(["--cruise-eye-y", str(args.cruise_eye_y)])
        if args.min_alt_above_sea is not None:
            sim_cmd.extend(["--min-alt-above-sea", str(args.min_alt_above_sea)])

        process_timeout = args.process_timeout
        if process_timeout <= 0.0:
            process_timeout = args.seconds + 120.0
        if args.fly_stop:
            process_timeout = max(process_timeout, 420.0)
        if args.scenario in (
            "break-stand",
            "visual-dig",
            "visual-blue",
            "idle-edit-smoke",
            "ocean-cruise",
            "ocean-cruise-enter",
            "ocean-cruise-stress",
            "ocean-cruise-short",
        ):
            process_timeout = max(process_timeout, args.seconds + 180.0)

        print("running:", " ".join(sim_cmd), flush=True)
        rc = run_with_timeout(sim_cmd, BIN, process_timeout)
        hang_killed = rc == 124
        kill_cubatarium_orphans()

        perf = newest_perf(t0)
        if perf is None:
            flight_report = BIN / "flight_sim_report.json"
            if flight_report.is_file():
                data = json.loads(flight_report.read_text(encoding="utf-8"))
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
                    "report": str(report_path),
                    "repeat": rep,
                }
            )
            last_rc = 3 if hang_killed else 1
            if repeats == 1:
                return last_rc
            continue

        print(f"analyzing {perf}", flush=True)
        analyze_cmd = [
            sys.executable,
            str(ANALYZE),
            str(perf),
            "--report",
            str(report_path),
        ]
        if (
            args.replay_manual
            or args.fly_stop
            or args.land_cruise
            or args.land_stand
            or args.land_south
            or args.land_south_short
            or args.scenario
            in ("idle-clean", "idle-warm", "idle-edit-smoke", "fly-clean", "ocean-cruise")
            or (args.scenario or "").startswith("ocean-cruise")
        ):
            analyze_cmd.append("--manual-idle")
        if getattr(args, "warmup_sec", None) is not None:
            analyze_cmd.extend(["--warmup-sec", str(args.warmup_sec)])
        ana = subprocess.call(analyze_cmd)
        info_log = None
        if DIAG.is_file():
            import importlib.util

            spec = importlib.util.spec_from_file_location("flight_sim_diag", DIAG)
            if spec and spec.loader:
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                info_log = mod.newest_info_log(t0)
        annotate_report_run(report_path, hang_killed, rc, perf, info_log)

        metrics_summary: dict = {}
        if report_path.is_file():
            run_reports.append(report_path)
            try:
                result = json.loads(report_path.read_text(encoding="utf-8"))
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
                            "calm_stop_wall_med",
                            "calm_stop_emerge_med",
                            "calm_stop_stream_med",
                            "stop_mesh_prep_med",
                            "chunks_traveled",
                            "dominant_spike_class",
                            "dominant_heavy_spike_class",
                            "spike_max_world_extra",
                            "spike_world_extra_dominant_rate",
                            "break_complete_sum",
                            "break_inflight_race_sum",
                            "break_dark_face_sum",
                            "wall_ms_med",
                            "wall_ms_fly_med",
                            "tick_env_fly_max",
                            "world_extra_fly_max",
                        )
                    },
                    "soft": {
                        k: (result.get("soft") or {}).get(k)
                        for k in (
                            "dominant_spike_class",
                            "dominant_heavy_spike_class",
                            "soft_world_extra_ok",
                            "spike_bucket_counts",
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
                            report_path.read_text(encoding="utf-8"), encoding="utf-8"
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
                "report": str(report_path),
                "repeat": rep,
                "summary": metrics_summary,
            }
        )

        if hang_killed:
            print("flight-sim process HANG-KILLED exit=124", file=sys.stderr)
            last_rc = 3
        elif rc != 0:
            print(f"flight-sim process exit={rc}", file=sys.stderr)
            last_rc = rc
        else:
            last_rc = ana

    if repeats > 1 and run_reports:
        agg_path = base_report.with_name(f"{base_report.stem}_agg{base_report.suffix}")
        write_repeat_aggregate(run_reports, agg_path)
        print(f"wrote aggregate: {agg_path}", flush=True)

    return last_rc


AGG_METRIC_KEYS = (
    "calm_stop_wall_med",
    "calm_stop_emerge_med",
    "calm_stop_stream_med",
    "calm_stop_phys_med",
    "stop_mesh_prep_med",
    "stop_wall_med",
    "wall_ms_med",
    "wall_ms_fly_med",
    "dirty_med",
    "post_stop_focus_dirty_med",
    "post_stop_black_sticky_max",
    "post_stop_missing_max",
    "physics_block_ms_p95",
    "chunks_traveled",
    "opaque_cmd_on_med",
)


def write_repeat_aggregate(reports: list[Path], out: Path) -> None:
    import statistics

    rows: list[dict] = []
    for p in reports:
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            continue
        m = data.get("metrics") or {}
        rows.append(
            {
                "report": str(p),
                "pass": data.get("pass"),
                **{k: m.get(k) for k in AGG_METRIC_KEYS},
            }
        )
    agg: dict = {"n": len(rows), "runs": rows, "median": {}, "min": {}, "max": {}}
    for k in AGG_METRIC_KEYS:
        vals = [r[k] for r in rows if r.get(k) is not None]
        if not vals:
            continue
        agg["median"][k] = statistics.median(vals)
        agg["min"][k] = min(vals)
        agg["max"][k] = max(vals)
    out.write_text(json.dumps(agg, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
