"""Read-only JSONL audit; output to stdout. Period means are NOT frame percentiles."""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import statistics

KEYS = """wall_ms stream_ms streamer_update_ms async_io_ms world_streaming_phase_ms
mesh_emerge_ms mesh_emerge_prep_ms prep_schedule_policy_ms prep_spawn_ring_query_ms
prep_recover_ms prep_sync_focus_ring_ms prep_refresh_pressure_ms
mesh_snapshot_ms relight_capture_ms mesh_gpu_kick_ms mesh_gpu_finish_ms
scene_ms render_total_ms scene_opaque_refresh_ms scene_opaque_cull_ms
scene_opaque_gpu_draw_ms scene_opaque_packed_ms scene_self_ms fluid_map_cpu_ms
prepare_frame_ms gui_overlay_ms gpu_cull_exec_ms pool_fence_wait_ms
near_focus_holes visual_holes unfinished_visual focus_missing_mesh
visible_black_focus_n draw_oracle_stale_vertex_light_n stale_vl_rev_n
fully_dark_census_n visible_black_fully_dark_stalled_n visible_black_legal_dark_n
dark_face_near_n dark_face_stale_near_n dark_face_void_near_n
mesh_apply_stale_visual mesh_apply_stale_geom_delta mesh_apply_stale_light_delta
mesh_apply_stale_geom_accepted_delta mesh_apply_stale_light_accepted_delta
mesh_apply_stale_accepted_refresh_delta
publication_incomplete_material_n publication_oom_retain_n publication_progress_unit_n
pubver_changed_without_fresh_n pass_mesh_rev_lag_max pass_mdi_stale_gpu_resident_n
gpu_pool_used_mb gpu_pool_cap_mb pool_free_slot_n pool_retired_pending_n
opaque_gpu_packed_n opaque_cmd_total opaque_cmd_on opaque_refs_cpu_vis
frame_deadline_remaining_ms mark_relit_schedule_n mark_relit_h2_fire_n
dirty_fm_n dirty_remesh_n mesh_dirty_schedule_ok_fm_n mesh_dirty_schedule_ok_remesh_n
pending_gpu_applies_n gpu_finish_n gpu_kick_n mesh_worker_inflight_n
oldest_stale_vertex_light_age_frames chunk_count rss_mb chunk_meshed_unlit
""".split()

def num(row, key):
    v = row.get(key)
    return float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) else None

def summarize(rows):
    stats = {}
    for key in KEYS:
        xs = [v for row in rows if (v := num(row, key)) is not None]
        if xs:
            stats[key] = {"n": len(xs), "median": statistics.median(xs), "max": max(xs), "min": min(xs)}
    return {"rows": len(rows), "represented_frames": sum(num(r, "frames") or 0 for r in rows), "stats": stats}

def analyze(path):
    payload = path.read_bytes()
    rows, bad = [], 0
    for line in payload.decode("utf-8", errors="replace").splitlines():
        try:
            row = json.loads(line)
            if isinstance(row, dict): rows.append(row)
        except json.JSONDecodeError:
            bad += 1
    periods = [r for r in rows if r.get("kind") == "period"]
    moving = [r for r in periods if 2 < (num(r, "movement_speed") or 0) < 20]
    corridor = [r for r in moving if 2 <= (num(r, "focus_cx") or 0) <= 5 and num(r, "focus_cz") == 3]
    # Same transparent diagnostic selection as Sep 14; not an acceptance gate.
    comparable = [r for r in moving if -3 <= (num(r, "focus_cx") or 0) <= 6 and (num(r, "player_y") or 0) >= 54]
    stationary = [r for r in periods if (num(r, "movement_speed") or 0) <= 2]
    domains = {k: sorted({str(r[k]) for r in periods if k in r}) for k in
               ("gl_renderer", "gl_version", "backend_mesher", "backend_store", "focus_cx", "focus_cz", "player_y")}
    out = {"path": str(path), "sha256": hashlib.sha256(payload).hexdigest(),
           "kinds": dict(collections.Counter(r.get("kind", "missing") for r in rows)),
           "invalid_lines": bad, "domains": domains,
           "segments": {name: summarize(rs) for name, rs in
                        (("all_periods", periods), ("moving_2_20", moving), ("mid_x2_5_z3_moving", corridor),
                         ("historical_selection", comparable), ("stationary_le2", stationary))}}
    out["period_timeline"] = [{k: r[k] for k in ("frames", "movement_speed", "focus_cx", "focus_cz", "player_y", "wall_ms", "stream_ms", "mesh_emerge_ms", "mesh_gpu_kick_ms", "mesh_gpu_finish_ms", "near_focus_holes", "unfinished_visual", "pass_mesh_rev_lag_max", "gpu_pool_used_mb", "pool_free_slot_n") if k in r} for r in periods]
    return out

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--output", type=Path, help="Write generated analysis JSON instead of stdout")
    args = ap.parse_args()
    result = json.dumps([analyze(p) for p in args.logs], ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(result + "\n", encoding="utf-8")
        print(f"Wrote {args.output}")
    else:
        print(result)
