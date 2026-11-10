from __future__ import annotations

import json
from collections import Counter
from pathlib import Path


perf = Path("bin/logs/perf_20260929-211136_19024.jsonl")
report_path = Path(
    "bin/suite_reports/engine_refactor/relight_lifecycle_product174657_20260929m160_centered_face_light.json"
)
rows = [json.loads(line) for line in perf.read_text(encoding="utf-8").splitlines()
        if line.startswith("{")]
samples = [row for row in rows if row.get("kind") == "renderer_pixel_probe"]
report = json.loads(report_path.read_text(encoding="utf-8"))
by_gate = {}
for row in rows:
    if row.get("kind") == "view_draw_gate_trace":
        by_gate.setdefault((row.get("frame_epoch"), row.get("cx"),
                            row.get("cy"), row.get("cz")), []).append(row)
unique = {}
for row in samples:
    unique[(row["renderer_pixel_probe_id"], row["renderer_pixel_x"],
            row["renderer_pixel_y"])] = row
samples = list(unique.values())
depth = [row for row in samples
         if row.get("renderer_pixel_opaque_surface_valid")]
matched = [row for row in depth
           if row.get("renderer_pixel_opaque_vertex_light_valid")]
live_valid = [row for row in matched
              if row.get("renderer_pixel_opaque_live_face_light_valid")]


def rgb(row):
    pixel = int(row["renderer_pixel_rgba"])
    return ((pixel >> 24) & 255, (pixel >> 16) & 255, (pixel >> 8) & 255)


def luminance(row):
    r, g, b = rgb(row)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def cpu_packed(row):
    sky = round(row["renderer_pixel_opaque_vertex_sky_light"] * 15)
    block = round(row["renderer_pixel_opaque_vertex_block_light"] * 15)
    return (block << 4) | sky


light_mismatch = [row for row in live_valid
                  if cpu_packed(row) != row["renderer_pixel_opaque_live_face_light_packed"]]
revision_behind = [row for row in matched
                   if row.get("renderer_pixel_opaque_field_light_rev", 0) >
                   row.get("renderer_pixel_opaque_published_light_rev", 0)]
revision_behind_mismatch = [row for row in light_mismatch
                            if row.get("renderer_pixel_opaque_field_light_rev", 0) >
                            row.get("renderer_pixel_opaque_published_light_rev", 0)]
gate_joined = []
for row in light_mismatch:
    key = (row.get("frame_epoch"), row.get("renderer_pixel_opaque_chunk_x"),
           row.get("renderer_pixel_opaque_chunk_y"),
           row.get("renderer_pixel_opaque_chunk_z"))
    gate_joined.extend((row, gate) for gate in by_gate.get(key, []))
unsettled = [row for row in live_valid
             if row.get("renderer_pixel_opaque_demand_present")
             and row.get("renderer_pixel_opaque_demand_desired_light_rev", 0) >
             row.get("renderer_pixel_opaque_demand_published_light_rev", 0)]
dark = [row for row in samples if luminance(row) < 24]
dark_matched = [row for row in dark if row in matched]
zero_source = [row for row in matched
               if cpu_packed(row) == 0]
zero_source_live_lit = [row for row in zero_source
                        if row.get("renderer_pixel_opaque_live_face_light_valid")
                        and row.get("renderer_pixel_opaque_live_face_light_packed") != 0]
missing_demand = [row for row in matched
                  if not row.get("renderer_pixel_opaque_demand_present")]

print("raw_samples", len([row for row in rows if row.get("kind") == "renderer_pixel_probe"]),
      "unique_samples", len(samples),
      "probes", len(set(row["renderer_pixel_probe_id"] for row in samples)),
      "per_probe_counts", sorted(Counter(Counter(
          row["renderer_pixel_probe_id"] for row in samples).values()).items()))
print("opaque_depth", len(depth), "matched_cpu_triangles", len(matched),
      "live_face_light_valid", len(live_valid),
      "source_live_mismatch", len(light_mismatch),
      "mismatch_fraction", round(len(light_mismatch) / max(1, len(live_valid)), 4))
print("matched_without_demand", len(missing_demand),
      "with_outstanding_light_demand", len(unsettled),
      "mismatch_with_outstanding_demand", sum(1 for row in light_mismatch
          if row.get("renderer_pixel_opaque_demand_desired_light_rev", 0) >
          row.get("renderer_pixel_opaque_demand_published_light_rev", 0)),
      "mismatch_active_attempt", sum(1 for row in light_mismatch
          if row.get("renderer_pixel_opaque_demand_has_active_attempt")),
      "mismatch_settled_light", sum(1 for row in light_mismatch
          if row.get("renderer_pixel_opaque_demand_has_settled_light")),
      "zero_source_light", len(zero_source),
      "zero_source_but_live_face_lit", len(zero_source_live_lit),
      "published_rev_behind_field", len(revision_behind),
      "source_live_mismatch_with_rev_behind", len(revision_behind_mismatch))
