from __future__ import annotations

import json
import re
from collections import Counter, defaultdict
from pathlib import Path


perf_path = Path("bin/logs/perf_20260929-215240_35980.jsonl")
report_path = Path(
    "bin/suite_reports/engine_refactor/relight_lifecycle_product174657_20260929m162_relight_handoff_audit.json"
)
log_path = Path("bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260929-215233.35980")
rows = [json.loads(line) for line in perf_path.read_text(encoding="utf-8").splitlines()
        if line.startswith("{")]
samples = [row for row in rows if row.get("kind") == "renderer_pixel_probe"]
unique = {}
for row in samples:
    unique[(row["renderer_pixel_probe_id"], row["renderer_pixel_x"],
            row["renderer_pixel_y"])] = row
samples = list(unique.values())
depth = [row for row in samples if row.get("renderer_pixel_opaque_surface_valid")]
matched = [row for row in depth if row.get("renderer_pixel_opaque_vertex_light_valid")]
live = [row for row in matched if row.get("renderer_pixel_opaque_live_face_light_valid")]


def packed(row):
    sky = round(row["renderer_pixel_opaque_vertex_sky_light"] * 15)
    block = round(row["renderer_pixel_opaque_vertex_block_light"] * 15)
    return (block << 4) | sky


stale_zero_lit = [r for r in live if packed(r) == 0 and
                  r.get("renderer_pixel_opaque_live_face_light_packed", 0) != 0 and
                  r.get("renderer_pixel_opaque_field_light_rev", 0) >
                  r.get("renderer_pixel_opaque_published_light_rev", 0)]
hit_chunks = {(r.get("renderer_pixel_opaque_chunk_x"),
               r.get("renderer_pixel_opaque_chunk_y"),
               r.get("renderer_pixel_opaque_chunk_z")) for r in stale_zero_lit}


handoffs = []
applies = []
captures = []
for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
    marker = "[RelightAudit] "
    if marker not in line:
        continue
    message = line.split(marker, 1)[1]
    if message.startswith("slice_handoff "):
        row = dict(re.findall(r"([A-Za-z_]+)=([^\s]+)", message))
        coord = re.search(r"coord=\((-?\d+),(-?\d+),(-?\d+)\)", message)
        if coord:
            row["coord"] = tuple(int(x) for x in coord.groups())
            row["message"] = message
            handoffs.append(row)
    elif message.startswith("apply job="):
        applies.append(message)
    elif message.startswith("capture submit"):
        captures.append(message)

stale_handoffs = [r for r in handoffs if int(r.get("drawable", 0)) and
                  int(r.get("satisfying", 0)) and
                  int(r.get("field_light_rev", 0)) >
                  int(r.get("meshed_light_rev", 0))]
matching_hit_handoffs = [r for r in stale_handoffs if r.get("coord") in hit_chunks]
by_chunk = defaultdict(list)
for row in stale_handoffs:
    by_chunk[row["coord"]].append(row)

print("pixel_probe", {"unique": len(samples), "depth": len(depth),
      "matched": len(matched), "live": len(live),
      "stale_zero_lit_samples": len(stale_zero_lit),
      "stale_zero_lit_chunks": len(hit_chunks)})
print("relight_log", {"handoffs": len(handoffs), "stale_drawable_satisfying_handoffs": len(stale_handoffs),
      "handoff_chunks": len(by_chunk), "stale_hit_chunks_seen": len({r['coord'] for r in matching_hit_handoffs}),
      "matching_hit_handoffs": len(matching_hit_handoffs), "apply_events": len(applies),
      "capture_submits": len(captures)})
print("stale_handoff_by_control", {
    key: dict(Counter(row.get(key, "missing") for row in stale_handoffs))
    for key in ("installed", "stale_mesh_input", "primary_source", "primary_only",
                "finalize", "draw_gate", "markrelit_input", "pending_light",
                "first_mesh_ticket", "relight_flow_ticket", "repair_ticket", "dirty")})
print("stale_handoff_owner", {
    key: dict(Counter(row.get(key, "missing") for row in stale_handoffs))
    for key in ("queue", "mesh_inflight", "raa_pending", "gpu_pending", "gpu_extract",
                "scheduled_this_frame", "defer_until_lit")})
print("matching_hit_by_control", {
    key: dict(Counter(row.get(key, "missing") for row in matching_hit_handoffs))
    for key in ("installed", "stale_mesh_input", "primary_source", "primary_only",
                "finalize", "draw_gate", "markrelit_input", "pending_light",
                "first_mesh_ticket", "relight_flow_ticket", "repair_ticket", "dirty",
                "mesh_inflight", "gpu_pending", "defer_until_lit")})
print("matching_hit_examples")
for row in matching_hit_handoffs[:10]:
    print(row["message"])
print("latest_stale_handoff_per_chunk")
for coord, values in sorted(by_chunk.items()):
    if coord not in hit_chunks:
        continue
    row = values[-1]
    print(coord, {key: row.get(key) for key in (
        "job", "installed", "stale_mesh_input", "primary_source", "primary_only",
        "finalize", "markrelit_input", "field_light_rev", "meshed_light_rev",
        "dirty", "mesh_inflight", "gpu_pending", "pending_light",
        "first_mesh_ticket", "relight_flow_ticket")})
report = json.loads(report_path.read_text(encoding="utf-8"))
print("metrics", {key: report.get("metrics", {}).get(key) for key in (
    "holes_rate", "effective_holes_blink_rate", "fly_visible_black_max",
    "wall_ms_fly_med", "effective_fps_fly", "cruise_fifo_dropped_delta",
    "cruise_false_clear_delta", "relight_apply_partial_frames")})
print("flight", {key: report.get(key) for key in ("pass", "process_rc", "run_outcome", "periods")})
