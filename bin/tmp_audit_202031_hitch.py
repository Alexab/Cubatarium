import json
from pathlib import Path

rows = [
    json.loads(l)
    for l in Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260920-202031_6988.jsonl")
    .read_text(encoding="utf-8")
    .splitlines()
    if l.strip()
]

interest = [
    "mesh_emerge_ms",
    "mesh_emerge_prep_ms",
    "mesh_dirty_tick_ms",
    "mesh_dirty_schedule_ms",
    "mesh_dirty_prune_ms",
    "mesh_dirty_sort_ms",
    "mesh_dirty_drain_ms",
    "mesh_dirty_gpu_ms",
    "mesh_gpu_kick_ms",
    "mesh_gpu_finish_ms",
    "mesh_async_drain_ms",
    "mesh_snapshot_ms",
    "mesh_sync_ms",
    "mesh_immediate_ms",
    "render_total_ms",
    "wall_ms",
    "stream_ms",
    "world_streaming_phase_ms",
    "scene_ms",
    "do_movement_ms",
    "emerge_budget_ms",
    "spikes",
    "dirty_n",
    "dirty_fm_n",
    "dirty_remesh_n",
    "dirty_admit_budget_end",
    "gpu_kick_n",
    "gpu_finish_n",
    "gpu_kick_post_drain_n",
    "mark_relit_prefer_kick_n",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_ok_fm_n",
    "mesh_dirty_schedule_ok_remesh_n",
    "mesh_dirty_schedule_skip_n",
    "publication_progress_unit_n",
    "mesh_apply_stale",
    "mesh_apply_stale_geom",
    "mesh_apply_stale_light_accepted",
    "mesh_apply_stale_accepted_refresh",
    "mesh_apply_stale_visual",
    "unfinished_visual",
    "near_focus_holes",
    "focus_missing_mesh",
    "column_job_gpu_pending_n",
    "pending_gpu",
    "softdefer_empty_placeholder_n",
]

for idx in [27, 36, 65, 72, 73]:
    r = rows[idx]
    print("==== p%d ====" % idx)
    for k in interest:
        v = r.get(k)
        if v is None:
            continue
        if isinstance(v, float) and abs(v) < 0.01:
            continue
        if isinstance(v, int) and v == 0:
            continue
        print(" ", k, "=", v)
    # any other float > 20ms
    extras = []
    for k, v in r.items():
        if k in interest:
            continue
        if isinstance(v, float) and v > 20:
            extras.append((v, k))
    extras.sort(reverse=True)
    print("  EXTRA>20ms:")
    for v, k in extras[:15]:
        print("   ", k, "=", v)
