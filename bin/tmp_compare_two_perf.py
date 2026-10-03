import json
import os
import sys
from collections import Counter


def load_spikes(path):
    spikes = []
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            o = json.loads(ln)
            if o.get("kind") == "spike":
                spikes.append(o)
    return spikes


def p50(vals):
    vals = sorted(vals)
    return vals[len(vals) // 2] if vals else None


def p90(vals):
    vals = sorted(vals)
    if not vals:
        return None
    idx = min(len(vals) - 1, int(len(vals) * 0.9))
    return vals[idx]


def summarize(path):
    spikes = load_spikes(path)
    def arr(k):
        return [s.get(k, 0) for s in spikes]
    out = {
        "file": os.path.basename(path),
        "spikes": len(spikes),
        "underfeet_reason": dict(sorted(Counter(arr("underfeet_reason")).items())),
        "underfeet_stage": dict(sorted(Counter(arr("underfeet_stage")).items())),
        "underfeet_has_mesh_0": sum(1 for v in arr("underfeet_has_mesh") if v == 0),
        "underfeet_need1_mesh0": sum(1 for s in spikes if s.get("underfeet_need", 0) == 1 and s.get("underfeet_has_mesh", 0) == 0),
        "underfeet_reason7": sum(1 for v in arr("underfeet_reason") if v == 7),
        "do_move_p50": p50(arr("do_movement_ms")),
        "do_move_p90": p90(arr("do_movement_ms")),
        "phys_p50": p50(arr("phys_ms")),
        "phys_p90": p90(arr("phys_ms")),
        "stream_phase_p50": p50(arr("world_streaming_phase_ms")),
        "stream_phase_p90": p90(arr("world_streaming_phase_ms")),
        "camera_move_p50": p50(arr("camera_move_ms")),
        "camera_ground_support_p50": p50(arr("camera_ground_support_ms")),
        "camera_locomotion_p50": p50(arr("camera_locomotion_ms")),
        "camera_horiz_move_p50": p50(arr("camera_horiz_move_ms")),
        "camera_sync_p50": p50(arr("camera_sync_ms")),
        "creature_tick_p50": p50(arr("creature_tick_ms")),
        "ensure_collision_p50": p50(arr("ensure_collision_ms")),
        "physics_substeps_p50": p50(arr("physics_substeps")),
        "pending_light_p50": p50(arr("pending_light")),
        "pending_light_focus_p50": p50(arr("pending_light_focus")),
        "relight_apply_n_p50": p50(arr("relight_apply_n")),
        "relight_apply_ms_p50": p50(arr("relight_apply_ms")),
        "relight_drain_ms_p50": p50(arr("relight_drain_ms")),
        "relight_capture_ms_p50": p50(arr("relight_capture_ms")),
        "relight_fifo_n_p50": p50(arr("relight_fifo_n")),
        "relight_fifo_dropped_max": max(arr("relight_fifo_dropped")) if spikes else None,
        "mesh_immediate_ms_p50": p50(arr("mesh_immediate_ms")),
        "mesh_sync_ms_p50": p50(arr("mesh_sync_ms")),
        "mesh_dirty_tick_ms_p50": p50(arr("mesh_dirty_tick_ms")),
        "mesh_dirty_drain_ms_p50": p50(arr("mesh_dirty_drain_ms")),
        "mesh_dirty_schedule_ms_p50": p50(arr("mesh_dirty_schedule_ms")),
        "mesh_dirty_gpu_ms_p50": p50(arr("mesh_dirty_gpu_ms")),
        "mesh_dirty_sync_ms_p50": p50(arr("mesh_dirty_sync_ms")),
        "unfinished_visual_p50": p50(arr("unfinished_visual")),
        "focus_not_render_ready_p50": p50(arr("focus_not_render_ready")),
        "focus_pressure_p50": p50(arr("focus_pressure")),
        "opaque_on_p50": p50(arr("opaque_cmd_on")),
        "opaque_total_p50": p50(arr("opaque_cmd_total")),
        "opaque_draw_p50": p50(arr("opaque_draw_n")),
        "wall_p50": p50(arr("wall_ms")),
        "wall_p90": p90(arr("wall_ms")),
    }
    return out


def main():
    if len(sys.argv) != 3:
        print("usage: compare A B")
        return 2
    for path in sys.argv[1:]:
        s = summarize(path)
        print(json.dumps(s, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    raise SystemExit(main())

