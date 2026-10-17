import json
import statistics
from pathlib import Path

rows = [
    json.loads(l)
    for l in Path("bin/logs/perf_20260914-080455_21652.jsonl")
    .read_text(encoding="utf-8")
    .splitlines()
    if l.strip()
]
print("n", len(rows))
print("i wall vb unf miss stuck kick finish ok okr skip rem fm age debtf")
for i, r in enumerate(rows):
    print(
        "%2d %6.1f %3d %3d %s %4d %d/%d %d %d %3d %3d %3d %3d %d"
        % (
            i,
            r.get("wall_ms", 0) or 0,
            r.get("visible_black_focus_n", 0) or 0,
            r.get("unfinished_visual", 0) or 0,
            r.get("focus_missing_mesh", 0),
            r.get("miss_stuck_run_frames", 0) or 0,
            r.get("gpu_kick_n", 0) or 0,
            r.get("gpu_finish_n", 0) or 0,
            r.get("mesh_dirty_schedule_ok_n", 0) or 0,
            r.get("mesh_dirty_schedule_ok_remesh_n", 0) or 0,
            r.get("mesh_dirty_schedule_skip_snapshot_n", 0) or 0,
            r.get("dirty_remesh_n", 0) or 0,
            r.get("dirty_fm_n", 0) or 0,
            r.get("oldest_stale_vertex_light_age_frames", 0) or 0,
            r.get("gpu_kick_debt_forced_n", 0) or 0,
        )
    )

def med(key):
    xs = [r.get(key) for r in rows if isinstance(r.get(key), (int, float))]
    return statistics.median(xs) if xs else None

print("--- medians ---")
for k in [
    "wall_ms",
    "visible_black_focus_n",
    "unfinished_visual",
    "focus_missing_mesh",
    "miss_stuck_run_frames",
    "gpu_kick_n",
    "empty_backlog",
    "pool_unsync_n",
    "dirty_fm_n",
    "dirty_remesh_n",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_ok_remesh_n",
    "mesh_dirty_schedule_skip_snapshot_n",
]:
    print(k, med(k))

# miss_stuck with kick~0 frames
bad = sum(
    1
    for r in rows
    if (r.get("miss_stuck_run_frames") or 0) > 30
    and (r.get("gpu_kick_n") or 0) == 0
)
print("frames miss_stuck>30 & kick=0:", bad, "/", len(rows))
print(
    "frames miss_stuck>30 & kick>0:",
    sum(
        1
        for r in rows
        if (r.get("miss_stuck_run_frames") or 0) > 30
        and (r.get("gpu_kick_n") or 0) > 0
    ),
)
