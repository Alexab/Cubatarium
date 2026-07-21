#!/usr/bin/env python3
"""Iterate westbound flight-sim with streaming_tune.json knob descent."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from copy import deepcopy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
RUN = Path(__file__).with_name("flight_sim_run.py")
TUNE_PATH = BIN / "streaming_tune.json"
HISTORY_PATH = BIN / "flight_sim_iterate_history.jsonl"

DEFAULT_TUNE = {
    "mesh_forward_bias_k": 0.75,
    "relight_inflight_mult_high": 4,
    "relight_inflight_mult_holes": 6,
    "mesh_fly_cap_yellow": 10,
    "mesh_fly_cap_red": 8,
    "recover_n_boost": 0,
}

# Coordinate descent steps when gates fail (pending → holes → wall).
KNOB_STEPS = [
    ("relight_inflight_mult_holes", +2, 12),
    ("relight_inflight_mult_high", +2, 10),
    ("recover_n_boost", +2, 8),
    ("mesh_fly_cap_yellow", +4, 24),
    ("mesh_fly_cap_red", +4, 20),
    ("mesh_forward_bias_k", +0.25, 1.5),
]


def write_tune(tune: dict) -> None:
    BIN.mkdir(parents=True, exist_ok=True)
    TUNE_PATH.write_text(json.dumps(tune, indent=2) + "\n", encoding="utf-8")


def load_tune() -> dict:
    if TUNE_PATH.is_file():
        data = json.loads(TUNE_PATH.read_text(encoding="utf-8"))
        out = dict(DEFAULT_TUNE)
        out.update({k: data[k] for k in DEFAULT_TUNE if k in data})
        return out
    return dict(DEFAULT_TUNE)


def append_history(entry: dict) -> None:
    with HISTORY_PATH.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")


def worse_wall_or_red(prev: dict | None, cur: dict) -> bool:
    if prev is None:
        return False
    pm = prev.get("metrics") or {}
    cm = cur.get("metrics") or {}
    wall_p = pm.get("wall_ms_med")
    wall_c = cm.get("wall_ms_med")
    red_p = pm.get("red_rate")
    red_c = cm.get("red_rate")
    if wall_p is not None and wall_c is not None and wall_c > wall_p + 8.0:
        return True
    if red_p is not None and red_c is not None and red_c > red_p + 0.15:
        return True
    return False


def pick_knob_update(tune: dict, result: dict) -> tuple[str, float | int] | None:
    gates = result.get("gates") or {}
    gates_stop = result.get("gates_stop") or {}
    soft = result.get("soft") or {}
    order = []
    if not gates_stop.get("post_stop_black_sticky_zero", True) or soft.get(
        "black_proxy_soft_fail", False
    ):
        order.extend(
            [
                "relight_inflight_mult_holes",
                "relight_inflight_mult_high",
                "recover_n_boost",
            ]
        )
    if not gates.get("pending_light_focus_med_le_15", True):
        order.extend(
            [
                "relight_inflight_mult_holes",
                "relight_inflight_mult_high",
                "recover_n_boost",
            ]
        )
    if not gates.get("visual_holes_rate_le_0_10", True):
        order.extend(
            [
                "recover_n_boost",
                "mesh_fly_cap_yellow",
                "mesh_fly_cap_red",
                "mesh_forward_bias_k",
            ]
        )
    if not gates.get("wall_ms_med_le_25", True) or not gates.get(
        "stream_pressure_red_rate_le_0_30", True
    ):
        # On wall/red fail, do not raise mesh fly further — prefer relight.
        order.extend(["relight_inflight_mult_holes", "recover_n_boost"])
    if not gates.get("dirty_med_le_400", True):
        order.append("recover_n_boost")

    seen = set()
    for name in order:
        if name in seen:
            continue
        seen.add(name)
        for kn, step, cap in KNOB_STEPS:
            if kn != name:
                continue
            cur = tune[kn]
            nxt = cur + step
            if isinstance(cap, float):
                if nxt > cap + 1e-6:
                    continue
            elif nxt > cap:
                continue
            return kn, nxt
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--max-iters", type=int, default=6)
    ap.add_argument("--build-first", action="store_true")
    ap.add_argument("--fly-stop", action="store_true")
    ap.add_argument("--commit-on-pass", action="store_true")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build" / "desktop-linux",
    )
    args = ap.parse_args()

    tune = load_tune()
    write_tune(tune)
    prev_result: dict | None = None
    prev_tune = deepcopy(tune)

    for i in range(args.max_iters):
        report = BIN / f"flight_sim_gate_report_iter{i}.json"
        cmd = [
            sys.executable,
            str(RUN),
            "--world",
            args.world,
            "--seconds",
            str(args.seconds),
            "--report",
            str(report),
            "--build-dir",
            str(args.build_dir),
        ]
        if args.build_first and i == 0:
            cmd.append("--build")
        cmd.append("--update-best")
        if args.fly_stop:
            cmd.append("--fly-stop")
            cmd.extend(["--process-timeout", "300"])
        else:
            cmd.extend(["--process-timeout", "180"])
        print(f"=== iterate {i}: tune={tune}", flush=True)
        write_tune(tune)
        rc = subprocess.call(cmd)
        if not report.is_file():
            print(f"FAIL: missing report {report}", file=sys.stderr)
            return 1
        result = json.loads(report.read_text(encoding="utf-8"))
        entry = {
            "iter": i,
            "tune": deepcopy(tune),
            "pass": result.get("pass"),
            "metrics": result.get("metrics"),
            "gates": result.get("gates"),
            "soft": result.get("soft"),
            "run_rc": rc,
            "report": str(report),
        }
        append_history(entry)
        print(json.dumps(entry, indent=2), flush=True)

        best_path = BIN / "flight_sim_gate_report_west_best.json"
        best = None
        if best_path.is_file():
            best = json.loads(best_path.read_text(encoding="utf-8"))
        cur_pass = sum(1 for v in (result.get("gates") or {}).values() if v)
        best_pass = (
            sum(1 for v in (best.get("gates") or {}).values() if v) if best else 0
        )
        if cur_pass > best_pass:
            print(
                f"CHECKPOINT: gates {best_pass} -> {cur_pass} — "
                "consider: python tools/flight_sim_checkpoint.py --label iterate",
                flush=True,
            )

        if result.get("pass"):
            print("PASS: gates satisfied", flush=True)
            shutil.copyfile(report, BIN / "flight_sim_gate_report_west_pass.json")
            if args.commit_on_pass:
                subprocess.call(
                    [
                        sys.executable,
                        str(Path(__file__).with_name("flight_sim_checkpoint.py")),
                        "--label",
                        "iterate-pass",
                        "--report",
                        str(report),
                        "--force",
                    ]
                )
            return 0

        if worse_wall_or_red(prev_result, result):
            print("rollback tune: wall/red worsened", flush=True)
            tune = deepcopy(prev_tune)
            write_tune(tune)
            # Still try a different knob next
        else:
            prev_tune = deepcopy(tune)
            prev_result = result

        upd = pick_knob_update(tune, result)
        if upd is None:
            print("STOP: no more knob headroom", flush=True)
            return 1
        kn, nxt = upd
        print(f"adjust {kn}: {tune[kn]} -> {nxt}", flush=True)
        tune[kn] = nxt

    print("STOP: max iters without PASS", flush=True)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
