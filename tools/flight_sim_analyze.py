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


def analyze(path: Path, warmup_sec: float = 5.0) -> dict:
    rows = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    periods = [r for r in rows if r.get("kind") == "period"]
    spikes = [r for r in rows if r.get("kind") == "spike"]
    # Approximate warmup: skip first periods until cumulative ~warmup via frames*interval.
    # Periods are ~2s each; skip first max(2, warmup/2) periods.
    skip = max(2, int(warmup_sec / 2.0))
    steady = periods[skip:] if len(periods) > skip else periods

    def col(rs, key):
        return [float(r.get(key) or 0) for r in rs if key in r]

    # Prefer visual_holes if present (phase 1); else near_focus_holes.
    hole_key = (
        "visual_holes"
        if any("visual_holes" in r for r in steady)
        else "near_focus_holes"
    )
    holes = col(steady, hole_key)
    dirty = col(steady, "dirty")
    pending_f = col(steady, "pending_light_focus")
    pressure = col(steady, "stream_pressure")
    wall = col(steady, "wall_ms")
    mesh_async = col(steady, "mesh_async")

    holes_rate = (sum(1 for h in holes if h > 0) / len(holes)) if holes else 1.0
    red_rate = (sum(1 for p in pressure if p >= 2) / len(pressure)) if pressure else 1.0

    # Focus travel (manual World_164 ocean: ~11 chunks west). Stationary
    # hold-forward used to false-pass all gates.
    focus_pts = [
        (int(r.get("focus_cx") or 0), int(r.get("focus_cz") or 0)) for r in steady
    ]
    chunks_traveled = 0
    if focus_pts:
        c0, c1 = focus_pts[0], focus_pts[-1]
        chunks_traveled = max(abs(c1[0] - c0[0]), abs(c1[1] - c0[1]))

    # Stuck mesh_async~42 while holes: consecutive periods.
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
    # Each period ~2s
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

    # Soft diagnostics (do not fail hard gates): rising pending while traveling,
    # and black-proxy (mesh present / holes low while pending stays high).
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
    black_proxy_rate = (
        black_proxy_periods / len(steady) if steady else 0.0
    )

    gates = {
        "visual_holes_rate_le_0_10": holes_rate <= 0.10,
        "dirty_med_le_400": ok_med(median(dirty), 400),
        "dirty_not_plateau_gt800_10s": dirty_high_sec <= 10.0,
        "pending_light_focus_med_le_15": ok_med(median(pending_f), 15),
        "stream_pressure_red_rate_le_0_30": red_rate <= 0.30,
        "mesh_async_not_stuck_with_holes_10s": stuck_async_holes_sec <= 10.0,
        "wall_ms_med_le_25": ok_med(median(wall), 25),
        "chunks_traveled_ge_3": chunks_traveled >= 3,
    }
    passed = all(gates.values())

    soft = {
        "pending_trend_rising_while_traveling": pending_trend_rising,
        "black_proxy_rate": black_proxy_rate,
        "black_proxy_soft_fail": black_proxy_rate >= 0.25,
    }

    return {
        "perf_jsonl": str(path),
        "periods": len(periods),
        "steady_periods": len(steady),
        "spikes": len(spikes),
        "hole_key": hole_key,
        "metrics": {
            "holes_rate": holes_rate,
            "dirty_med": median(dirty),
            "dirty_max": max(dirty) if dirty else None,
            "pending_light_focus_med": median(pending_f),
            "red_rate": red_rate,
            "wall_ms_med": median(wall),
            "mesh_async_med": median(mesh_async),
            "stuck_async_holes_sec": stuck_async_holes_sec,
            "dirty_high_sec": dirty_high_sec,
            "chunks_traveled": chunks_traveled,
            "focus_start": focus_pts[0] if focus_pts else None,
            "focus_end": focus_pts[-1] if focus_pts else None,
        },
        "gates": gates,
        "soft": soft,
        "pass": passed,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("perf_jsonl", type=Path)
    ap.add_argument("--warmup-sec", type=float, default=5.0)
    ap.add_argument("--report", type=Path, default=None)
    args = ap.parse_args()
    if not args.perf_jsonl.is_file():
        print(f"FAIL: missing {args.perf_jsonl}", file=sys.stderr)
        return 2
    result = analyze(args.perf_jsonl, args.warmup_sec)
    text = json.dumps(result, indent=2)
    print(text)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
