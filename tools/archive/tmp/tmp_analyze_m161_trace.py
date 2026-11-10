from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path


perf_path = Path("bin/logs/perf_20260929-213114_32736.jsonl")
report_path = Path(
    "bin/suite_reports/engine_refactor/relight_lifecycle_product174657_20260929m161_opaque_work_owner.json"
)
rows = [json.loads(line) for line in perf_path.read_text(encoding="utf-8").splitlines()
        if line.startswith("{")]
samples = [row for row in rows if row.get("kind") == "renderer_pixel_probe"]
report = json.loads(report_path.read_text(encoding="utf-8"))
unique = {}
for row in samples:
    unique[(row["renderer_pixel_probe_id"], row["renderer_pixel_x"],
            row["renderer_pixel_y"])] = row
samples = list(unique.values())
depth = [row for row in samples
         if row.get("renderer_pixel_opaque_surface_valid")]
matched = [row for row in depth
           if row.get("renderer_pixel_opaque_vertex_light_valid")]
live = [row for row in matched
        if row.get("renderer_pixel_opaque_live_face_light_valid")]


def packed(row):
    sky = round(row["renderer_pixel_opaque_vertex_sky_light"] * 15)
    block = round(row["renderer_pixel_opaque_vertex_block_light"] * 15)
    return (block << 4) | sky


mismatch = [row for row in live
            if packed(row) != row["renderer_pixel_opaque_live_face_light_packed"]]
stale_zero_lit = [row for row in mismatch if packed(row) == 0 and
                  row.get("renderer_pixel_opaque_field_light_rev", 0) >
                  row.get("renderer_pixel_opaque_published_light_rev", 0)]
rev_behind = [row for row in matched if
              row.get("renderer_pixel_opaque_field_light_rev", 0) >
              row.get("renderer_pixel_opaque_published_light_rev", 0)]


def distribution(rows, key):
    return dict(sorted(Counter(row.get(key, None) for row in rows).items(),
                       key=lambda item: (str(type(item[0])), str(item[0]))))


def bit_counts(rows, key, bits):
    return {name: sum(1 for row in rows if row.get(key, 0) & (1 << bit))
            for bit, name in enumerate(bits)}


print("raw", len([r for r in rows if r.get("kind") == "renderer_pixel_probe"]),
      "unique", len(samples), "probes", len({r["renderer_pixel_probe_id"] for r in samples}))
print("depth", len(depth), "matched", len(matched), "live", len(live),
      "mismatch", len(mismatch), "rev_behind", len(rev_behind),
      "stale_zero_lit", len(stale_zero_lit))
print("stale_zero_lit_queue", {
    "demand_active": distribution(stale_zero_lit, "renderer_pixel_opaque_demand_has_active_attempt"),
    "demand_settled": distribution(stale_zero_lit, "renderer_pixel_opaque_demand_has_settled_light"),
    "demand_light_rev_pair": distribution([
        dict(pair=f"{row.get('renderer_pixel_opaque_demand_desired_light_rev')}/"
                  f"{row.get('renderer_pixel_opaque_demand_published_light_rev')}")
        for row in stale_zero_lit], "pair"),
    "opaque_draw_ready": distribution(stale_zero_lit, "renderer_pixel_opaque_draw_ready"),
    "dirty_queue_kind": distribution(stale_zero_lit, "mesh_dirty_queue_kind"),
    "dirty_queue_size": distribution(stale_zero_lit, "mesh_dirty_queue_size"),
    "dirty_queue_age": distribution(stale_zero_lit, "mesh_dirty_queue_age_frames"),
    "mesh_owner_composite": distribution(stale_zero_lit, "mesh_work_owner_flags"),
    "mesh_owner_bits": bit_counts(stale_zero_lit, "mesh_work_owner_flags", [
        "dirty", "mesh_inflight", "remesh_after_apply", "gpu_pending",
        "gpu_queued", "gpu_kicked", "gpu_extract", "pending_capture"]),
    "relight_owner_composite": distribution(stale_zero_lit, "relight_owner_flags"),
    "relight_owner_bits": bit_counts(stale_zero_lit, "relight_owner_flags", [
        "pending_light", "relight_queued", "async_relight", "defer_until_lit",
        "softdefer_held", "column_lit_ready", "lit_gate_required", "repair_ticket"]),
    "relight_queue_kind": distribution(stale_zero_lit, "relight_queue_kind"),
    "relight_queue_size": distribution(stale_zero_lit, "relight_queue_size"),
    "column_flow_ticket_flags": distribution(stale_zero_lit, "column_flow_ticket_flags"),
    "column_stage": distribution(stale_zero_lit, "column_emerge_stage"),
})
chunk_rows = defaultdict(list)
for row in stale_zero_lit:
    key = (row.get("renderer_pixel_opaque_chunk_x"),
           row.get("renderer_pixel_opaque_chunk_y"),
           row.get("renderer_pixel_opaque_chunk_z"))
    chunk_rows[key].append(row)
print("stale_zero_lit_unique_chunks", len(chunk_rows))
for chunk, values in sorted(chunk_rows.items(), key=lambda item: (-len(item[1]), item[0])):
    print("chunk", chunk, "samples", len(values),
          "queue", distribution(values, "mesh_dirty_queue_kind"),
          "queue_size", distribution(values, "mesh_dirty_queue_size"),
          "mesh_owner", distribution(values, "mesh_work_owner_flags"),
          "relight_owner", distribution(values, "relight_owner_flags"),
          "relight_q", distribution(values, "relight_queue_kind"),
          "flow", distribution(values, "column_flow_ticket_flags"))
print("stale examples")
for row in stale_zero_lit[:20]:
    print({key: row.get(key) for key in (
        "frame_epoch", "renderer_pixel_probe_id", "renderer_pixel_opaque_chunk_x",
        "renderer_pixel_opaque_chunk_y", "renderer_pixel_opaque_chunk_z",
        "renderer_pixel_opaque_vertex_light_block_id",
        "renderer_pixel_opaque_vertex_light_face_index",
        "renderer_pixel_opaque_live_face_light_packed",
        "renderer_pixel_opaque_field_light_rev",
        "renderer_pixel_opaque_published_light_rev",
        "renderer_pixel_opaque_demand_desired_light_rev",
        "renderer_pixel_opaque_demand_published_light_rev",
        "renderer_pixel_opaque_demand_has_active_attempt",
        "renderer_pixel_opaque_demand_has_settled_light",
        "mesh_dirty_queue_kind", "mesh_dirty_queue_index", "mesh_dirty_queue_size",
        "mesh_dirty_queue_age_frames", "mesh_work_owner_flags", "relight_owner_flags",
        "relight_queue_kind", "relight_queue_index", "relight_queue_size",
        "column_flow_ticket_flags", "column_emerge_stage")} )
print("flight_metrics", {key: report.get("metrics", {}).get(key) for key in (
    "holes_rate", "effective_holes_blink_rate", "fly_visible_black_max",
    "wall_ms_fly_med", "effective_fps_fly", "chunk_count_end",
    "pending_light_focus_med", "cruise_fifo_dropped_delta", "cruise_false_clear_delta")})
print("flight", {key: report.get(key) for key in (
    "pass", "process_rc", "run_outcome", "periods", "steady_periods",
    "post_stop_convergence_pass")})
