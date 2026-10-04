#!/usr/bin/env python3
"""Summarize framebuffer pixel and screen-ray witnesses from perf JSONL."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


def unpack_rgb(value: int) -> tuple[int, int, int]:
    return ((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF)


def luminance(rgb: tuple[int, int, int]) -> float:
    red, green, blue = rgb
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--threshold", type=float, default=32.0)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    probe_count = 0
    ray_states: Counter[str] = Counter()
    dark_pixels: list[dict[str, Any]] = []
    screen_rays_by_epoch: dict[int, Counter[str]] = {}
    max_oom_period: dict[str, Any] | None = None
    max_pool_period: dict[str, Any] | None = None

    with args.perf_jsonl.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue

            kind = row.get("kind")
            if kind == "renderer_pixel_probe":
                probe_count += 1
                rgba = int(row.get("renderer_pixel_rgba", 0))
                rgb = unpack_rgb(rgba)
                luma = luminance(rgb)
                ray_state = int(row.get("renderer_pixel_voxel_ray_state", 0))
                ray_states[str(ray_state)] += 1
                if luma >= args.threshold:
                    continue

                pre = unpack_rgb(int(row.get("renderer_pixel_pretransparent_rgba", 0)))
                epoch = int(row.get("frame_epoch", 0))
                record = {
                    "frame_epoch": epoch,
                    "pixel": [row.get("renderer_pixel_x"), row.get("renderer_pixel_y")],
                    "camera": [row.get("camera_x"), row.get("camera_y"), row.get("camera_z")],
                    "rgb": rgb,
                    "luminance": round(luma, 2),
                    "pretransparent_rgb": pre,
                    "transparent_changed_rgb": rgb != pre,
                    "voxel_ray_state": ray_state,
                    "voxel_ray_gap": row.get("renderer_pixel_voxel_ray_gap"),
                    "voxel_hit": [row.get("renderer_pixel_voxel_hit_x"),
                                  row.get("renderer_pixel_voxel_hit_y"),
                                  row.get("renderer_pixel_voxel_hit_z")],
                    "block_id": row.get("renderer_pixel_voxel_hit_block_id"),
                    "entry_face": row.get("renderer_pixel_voxel_entry_face"),
                    "voxel_distance": row.get("renderer_pixel_voxel_hit_distance"),
                    "depth_distance": row.get("renderer_pixel_opaque_hit_distance"),
                    "source_face_valid": row.get("renderer_pixel_voxel_face_source_valid"),
                    "gpu_face_command": row.get("renderer_pixel_voxel_face_gpu_command"),
                    "gpu_face_index_count": row.get("renderer_pixel_voxel_face_gpu_index_count"),
                    "opaque_mdi_pass_flags": row.get("renderer_pixel_opaque_mdi_visible_pass_flags"),
                    "voxel_chunk_render_flags": row.get("renderer_pixel_voxel_chunk_render_flags"),
                    "voxel_chunk_ref_flags": row.get("renderer_pixel_voxel_chunk_ref_flags"),
                    "voxel_chunk_mdi_commands": row.get("renderer_pixel_voxel_chunk_mdi_command_count"),
                    "packed_draw_selected": row.get("renderer_pixel_voxel_chunk_packed_draw_selected"),
                    "packed_draw_calls": row.get("renderer_pixel_voxel_chunk_packed_draw_call_count"),
                    "opaque_vertex_face": row.get("renderer_pixel_opaque_vertex_light_face_index"),
                    "opaque_vertex_face_distance": row.get("renderer_pixel_opaque_vertex_light_distance"),
                    "opaque_surface": [row.get("renderer_pixel_opaque_surface_x"),
                                        row.get("renderer_pixel_opaque_surface_y"),
                                        row.get("renderer_pixel_opaque_surface_z")],
                }
                dark_pixels.append(record)

            elif kind == "screen_ray_candidate_trace":
                epoch = int(row.get("frame_epoch", 0))
                counts = screen_rays_by_epoch.setdefault(epoch, Counter())
                counts["samples"] += 1
                counts["geometry_debt"] += int(row.get("screen_ray_geometry_debt", 0))
                counts["repairable_geometry_debt"] += int(
                    row.get("screen_ray_repairable_geometry_debt", 0)
                )
                counts["light_debt"] += int(row.get("screen_ray_light_debt", 0))
                counts["unloaded"] += int(row.get("screen_ray_state", 0) == 2)
                counts["candidates"] += int(row.get("screen_ray_candidate", 0))
                counts["selected"] += int(row.get("screen_ray_selected", 0))

            elif kind == "period":
                oom = int(row.get("publication_oom_retain_n", 0))
                pool_used = float(row.get("gpu_pool_used_mb", 0.0))
                pool_cap = float(row.get("gpu_pool_cap_mb", 0.0))
                if max_oom_period is None or oom > max_oom_period["publication_oom_retain_n"]:
                    max_oom_period = {
                        "player": [row.get("player_x"), row.get("player_y"), row.get("player_z")],
                        "publication_oom_retain_n": oom,
                        "gpu_pool_used_mb": pool_used,
                        "gpu_pool_cap_mb": pool_cap,
                        "pool_free_slot_n": row.get("pool_free_slot_n"),
                        "pool_retired_pending_n": row.get("pool_retired_pending_n"),
                        "gpu_mesh_slot_bound_n": row.get("gpu_mesh_slot_bound_n"),
                        "gpu_mesh_slot_max_n": row.get("gpu_mesh_slot_max_n"),
                        "focus_data_camera_band_solid_no_drawable_n": row.get(
                            "focus_data_camera_band_solid_no_drawable_n"
                        ),
                    }
                if pool_cap > 0 and (
                    max_pool_period is None
                    or pool_used / pool_cap > max_pool_period["pool_fill"]
                ):
                    max_pool_period = {
                        "player": [row.get("player_x"), row.get("player_y"), row.get("player_z")],
                        "pool_fill": round(pool_used / pool_cap, 4),
                        "gpu_pool_used_mb": pool_used,
                        "gpu_pool_cap_mb": pool_cap,
                        "publication_oom_retain_n": oom,
                    }

    for pixel in dark_pixels:
        ray_summary = screen_rays_by_epoch.get(pixel["frame_epoch"], Counter())
        pixel["same_frame_screen_rays"] = dict(ray_summary)

    report = {
        "perf_jsonl": str(args.perf_jsonl),
        "pixel_probe_count": probe_count,
        "dark_threshold_luma": args.threshold,
        "dark_pixel_count": len(dark_pixels),
        "voxel_ray_state_counts": dict(ray_states),
        "screen_ray_frame_count": len(screen_rays_by_epoch),
        "period_max_publication_oom": max_oom_period,
        "period_max_pool_fill": max_pool_period,
        "dark_pixels": dark_pixels,
        "interpretation_note": (
            "Pixel witnesses compare framebuffer color/depth with a CPU voxel ray. "
            "They localize mismatches but do not by themselves prove the responsible draw path."
        ),
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
