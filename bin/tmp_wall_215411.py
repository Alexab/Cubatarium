#!/usr/bin/env python3
"""Wall composition: which main-thread steps eat cruise 215411 vs 195810."""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")

MS_KEYS = [
    "wall_ms",
    "sim_ms",
    "swap_wait_ms",
    "unaccounted_ms",
    "world_streaming_phase_ms",
    "stream_ms",
    "mesh_emerge_ms",
    "mesh_emerge_prep_ms",
    "mesh_emerge_prep_missing_ms",
    "mesh_emerge_prep_unfinished_ms",
    "mesh_emerge_prep_sticky_ms",
    "mesh_emerge_prep_drop_dirty_ms",
    "prep_pending_light_ms",
    "prep_softdefer_setup_ms",
    "softdefer_empty_scan_ms",
    "relight_drain_ms",
    "fast_relight_ms",
    "fluid_map_cpu_ms",
    "fluid_map_gpu_ms",
    "commit_apply_ms",
    "commit_seal_ms",
    "commit_physics_ms",
    "streamer_update_ms",
    "async_io_ms",
    "mesh_sync_ms",
    "mesh_snapshot_ms",
    "mesh_immediate_ms",
    "mesh_dirty_tick_ms",
    "mesh_dirty_prune_ms",
    "mesh_dirty_sort_ms",
    "mesh_dirty_drain_ms",
    "mesh_dirty_schedule_ms",
    "mesh_dirty_gpu_ms",
    "mesh_dirty_sync_ms",
    "scene_ms",
    "render_total_ms",
    "phys_ms",
    "physics_block_ms",
    "physics_drain_ms",
    "do_movement_ms",
    "tick_env_ms",
    "prepare_frame_ms",
    "post_scene_ms",
    "gui_overlay_ms",
    "autosave_ms",
    "views_ms",
    "idle_prefetch_ms",
    "residual_ms",
]


def pct(vals, p):
    if not vals:
        return None
    vals = sorted(vals)
    i = min(len(vals) - 1, int(round((len(vals) - 1) * p)))
    return vals[i]


def load(name):
    rows = []
    for line in (LOGS / name).read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return [r for r in rows if (r.get("chunk_count") or 0) >= 80]


def summarize(label, name):
    cruise = load(name)
    print(f"\n===== {label} n={len(cruise)} =====")
    ranked = []
    for k in MS_KEYS:
        vals = [r[k] for r in cruise if isinstance(r.get(k), (int, float))]
        if not vals:
            continue
        med, p90, mx, s = pct(vals, 0.5), pct(vals, 0.9), max(vals), sum(vals)
        ranked.append((s, k, med, p90, mx, min(vals)))
    ranked.sort(reverse=True)
    wall_sum = next((s for s, k, *_ in ranked if k == "wall_ms"), 1.0)
    print(f"  {'key':36} {'med':>8} {'p90':>8} {'max':>8} {'sum%wall':>8}")
    for s, k, med, p90, mx, mn in ranked:
        if med is None:
            continue
        if med < 0.3 and p90 < 2.0 and k not in (
            "wall_ms",
            "relight_drain_ms",
            "mesh_emerge_ms",
            "stream_ms",
            "fluid_map_cpu_ms",
            "scene_ms",
        ):
            continue
        print(
            f"  {k:36} {med:8.2f} {p90:8.2f} {mx:8.1f} {100.0 * s / wall_sum:7.1f}%"
        )

    # drain share of wall
    pairs = [
        (r.get("wall_ms") or 0, r.get("relight_drain_ms") or 0, r.get("mesh_emerge_ms") or 0,
         r.get("stream_ms") or 0, r.get("scene_ms") or 0, r.get("fluid_map_cpu_ms") or 0,
         r.get("mesh_dirty_tick_ms") or 0, r.get("world_streaming_phase_ms") or 0)
        for r in cruise
    ]
    def share(i):
        w = [p[0] for p in pairs if p[0] > 0]
        v = [p[i] for p in pairs if p[0] > 0]
        return 100.0 * sum(v) / sum(w) if w else 0
    print(f"  drain/wall sum%={share(1):.1f} emerge/wall={share(2):.1f} "
          f"stream/wall={share(3):.1f} scene/wall={share(4):.1f} fluid/wall={share(5):.1f} "
          f"dirty_tick/wall={share(6):.1f} stream_phase/wall={share(7):.1f}")

    # hot skip / bg cap
    for k in ("capture_over_budget", "capture_bg_cap_n", "capture_band_cy",
              "softdefer_capture_budget", "async_relight_inflight",
              "relight_apply_n", "column_lighting_n", "column_meshing_n",
              "mesh_async", "gen_q", "stream_loads", "fluid_map_full_rebuild",
              "fluid_map_dirty", "mesh_immediate_count"):
        vals = [r.get(k) for r in cruise if r.get(k) is not None]
        if not vals:
            print(f"  {k}: MISSING")
            continue
        nums = [v for v in vals if isinstance(v, (int, float))]
        if not nums:
            print(f"  {k}: {Counter(vals).most_common(4)}")
            continue
        print(f"  {k}: med={pct(nums,0.5)} p90={pct(nums,0.9)} max={max(nums)} "
              f"nonzero={sum(1 for v in nums if v)}")

    # correlation: drain vs miss vs apply
    miss = [r for r in cruise if r.get("focus_missing_mesh")]
    print(f"  miss n={len(miss)}")
    # wall without first spike
    walls = sorted(r["wall_ms"] for r in cruise if isinstance(r.get("wall_ms"), (int, float)))
    if walls:
        print(f"  wall without max: med={pct(walls[1:],0.5) if len(walls)>1 else None} "
              f"p90={pct(walls,0.9)}")

    # top 8 wall frames composition
    top = sorted(cruise, key=lambda r: r.get("wall_ms") or 0, reverse=True)[:8]
    print("  TOP wall frames:")
    for r in top:
        print(
            f"    wall={r.get('wall_ms'):7.1f} drain={r.get('relight_drain_ms'):6.1f} "
            f"emerge={r.get('mesh_emerge_ms'):6.1f} stream={r.get('stream_ms'):5.1f} "
            f"fluid={r.get('fluid_map_cpu_ms'):6.1f} scene={r.get('scene_ms'):5.1f} "
            f"dirty={r.get('mesh_dirty_tick_ms')} phase={r.get('world_streaming_phase_ms')} "
            f"cz={r.get('focus_cz')} fin={r.get('relight_capture_finalize')} "
            f"span={r.get('relight_capture_band_cy_span')} cap_h={r.get('relight_capture_col_horiz')} "
            f"kind={r.get('kind')}"
        )


def main():
    summarize("215411", "perf_20260817-215411_29916.jsonl")
    summarize("195810", "perf_20260817-195810_30216.jsonl")


if __name__ == "__main__":
    main()
