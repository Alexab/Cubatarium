from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path


path = Path("bin/logs/perf_20260929-200226_40568.jsonl")
rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()
        if line.startswith("{")]
samples = [row for row in rows if row.get("kind") == "renderer_pixel_probe"]
metrics = [row for row in rows if row.get("kind") == "period"]
report = json.loads(Path(
    "bin/suite_reports/engine_refactor/relight_lifecycle_product174657_20260929m158_opaque_hit_vertex_light.json"
).read_text(encoding="utf-8"))

probe_counts = Counter(row["renderer_pixel_probe_id"] for row in samples)
coords = {}
for row in samples:
    coords.setdefault((row["renderer_pixel_probe_id"], row["renderer_pixel_x"],
                       row["renderer_pixel_y"]), row)
unique = list(coords.values())
valid_depth = [row for row in unique
               if row.get("renderer_pixel_opaque_surface_valid")]
light_matches = [row for row in valid_depth
                 if row.get("renderer_pixel_opaque_vertex_light_valid")]

def rgb(row):
    packed = int(row["renderer_pixel_rgba"])
    return ((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255)

def luma(row):
    r, g, b = rgb(row)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

dark = [row for row in unique if luma(row) < 24.0]
dark_matches = [row for row in dark
                if row.get("renderer_pixel_opaque_vertex_light_valid")]
no_depth = [row for row in unique
            if not row.get("renderer_pixel_opaque_surface_valid")]
distances = [row["renderer_pixel_opaque_vertex_light_distance"]
             for row in valid_depth
             if row.get("renderer_pixel_opaque_vertex_light_distance", -1) >= 0]
light_pairs = [(row["renderer_pixel_opaque_vertex_sky_light"],
                row["renderer_pixel_opaque_vertex_block_light"])
               for row in light_matches]
zero_light = [row for row in light_matches
              if row["renderer_pixel_opaque_vertex_sky_light"] <= 0.01
              and row["renderer_pixel_opaque_vertex_block_light"] <= 0.01]
stale_light = [row for row in light_matches
               if row.get("renderer_pixel_opaque_field_light_rev", 0) >
               row.get("renderer_pixel_opaque_published_light_rev", 0)]

print("rows", len(rows), "periods", len(metrics), "raw_pixel_samples", len(samples))
print("unique_probe_points", len(unique), "probe_count", len(probe_counts),
      "per_probe_counts", sorted(Counter(probe_counts.values()).items()))
print("probe_ids", sorted(probe_counts.items())[:4], "...", sorted(probe_counts.items())[-4:])
print("opaque_depth_hits", len(valid_depth), "depth_miss", len(no_depth),
      "light_matches", len(light_matches),
      "match_rate_of_depth", round(len(light_matches) / max(1, len(valid_depth)), 4))
print("screen_y_counts", sorted(Counter(row["renderer_pixel_y"] for row in unique).items()))
print("depth_y_counts", sorted(Counter(row["renderer_pixel_y"] for row in valid_depth).items()))
print("dark_luma_lt24", len(dark), "dark_with_light_match", len(dark_matches))
print("zero_vertex_light_matches", len(zero_light),
      "dark_zero_light", sum(1 for row in dark_matches
                             if row["renderer_pixel_opaque_vertex_sky_light"] <= .01
                             and row["renderer_pixel_opaque_vertex_block_light"] <= .01),
      "published_behind_field", len(stale_light))
if distances:
    ds = sorted(distances)
    print("match_distance_min_p50_p95_max",
          [round(ds[int(q * (len(ds) - 1))], 6) for q in (0, .5, .95, 1)])
if light_pairs:
    sky = sorted(v[0] for v in light_pairs)
    block = sorted(v[1] for v in light_pairs)
    print("interpolated_sky_min_p50_max",
          [round(sky[int(q * (len(sky) - 1))], 4) for q in (0, .5, 1)])
    print("interpolated_block_min_p50_max",
          [round(block[int(q * (len(block) - 1))], 4) for q in (0, .5, 1)])
print("dark_sample_examples")
for row in sorted(dark, key=luma)[:12]:
    print({"rgb": rgb(row), "luma": round(luma(row), 2), **{key: row.get(key) for key in (
        "renderer_pixel_probe_id", "frame_epoch", "renderer_pixel_x",
        "renderer_pixel_y", "renderer_pixel_rgba",
        "renderer_pixel_pretransparent_depth", "renderer_pixel_opaque_chunk_x",
        "renderer_pixel_opaque_chunk_y", "renderer_pixel_opaque_chunk_z",
        "renderer_pixel_opaque_ref_flags", "renderer_pixel_opaque_mdi_visible_pass_flags",
        "renderer_pixel_opaque_vertex_light_valid",
        "renderer_pixel_opaque_vertex_light_distance",
        "renderer_pixel_opaque_vertex_light_block_id",
        "renderer_pixel_opaque_vertex_light_face_index",
        "renderer_pixel_opaque_vertex_sky_light",
        "renderer_pixel_opaque_vertex_block_light",
        "renderer_pixel_opaque_published_light_rev",
        "renderer_pixel_opaque_field_light_rev")}})
print("verdict", {key: report.get(key) for key in (
    "pass", "process_rc", "run_outcome", "periods", "steady_periods",
    "west_route_coverage", "post_stop_convergence_pass")})
print("metrics", {key: report.get("metrics", {}).get(key) for key in (
    "movement_speed_fly_med", "focus_west_delta_cx", "holes_rate",
    "effective_holes_blink_rate", "fly_visible_black_max",
    "wall_ms_fly_med", "effective_fps_fly", "chunk_count_end",
    "pending_light_focus_med", "cruise_fifo_dropped_delta", "stop_not_ready_delta")})
