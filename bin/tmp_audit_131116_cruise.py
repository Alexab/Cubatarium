#!/usr/bin/env python3
"""Cruise median + wall waterfall for manual flight audit."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

ROOT = Path("bin/logs")
FLIGHTS = {
    "131116": "perf_20260909-131116_27772.jsonl",
    "194141": "perf_20260908-194141_28732.jsonl",
    "170947": "perf_20260908-170947_28864.jsonl",
    "203627": "perf_20260908-203627_16620.jsonl",
}

KEYS = [
    "wall_ms",
    "fps",
    "scene_ms",
    "mesh_emerge_ms",
    "stream_ms",
    "world_streaming_phase_ms",
    "scene_opaque_cull_ms",
    "scene_opaque_draw_ms",
    "scene_opaque_refresh_ms",
    "scene_transparent_ms",
    "scene_self_ms",
    "prep_schedule_policy_ms",
    "mesh_dirty_schedule_ms",
    "prep_refresh_pressure_ms",
    "relight_fifo_n",
    "softdefer_empty_stuck_n",
    "visual_holes",
    "visible_black_focus_n",
    "mesh_dirty_schedule_ok_n",
    "gpu_kick_n",
    "focus_missing_mesh",
    "unfinished_visual",
    "mesh_discarded_late",
    "opaque_gpu_packed_n",
    "opaque_cull_skipped",
    "chunk_meshed_unlit",
    "chunk_meshed_unlit_preview",
    "chunk_meshed_unlit_hidden",
    "mesh_gpu_finish_ms",
    "mesh_gpu_kick_ms",
    "async_io_ms",
    "relight_drain_ms",
    "prep_sync_focus_ring_ms",
    "prep_recover_ms",
    "fluid_map_cpu_ms",
    "gui_overlay_ms",
    "render_total_ms",
    "movement_speed",
]

SCENE_KEYS = [
    "scene_opaque_cull_ms",
    "scene_opaque_draw_ms",
    "scene_opaque_refresh_ms",
    "scene_transparent_ms",
    "scene_self_ms",
    "scene_filter_ready_ms",
    "scene_depth_capture_ms",
    "scene_overlays_ms",
    "scene_opaque_gpu_draw_ms",
    "scene_opaque_packed_ms",
]

EMERGE_KEYS = [
    "mesh_emerge_prep_ms",
    "prep_refresh_pressure_ms",
    "prep_schedule_policy_ms",
    "prep_sync_focus_ring_ms",
    "prep_recover_ms",
    "mesh_dirty_schedule_ms",
    "mesh_dirty_tick_ms",
    "mesh_gpu_finish_ms",
    "mesh_gpu_kick_ms",
    "prep_softdefer_pre_ms",
    "prep_hole_force_ms",
    "softdefer_empty_scan_ms",
]

STREAM_KEYS = [
    "async_io_ms",
    "relight_drain_ms",
    "streamer_update_ms",
    "idle_prefetch_ms",
    "relight_capture_ms",
    "relight_apply_ms",
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


def analyze(tag, fname):
    path = ROOT / fname
    periods, cruise = load_cruise(path)
    print(f"\n=== {tag} {fname} periods={len(periods)} cruise={len(cruise)} ===")
    if not cruise:
        print("  NO CRUISE")
        return None
    meds = {}
    for k in KEYS:
        meds[k] = median(series(cruise, k))
        p90 = pct(series(cruise, k), 90)
        print(f"  {k:32s} med={meds[k]!s:>10}  p90={p90!s:>10}")

    # Wall attribution
    wall = meds["wall_ms"] or 1.0
    scene = meds["scene_ms"] or 0.0
    stream = meds["stream_ms"] or 0.0
    emerge = meds["mesh_emerge_ms"] or 0.0
    phase = meds["world_streaming_phase_ms"] or 0.0
    other = max(0.0, wall - scene - stream - emerge)
    print("\n  WALL ATTRIBUTION (medians):")
    for name, v in [
        ("scene", scene),
        ("stream", stream),
        ("emerge", emerge),
        ("other(wall-scene-stream-emerge)", other),
        ("phase(for ref)", phase),
    ]:
        print(f"    {name:40s} {v:8.2f} ms  {100.0 * v / wall:5.1f}%")

    print("\n  WITHIN SCENE:")
    for k in SCENE_KEYS:
        v = median(series(cruise, k)) or 0.0
        print(f"    {k:32s} {v:8.3f}  {100.0 * v / max(scene, 1e-9):5.1f}% of scene  {100.0 * v / wall:5.1f}% wall")

    print("\n  WITHIN EMERGE / STREAM timers:")
    for k in EMERGE_KEYS + STREAM_KEYS:
        v = median(series(cruise, k)) or 0.0
        if v < 0.05:
            continue
        print(f"    {k:32s} {v:8.3f}  {100.0 * v / wall:5.1f}% wall")

    # schedule_ok=0 rate
    oks = series(cruise, "mesh_dirty_schedule_ok_n")
    zero_ok = sum(1 for x in oks if x == 0) / len(oks) if oks else 0
    cull_skip = series(cruise, "opaque_cull_skipped")
    cull_always = sum(1 for x in cull_skip if x == 0) / len(cull_skip) if cull_skip else 0
    print(f"\n  schedule_ok==0 frac={zero_ok:.3f}  cull_skipped==0 frac={cull_always:.3f}")
    print(
        f"  unlit_preview med={median(series(cruise, 'chunk_meshed_unlit_preview'))} "
        f"unlit med={median(series(cruise, 'chunk_meshed_unlit'))} "
        f"unlit_hidden med={median(series(cruise, 'chunk_meshed_unlit_hidden'))}"
    )
    holes_frac = sum(1 for r in cruise if float(r.get("visual_holes") or 0) > 0) / len(cruise)
    miss_frac = sum(1 for r in cruise if float(r.get("focus_missing_mesh") or 0) > 0) / len(cruise)
    print(f"  visual_holes_frac={holes_frac:.3f} focus_missing_frac={miss_frac:.3f}")
    return meds


def main():
    all_meds = {}
    for tag, fname in FLIGHTS.items():
        path = ROOT / fname
        if not path.exists():
            print(f"MISSING {fname}")
            continue
        all_meds[tag] = analyze(tag, fname)

    # Comparison table
    cols = ["131116", "194141", "170947", "203627"]
    metrics = [
        "wall_ms",
        "fps",
        "scene_ms",
        "mesh_emerge_ms",
        "stream_ms",
        "world_streaming_phase_ms",
        "scene_opaque_cull_ms",
        "scene_opaque_draw_ms",
        "prep_schedule_policy_ms",
        "relight_fifo_n",
        "softdefer_empty_stuck_n",
        "visual_holes",
        "visible_black_focus_n",
        "mesh_dirty_schedule_ok_n",
        "gpu_kick_n",
        "focus_missing_mesh",
        "unfinished_visual",
        "mesh_discarded_late",
        "opaque_gpu_packed_n",
    ]
    print("\n\n=== COMPARISON TABLE (cruise medians) ===")
    header = f"{'metric':32s}" + "".join(f"{c:>12s}" for c in cols) + f"{'SoT093857':>12s}"
    print(header)
    sot = {
        "wall_ms": 98.0,
        "fps": 1000 / 98.0,
        "scene_opaque_cull_ms": 13.0,
        "relight_fifo_n": 0.0,
        "softdefer_empty_stuck_n": 0.0,
        "mesh_discarded_late": 12.0,  # abs class
        "visible_black_focus_n": 43.0,
    }
    for m in metrics:
        row = f"{m:32s}"
        for c in cols:
            v = (all_meds.get(c) or {}).get(m)
            row += f"{v if v is not None else '-':>12}" if not isinstance(v, float) else f"{v:12.3f}"
        sv = sot.get(m)
        row += f"{sv:12.3f}" if isinstance(sv, float) else f"{'-':>12}"
        print(row)


if __name__ == "__main__":
    main()
