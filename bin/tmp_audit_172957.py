#!/usr/bin/env python3
"""Audit manual 172957 (R7.1) vs 153045/131116 — wall, holes, FPS jitter, stream lag."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

ROOT = Path("bin/logs")
FLIGHTS = {
    "172957_R71": "perf_20260909-172957_8616.jsonl",
    "153045_R7": "perf_20260909-153045_20028.jsonl",
    "131116_R6": "perf_20260909-131116_27772.jsonl",
    "194141_R5": "perf_20260908-194141_28732.jsonl",
}

KEYS = [
    "wall_ms",
    "fps",
    "scene_ms",
    "mesh_emerge_ms",
    "stream_ms",
    "world_streaming_phase_ms",
    "scene_opaque_cull_ms",
    "scene_transparent_ms",
    "prep_schedule_policy_ms",
    "mesh_dirty_tick_ms",
    "mesh_dirty_schedule_ms",
    "relight_fifo_n",
    "softdefer_empty_stuck_n",
    "visual_holes",
    "visible_black_focus_n",
    "mesh_dirty_schedule_ok_n",
    "gpu_kick_n",
    "focus_missing_mesh",
    "unfinished_visual",
    "mesh_discarded_late",
    "opaque_cull_skipped",
    "chunk_meshed_unlit_hidden",
    "column_meshing_n",
    "movement_speed",
    "miss_horiz",
    "dirty_fm_n",
    "dirty_n",
    "async_io_ms",
    "relight_drain_ms",
    "streamer_update_ms",
]


def median(xs):
    return st.median(xs) if xs else None


def pct(xs, p):
    if not xs:
        return None
    xs = sorted(xs)
    if len(xs) == 1:
        return xs[0]
    k = (len(xs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def load_cruise(path: Path):
    periods = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            if '"kind":"period"' not in line and '"kind": "period"' not in line:
                continue
            row = json.loads(line)
            if row.get("kind") != "period":
                continue
            periods.append(row)
    cruise = [r for r in periods if float(r.get("movement_speed") or 0) > 2.0]
    return periods, cruise


def series(rows, key):
    out = []
    for r in rows:
        if key == "fps":
            w = r.get("wall_ms")
            if w and w > 0:
                out.append(1000.0 / float(w))
            continue
        if key in r and r[key] is not None:
            out.append(float(r[key]))
    return out


def frac_gt0(rows, key):
    xs = series(rows, key)
    if not xs:
        return None
    return sum(1 for v in xs if v > 0) / len(xs)


def analyze(tag, fname):
    path = ROOT / fname
    periods, cruise = load_cruise(path)
    print(f"\n=== {tag} periods={len(periods)} cruise={len(cruise)} ===")
    if not cruise:
        print("  NO CRUISE")
        return {}
    meds = {}
    for k in KEYS:
        xs = series(cruise, k)
        meds[k] = median(xs)
        if k in ("wall_ms", "fps", "stream_ms", "mesh_emerge_ms", "scene_ms"):
            print(
                f"  {k:28s} med={median(xs)!s:>10}  p50={pct(xs,50)!s:>10}  "
                f"p90={pct(xs,90)!s:>10}  p95={pct(xs,95)!s:>10}  max={max(xs) if xs else None}"
            )
        else:
            print(f"  {k:28s} med={median(xs)}")
    meds["holes_frac"] = frac_gt0(cruise, "visual_holes")
    meds["focus_missing_frac"] = frac_gt0(cruise, "focus_missing_mesh")
    walls = series(cruise, "wall_ms")
    fpss = series(cruise, "fps")
    if walls:
        meds["wall_p90"] = pct(walls, 90)
        meds["wall_p95"] = pct(walls, 95)
        meds["wall_max"] = max(walls)
        meds["wall_stdev"] = st.pstdev(walls) if len(walls) > 1 else 0.0
        meds["wall_jitter_cv"] = (
            meds["wall_stdev"] / meds["wall_ms"] if meds["wall_ms"] else None
        )
    if fpss:
        meds["fps_p10"] = pct(fpss, 10)
        meds["fps_stdev"] = st.pstdev(fpss) if len(fpss) > 1 else 0.0
        # frame-to-frame absolute delta (period medians already 1s-ish)
        diffs = [abs(fpss[i] - fpss[i - 1]) for i in range(1, len(fpss))]
        meds["fps_absdiff_med"] = median(diffs)
        meds["fps_absdiff_p90"] = pct(diffs, 90)
    # stream lag proxies
    meshing = series(cruise, "column_meshing_n")
    # render_ready may be absent — use unfinished / miss
    meds["stream_share"] = (
        (meds.get("stream_ms") or 0) / (meds.get("wall_ms") or 1)
    )
    meds["emerge_share"] = (
        (meds.get("mesh_emerge_ms") or 0) / (meds.get("wall_ms") or 1)
    )
    print(
        f"  holes_frac={meds['holes_frac']} focus_missing_frac={meds['focus_missing_frac']}"
    )
    print(
        f"  wall_stdev={meds.get('wall_stdev')} cv={meds.get('wall_jitter_cv')} "
        f"fps_p10={meds.get('fps_p10')} fps_absdiff_med={meds.get('fps_absdiff_med')} "
        f"p90={meds.get('fps_absdiff_p90')}"
    )
    print(
        f"  stream_share={meds['stream_share']:.3f} emerge_share={meds['emerge_share']:.3f}"
    )
    return meds


def main():
    all_meds = {tag: analyze(tag, fn) for tag, fn in FLIGHTS.items()}
    base = all_meds.get("131116_R6", {})
    bad = all_meds.get("153045_R7", {})
    cur = all_meds.get("172957_R71", {})
    print("\n=== DELTA 172957 vs 131116 / 153045 ===")
    for k in [
        "wall_ms",
        "fps",
        "wall_p90",
        "wall_p95",
        "wall_stdev",
        "wall_jitter_cv",
        "fps_p10",
        "fps_absdiff_med",
        "holes_frac",
        "focus_missing_frac",
        "softdefer_empty_stuck_n",
        "mesh_dirty_schedule_ok_n",
        "dirty_fm_n",
        "stream_ms",
        "mesh_emerge_ms",
        "scene_ms",
        "scene_opaque_cull_ms",
        "prep_schedule_policy_ms",
        "relight_fifo_n",
        "column_meshing_n",
        "unfinished_visual",
        "miss_horiz",
        "async_io_ms",
        "relight_drain_ms",
    ]:
        a, b, c = cur.get(k), base.get(k), bad.get(k)
        print(f"  {k:28s} R71={a}  R6={b}  R7bad={c}")


if __name__ == "__main__":
    main()