print("mismatch_examples")
for row in light_mismatch[:14]:
    print({key: row.get(key) for key in (
        "renderer_pixel_probe_id", "renderer_pixel_opaque_chunk_x",
        "renderer_pixel_opaque_chunk_y", "renderer_pixel_opaque_chunk_z",
        "renderer_pixel_opaque_vertex_light_block_id",
        "renderer_pixel_opaque_vertex_light_face_index",
        "renderer_pixel_opaque_vertex_sky_light",
        "renderer_pixel_opaque_vertex_block_light",
        "renderer_pixel_opaque_live_face_light_packed",
        "renderer_pixel_opaque_live_face_light_source",
        "renderer_pixel_opaque_published_light_rev",
        "renderer_pixel_opaque_field_light_rev",
        "renderer_pixel_opaque_demand_desired_light_rev",
        "renderer_pixel_opaque_demand_published_light_rev",
        "renderer_pixel_opaque_demand_has_active_attempt",
        "renderer_pixel_opaque_demand_has_settled_light")})
print("same_frame_draw_gate_joins", len(gate_joined),
      "of_mismatch_samples", len(light_mismatch))
for pixel, gate in gate_joined[:8]:
    print("gate_join", {key: gate.get(key) for key in (
        "cx", "cy", "cz", "focus_cx", "focus_cz", "draw_gate_ready",
        "mesh_dirty_queue_kind", "mesh_dirty_queue_index",
        "mesh_dirty_queue_size", "mesh_dirty_queue_age_frames",
        "mesh_work_owner_flags", "relight_owner_flags",
        "renderer_gate_flags", "published_light_rev", "field_light_rev",
        "desired_light_rev", "demand_published_light_rev", "has_settled_light")})
print("dark_luma_lt24", len(dark), "matched", len(dark_matched))
print("dark_samples")
for row in sorted(dark, key=luminance):
    print({"rgb": rgb(row), "luma": round(luminance(row), 2), **{
        key: row.get(key) for key in (
            "renderer_pixel_probe_id", "frame_epoch", "renderer_pixel_x",
            "renderer_pixel_y", "renderer_pixel_opaque_chunk_x",
            "renderer_pixel_opaque_chunk_y", "renderer_pixel_opaque_chunk_z",
            "renderer_pixel_opaque_surface_x", "renderer_pixel_opaque_surface_y",
            "renderer_pixel_opaque_surface_z",
            "renderer_pixel_opaque_chunk_nonair", "renderer_pixel_opaque_mesh_revision",
            "renderer_pixel_opaque_published_geom_rev",
            "renderer_pixel_opaque_published_light_rev",
            "renderer_pixel_opaque_field_light_rev",
            "renderer_pixel_opaque_vertex_light_valid",
            "renderer_pixel_opaque_vertex_light_block_id",
            "renderer_pixel_opaque_vertex_light_face_index",
            "renderer_pixel_opaque_vertex_light_distance",
            "renderer_pixel_opaque_vertex_sky_light",
            "renderer_pixel_opaque_vertex_block_light",
            "renderer_pixel_opaque_live_face_light_valid",
            "renderer_pixel_opaque_live_face_light_packed",
            "renderer_pixel_opaque_live_face_light_source",
            "renderer_pixel_opaque_demand_present",
            "renderer_pixel_opaque_demand_has_active_attempt",
            "renderer_pixel_opaque_demand_has_settled_light",
            "renderer_pixel_opaque_demand_active_stage",
            "renderer_pixel_opaque_demand_desired_light_rev",
            "renderer_pixel_opaque_demand_published_light_rev",
            "renderer_pixel_opaque_demand_settled_light_rev",
            "renderer_pixel_opaque_draw_ready",
            "renderer_pixel_opaque_mdi_visible_pass_flags")}})
print("zero_source_live_lit_examples")
for row in zero_source_live_lit[:12]:
    print({key: row.get(key) for key in (
        "renderer_pixel_probe_id", "renderer_pixel_x", "renderer_pixel_y",
        "renderer_pixel_opaque_chunk_x", "renderer_pixel_opaque_chunk_y",
        "renderer_pixel_opaque_chunk_z", "renderer_pixel_opaque_vertex_light_block_id",
        "renderer_pixel_opaque_vertex_light_face_index",
        "renderer_pixel_opaque_live_face_light_packed",
        "renderer_pixel_opaque_live_face_light_source",
        "renderer_pixel_opaque_field_light_rev",
        "renderer_pixel_opaque_published_light_rev",
        "renderer_pixel_opaque_demand_desired_light_rev",
        "renderer_pixel_opaque_demand_published_light_rev",
        "renderer_pixel_opaque_demand_has_active_attempt")})
print("result", {key: report.get(key) for key in
      ("pass", "process_rc", "run_outcome", "periods", "steady_periods",
       "post_stop_convergence_pass")})
print("metrics", {key: report.get("metrics", {}).get(key) for key in
      ("holes_rate", "effective_holes_blink_rate", "fly_visible_black_max",
       "wall_ms_fly_med", "effective_fps_fly", "chunk_count_end",
       "pending_light_focus_med", "cruise_fifo_dropped_delta")})
