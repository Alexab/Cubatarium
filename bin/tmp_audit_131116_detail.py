#!/usr/bin/env python3
import json, statistics as st
from pathlib import Path

p = Path("bin/logs/perf_20260909-131116_27772.jsonl")
cruise = []
for line in p.open(encoding="utf-8"):
    if '"kind":"period"' not in line:
        continue
    r = json.loads(line)
    if r.get("kind") != "period":
        continue
    if float(r.get("movement_speed") or 0) > 2:
        cruise.append(r)
print("cruise", len(cruise))
keys = [
    "wall_ms",
    "scene_ms",
    "mesh_emerge_ms",
    "stream_ms",
    "world_streaming_phase_ms",
    "scene_opaque_draw_ms",
    "scene_opaque_cull_ms",
    "scene_transparent_ms",
    "scene_opaque_refresh_ms",
    "prep_schedule_policy_ms",
    "mesh_dirty_schedule_ms",
    "mesh_emerge_prep_ms",
    "mesh_dirty_tick_ms",
    "prep_refresh_pressure_ms",
    "mesh_gpu_finish_ms",
    "async_io_ms",
    "relight_drain_ms",
    "fluid_map_cpu_ms",
    "app_update_ms",
    "prepare_frame_ms",
    "render_total_ms",
    "phys_ms",
    "chunk_meshed_unlit",
    "chunk_meshed_unlit_hidden",
    "chunk_meshed_unlit_preview",
    "column_meshing_n",
    "column_render_ready_n",
    "softdefer_empty_placeholder_n",
    "softdefer_held_n",
    "pending_gpu_applies_n",
    "opaque_cull_skipped",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_skip_pipeline_n",
    "abort_schedule_final",
    "phase_abort_heavy",
]
for k in keys:
    xs = [float(r.get(k) or 0) for r in cruise]
    print(f"{k:36s} med={st.median(xs):10.3f} p90={st.quantiles(xs, n=10)[8]:10.3f} max={max(xs):10.3f} nz={sum(1 for x in xs if x>0)/len(xs):.2f}")

wall = st.median([r["wall_ms"] for r in cruise])
scene = st.median([r["scene_ms"] for r in cruise])
stream = st.median([r["stream_ms"] for r in cruise])
emerge = st.median([r["mesh_emerge_ms"] for r in cruise])
print("sum", scene + stream + emerge, "wall", wall, "gap", wall - scene - stream - emerge)
