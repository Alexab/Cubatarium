#!/usr/bin/env python3
"""Analyze flight-sim perf_*.jsonl against streaming gate thresholds."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path


def median(xs: list[float]) -> float | None:
    if not xs:
        return None
    return float(statistics.median(xs))


def detect_stop_segment(periods: list[dict], min_len: int = 3) -> list[dict]:
    """Last contiguous run where focus chunk does not move (player stopped)."""
    if len(periods) < min_len:
        return periods[-min_len:] if periods else []
    run_len = 1
    for i in range(len(periods) - 1, 0, -1):
        c0 = periods[i - 1]
        c1 = periods[i]
        dx = abs(int(c1.get("focus_cx") or 0) - int(c0.get("focus_cx") or 0))
        dz = abs(int(c1.get("focus_cz") or 0) - int(c0.get("focus_cz") or 0))
        if dx == 0 and dz == 0:
            run_len += 1
        else:
            break
    if run_len >= min_len:
        return periods[-run_len:]
    return periods[-min_len:] if len(periods) >= min_len else periods


def detect_longest_stop_segment(periods: list[dict], min_len: int = 5) -> list[dict]:
    """Longest contiguous focus plateau (manual idle / hover)."""
    if len(periods) < min_len:
        return detect_stop_segment(periods, min_len=max(3, min_len // 2))
    best_start = 0
    best_len = 0
    run_start = 0
    run_len = 1
    for i in range(1, len(periods)):
        c0 = periods[i - 1]
        c1 = periods[i]
        dx = abs(int(c1.get("focus_cx") or 0) - int(c0.get("focus_cx") or 0))
        dz = abs(int(c1.get("focus_cz") or 0) - int(c0.get("focus_cz") or 0))
        if dx == 0 and dz == 0:
            run_len += 1
        else:
            if run_len > best_len:
                best_len = run_len
                best_start = run_start
            run_start = i
            run_len = 1
    if run_len > best_len:
        best_len = run_len
        best_start = len(periods) - run_len
    if best_len >= min_len:
        return periods[best_start : best_start + best_len]
    return detect_stop_segment(periods, min_len=max(3, min_len // 2))


def analyze(
    path: Path,
    warmup_sec: float = 5.0,
    stop_tail_periods: int = 5,
    manual_idle: bool = False,
) -> dict:
    rows = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    periods = [r for r in rows if r.get("kind") == "period"]
    spikes = [r for r in rows if r.get("kind") == "spike"]
    skip = max(2, int(warmup_sec / 2.0))
    steady = periods[skip:] if len(periods) > skip else periods

    def col(rs, key):
        return [float(r.get(key) or 0) for r in rs if key in r]

    unfinished_key = (
        "unfinished_visual"
        if any("unfinished_visual" in r for r in steady)
        else None
    )
    hole_key = (
        "visual_holes"
        if any("visual_holes" in r for r in steady)
        else "near_focus_holes"
    )
    holes = col(steady, hole_key)
    dark_sticky = col(steady, "black_sticky")
    if not dark_sticky and any("focus_dark_mesh" in r for r in steady):
        dark_sticky = col(steady, "focus_dark_mesh")
    unfinished_visual = col(steady, unfinished_key) if unfinished_key else []
    # Treat light debt as unfinished even when visual_holes=0 (dark meshes).
    effective_holes = []
    for i, (r, h, d) in enumerate(zip(steady, holes, dark_sticky)):
        pend = float(r.get("pending_light_focus") or 0)
        unfinished = (
            (unfinished_visual[i] > 0 if i < len(unfinished_visual) else False)
            or h > 0
            or d > 0
            or pend >= 20
        )
        effective_holes.append(1.0 if unfinished else 0.0)
    effective_holes_rate = (
        sum(effective_holes) / len(effective_holes) if effective_holes else 1.0
    )
    mesh_async_stuck_idle = 0
    run = 0
    for r in steady:
        async_n = float(r.get("mesh_async") or 0)
        relight = float(r.get("relight_drain_ms") or 0)
        cx = int(r.get("focus_cx") or 0)
        cz = int(r.get("focus_cz") or 0)
        if async_n >= 40 and relight < 0.05:
            run += 1
            mesh_async_stuck_idle = max(mesh_async_stuck_idle, run)
        else:
            run = 0
    mesh_async_stuck_sec = mesh_async_stuck_idle * 2.0

    dirty = col(steady, "dirty")
    pending_f = col(steady, "pending_light_focus")
    pressure = col(steady, "stream_pressure")
    wall = col(steady, "wall_ms")
    mesh_async = col(steady, "mesh_async")

    holes_rate = (sum(1 for h in holes if h > 0) / len(holes)) if holes else 1.0
    red_rate = (sum(1 for p in pressure if p >= 2) / len(pressure)) if pressure else 1.0

    focus_pts = [
        (int(r.get("focus_cx") or 0), int(r.get("focus_cz") or 0)) for r in steady
    ]
    chunks_traveled = 0
    if focus_pts:
        c0, c1 = focus_pts[0], focus_pts[-1]
        chunks_traveled = max(abs(c1[0] - c0[0]), abs(c1[1] - c0[1]))

    stuck_async_holes = 0
    run = 0
    for r in steady:
        h = float(r.get(hole_key) or 0)
        a = float(r.get("mesh_async") or 0)
        if h > 0 and a >= 40:
            run += 1
            stuck_async_holes = max(stuck_async_holes, run)
        else:
            run = 0
    stuck_async_holes_sec = stuck_async_holes * 2.0

    dirty_high_run = 0
    run = 0
    for d in dirty:
        if d > 800:
            run += 1
            dirty_high_run = max(dirty_high_run, run)
        else:
            run = 0
    dirty_high_sec = dirty_high_run * 2.0

    def ok_med(val, limit):
        return val is not None and val <= limit

    pending_trend_rising = False
    if len(pending_f) >= 4 and chunks_traveled >= 3:
        mid = len(pending_f) // 2
        first = median(pending_f[:mid]) or 0.0
        second = median(pending_f[mid:]) or 0.0
        pending_trend_rising = second > first + 8.0
    black_proxy_periods = 0
    for r in steady:
        h = float(r.get(hole_key) or 0)
        p = float(r.get("pending_light_focus") or 0)
        if h <= 0 and p >= 20:
            black_proxy_periods += 1
    black_proxy_rate = black_proxy_periods / len(steady) if steady else 0.0

    stop_segment = (
        detect_longest_stop_segment(steady, min_len=8)
        if manual_idle
        else detect_stop_segment(steady, min_len=max(3, stop_tail_periods // 2))
    )
    fly_segment = (
        steady[: len(steady) - len(stop_segment)] if stop_segment else steady
    )
    wall_fly = col(fly_segment, "wall_ms") if fly_segment else wall

    async_stuck_sec = max(stuck_async_holes_sec, mesh_async_stuck_sec)
    wall_fly_med = median(wall_fly)
    gates = {
        "visual_holes_rate_le_0_10": effective_holes_rate <= 0.10,
        "dirty_med_le_400": ok_med(median(dirty), 400),
        "dirty_not_plateau_gt800_10s": dirty_high_sec <= 10.0,
        "pending_light_focus_med_le_15": ok_med(median(pending_f), 15),
        "stream_pressure_red_rate_le_0_30": red_rate <= 0.30,
        "mesh_async_not_stuck_10s": async_stuck_sec <= 10.0,
        "wall_ms_med_le_25": ok_med(wall_fly_med, 25),
        "chunks_traveled_ge_3": chunks_traveled >= 3,
    }
    gates_pass_count = sum(1 for v in gates.values() if v)

    stop_tail = (
        stop_segment[-stop_tail_periods:]
        if len(stop_segment) >= stop_tail_periods
        else stop_segment
    )
    sticky_key = (
        "black_sticky"
        if any("black_sticky" in r for r in stop_tail)
        else "focus_dark_mesh"
    )
    black_sticky_stop = col(stop_tail, sticky_key)
    missing_stop = col(stop_tail, hole_key)
    unfinished_stop = col(stop_tail, unfinished_key) if unfinished_key else []
    not_ready_stop = col(stop_tail, "focus_not_render_ready")
    pending_stop = col(stop_tail, "pending_light_focus")
    relight_stop = col(stop_tail, "relight_drain_ms")
    post_stop_pending_med = median(pending_stop)
    post_stop_black_sticky_max = (
        max(black_sticky_stop) if black_sticky_stop else None
    )
    post_stop_missing_max = max(missing_stop) if missing_stop else None
    stop_effective = []
    for i, r in enumerate(stop_tail):
        h = float(r.get(hole_key) or 0)
        d = float(r.get(sticky_key) or 0)
        pend = float(r.get("pending_light_focus") or 0)
        nr = float(r.get("focus_not_render_ready") or 0)
        unfinished = unfinished_stop[i] > 0 if i < len(unfinished_stop) else False
        stop_effective.append(
            1.0
            if (unfinished or h > 0 or d > 0 or pend >= 15 or nr >= 8)
            else 0.0
        )
    post_stop_effective_holes_rate = (
        sum(stop_effective) / len(stop_effective) if stop_effective else 1.0
    )
    post_stop_relight_med = median(relight_stop)
    post_stop_not_ready_med = median(not_ready_stop) if not_ready_stop else None
    post_stop_not_ready_end = (
        not_ready_stop[-1] if not_ready_stop else None
    )
    stop_not_ready_delta = None
    if len(not_ready_stop) >= 2:
        stop_not_ready_delta = not_ready_stop[-1] - not_ready_stop[0]
    if len(pending_stop) >= 2:
        stop_pending_delta = pending_stop[-1] - pending_stop[0]
    elif len(stop_segment) >= 2:
        full_pending = col(stop_segment, "pending_light_focus")
        if len(full_pending) >= 2:
            stop_pending_delta = full_pending[-1] - full_pending[0]

    # Manual 083042: pendf stuck ~40 for ~30s while wall~22–30 and holes=0.
    stop_wall = col(stop_segment, "wall_ms")
    stop_wall_med = median(stop_wall)
    stop_pending_full = col(stop_segment, "pending_light_focus")
    stop_pending_plateau_sec = 0.0
    plateau_pending_threshold = 5.0 if manual_idle else 20.0
    run = 0
    for i, p in enumerate(stop_pending_full):
        wall_i = stop_wall[i] if i < len(stop_wall) else 999.0
        if p >= plateau_pending_threshold and wall_i < 35.0:
            run += 1
            stop_pending_plateau_sec = max(stop_pending_plateau_sec, run * 2.0)
        else:
            run = 0
    # Healthy FPS with unfinished focus (holes=0 but light debt / dark mesh).
    healthy_unfinished = 0
    for i, r in enumerate(stop_tail):
        wall_r = float(r.get("wall_ms") or 999)
        pend = float(r.get("pending_light_focus") or 0)
        dark = float(r.get(sticky_key) or 0)
        miss = float(r.get(hole_key) or 0)
        unfinished = unfinished_stop[i] > 0 if i < len(unfinished_stop) else False
        if wall_r < 28.0 and (unfinished or pend >= 15 or dark >= 1 or miss >= 1):
            healthy_unfinished += 1
    healthy_unfinished_rate = (
        healthy_unfinished / len(stop_tail) if stop_tail else 1.0
    )

    stop_recovery_ok = (
        (post_stop_black_sticky_max or 0) <= 0.5
        and (post_stop_missing_max or 0) <= 0.5
        and post_stop_effective_holes_rate <= 0.05
        and ok_med(post_stop_pending_med, 5 if manual_idle else 15)
        and stop_pending_plateau_sec <= (60.0 if manual_idle else 8.0)
        and healthy_unfinished_rate <= 0.25
    )
    already_clean_stop = (
        ok_med(post_stop_pending_med, 2 if manual_idle else 15)
        and (post_stop_black_sticky_max or 0) <= 0.5
        and (post_stop_missing_max or 0) <= 0.5
        and post_stop_effective_holes_rate <= 0.05
        and stop_pending_plateau_sec <= (12.0 if manual_idle else 4.0)
    )
    pending_stop_limit = 5 if manual_idle else 15
    gates_stop = {
        "post_stop_pending_med_le_15": ok_med(post_stop_pending_med, pending_stop_limit),
        "post_stop_black_sticky_zero": post_stop_black_sticky_max is not None
        and post_stop_black_sticky_max <= 0.5,
        "post_stop_missing_zero": post_stop_missing_max is not None
        and post_stop_missing_max <= 0.5,
        "post_stop_effective_holes_zero": post_stop_effective_holes_rate <= 0.05,
        "post_stop_pending_falling": already_clean_stop
        or (
            stop_pending_delta is not None and stop_pending_delta <= -3.0
        ),
        "post_stop_relight_active": already_clean_stop
        or (
            post_stop_relight_med is not None and post_stop_relight_med > 0.5
        ),
        "post_stop_pending_not_plateau_8s": stop_pending_plateau_sec
        <= (60.0 if manual_idle else 8.0),
        "post_stop_healthy_fps_not_unfinished": healthy_unfinished_rate <= 0.25,
        "post_stop_not_ready_falling": stop_not_ready_delta is not None
        and stop_not_ready_delta <= -8.0,
    }
    gates_stop_pass_count = sum(1 for v in gates_stop.values() if v)
    passed = all(gates.values()) and all(gates_stop.values())

    soft = {
        "pending_trend_rising_while_traveling": pending_trend_rising,
        "black_proxy_rate": black_proxy_rate,
        "black_proxy_soft_fail": black_proxy_rate >= 0.25,
        "holes_rate_raw": holes_rate,
        "mesh_async_stuck_sec": mesh_async_stuck_sec,
        "gates_stop": gates_stop,
        "stop_segment_periods": len(stop_segment),
        "stop_pending_delta": stop_pending_delta,
        "stop_not_ready_delta": stop_not_ready_delta,
        "stop_recovery_ok": stop_recovery_ok,
    }

    return {
        "perf_jsonl": str(path),
        "periods": len(periods),
        "steady_periods": len(steady),
        "spikes": len(spikes),
        "hole_key": hole_key,
        "unfinished_key": unfinished_key,
        "metrics": {
            "holes_rate": holes_rate,
            "effective_holes_rate": effective_holes_rate,
            "mesh_async_stuck_sec": mesh_async_stuck_sec,
            "dirty_med": median(dirty),
            "dirty_max": max(dirty) if dirty else None,
            "pending_light_focus_med": median(pending_f),
            "red_rate": red_rate,
            "wall_ms_med": median(wall),
            "wall_ms_fly_med": wall_fly_med,
            "mesh_async_med": median(mesh_async),
            "stuck_async_holes_sec": stuck_async_holes_sec,
            "dirty_high_sec": dirty_high_sec,
            "chunks_traveled": chunks_traveled,
            "focus_start": focus_pts[0] if focus_pts else None,
            "focus_end": focus_pts[-1] if focus_pts else None,
            "gates_pass_count": gates_pass_count,
            "gates_total": len(gates),
            "post_stop_pending_med": post_stop_pending_med,
            "post_stop_black_sticky_max": post_stop_black_sticky_max,
            "post_stop_missing_max": post_stop_missing_max,
            "post_stop_effective_holes_rate": post_stop_effective_holes_rate,
            "gates_stop_pass_count": gates_stop_pass_count,
            "gates_stop_total": len(gates_stop),
            "post_stop_relight_med": post_stop_relight_med,
            "post_stop_not_ready_med": post_stop_not_ready_med,
            "post_stop_not_ready_end": post_stop_not_ready_end,
            "stop_not_ready_delta": stop_not_ready_delta,
            "stop_pending_delta": stop_pending_delta,
            "stop_segment_periods": len(stop_segment),
            "stop_tail_periods": len(stop_tail),
        "stop_pending_plateau_sec": stop_pending_plateau_sec,
        "stop_wall_med": stop_wall_med,
        "healthy_unfinished_rate": healthy_unfinished_rate,
        "manual_idle": manual_idle,
    },
        "gates": gates,
        "gates_stop": gates_stop,
        "soft": soft,
        "pass": passed,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("perf_jsonl", type=Path)
    ap.add_argument("--warmup-sec", type=float, default=5.0)
    ap.add_argument("--stop-tail-periods", type=int, default=5)
    ap.add_argument(
        "--manual-idle",
        action="store_true",
        help="use longest focus plateau + stricter pending stop gates",
    )
    ap.add_argument("--report", type=Path, default=None)
    args = ap.parse_args()
    if not args.perf_jsonl.is_file():
        print(f"FAIL: missing {args.perf_jsonl}", file=sys.stderr)
        return 2
    result = analyze(
        args.perf_jsonl,
        args.warmup_sec,
        args.stop_tail_periods,
        manual_idle=args.manual_idle,
    )
    text = json.dumps(result, indent=2)
    print(text)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
