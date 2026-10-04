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
    dark_light_states: Counter[str] = Counter()
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
                voxel_distance = row.get("renderer_pixel_voxel_hit_distance")
                depth_distance = row.get("renderer_pixel_opaque_hit_distance")
                distance_delta = (
                    abs(float(voxel_distance) - float(depth_distance))
                    if voxel_distance is not None
                    and depth_distance is not None
                    and float(voxel_distance) >= 0.0
                    and float(depth_distance) >= 0.0
                    else None
                )
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
                    "voxel_depth_distance_delta": (
                        round(distance_delta, 4) if distance_delta is not None else None
                    ),
                    "voxel_depth_same_surface_by_distance": (
                        distance_delta <= 0.5 if distance_delta is not None else False
                    ),
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
                    "depth_surface": {
                        "valid": row.get("renderer_pixel_opaque_surface_valid"),
                        "chunk": [row.get("renderer_pixel_opaque_chunk_x"),
                                  row.get("renderer_pixel_opaque_chunk_y"),
                                  row.get("renderer_pixel_opaque_chunk_z")],
                        "opaque_mdi_visible_pass_flags": row.get(
                            "renderer_pixel_opaque_mdi_visible_pass_flags"
                        ),
                        "opaque_mdi_visible_index_count": row.get(
                            "renderer_pixel_opaque_mdi_visible_index_count"
                        ),
                        "drawable": row.get("renderer_pixel_opaque_drawable"),
                        "draw_ready": row.get("renderer_pixel_opaque_draw_ready"),
                        "live_gpu_marker": row.get("renderer_pixel_opaque_live_gpu"),
                        "mesh_revision": row.get("renderer_pixel_opaque_mesh_revision"),
                        "published_geom_revision": row.get(
                            "renderer_pixel_opaque_published_geom_rev"
                        ),
                        "source_face": {
                            "valid": row.get("renderer_pixel_opaque_vertex_light_valid"),
                            "block_id": row.get(
                                "renderer_pixel_opaque_vertex_light_block_id"
                            ),
                            "face": row.get(
                                "renderer_pixel_opaque_vertex_light_face_index"
                            ),
                            "distance": row.get(
                                "renderer_pixel_opaque_vertex_light_distance"
                            ),
                            "sky_light": row.get("renderer_pixel_opaque_vertex_sky_light"),
                            "block_light": row.get(
                                "renderer_pixel_opaque_vertex_block_light"
                            ),
                            "light_preview": row.get(
                                "renderer_pixel_opaque_vertex_light_preview"
                            ),
                            "live_face_light_valid": row.get(
                                "renderer_pixel_opaque_live_face_light_valid"
                            ),
                            "live_face_light_source": row.get(
                                "renderer_pixel_opaque_live_face_light_source"
                            ),
                        },
                    },
                    "voxel_hit_surface": {
                        "chunk": [row.get("renderer_pixel_voxel_chunk_x"),
                                  row.get("renderer_pixel_voxel_chunk_y"),
                                  row.get("renderer_pixel_voxel_chunk_z")],
                        "mesh_revision": row.get(
                            "renderer_pixel_voxel_chunk_mesh_revision"
                        ),
                        "published_geom_revision": row.get(
                            "renderer_pixel_voxel_chunk_published_geom_rev"
                        ),
                        "render_flags": row.get(
                            "renderer_pixel_voxel_chunk_render_flags"
                        ),
                        "ref_flags": row.get("renderer_pixel_voxel_chunk_ref_flags"),
                        "mdi_visible_pass_flags": row.get(
                            "renderer_pixel_voxel_chunk_mdi_visible_pass_flags"
                        ),
                        "mdi_visible_index_count": row.get(
                            "renderer_pixel_voxel_chunk_mdi_visible_index_count"
                        ),
                        "work_owner_flags": row.get(
                            "renderer_pixel_voxel_chunk_work_owner_flags"
                        ),
                        "dirty_queue": {
                            "kind": row.get(
                                "renderer_pixel_voxel_chunk_dirty_queue_kind"
                            ),
                            "index": row.get(
                                "renderer_pixel_voxel_chunk_dirty_queue_index"
                            ),
                            "size": row.get(
                                "renderer_pixel_voxel_chunk_dirty_queue_size"
                            ),
                            "age_frames": row.get(
                                "renderer_pixel_voxel_chunk_dirty_queue_age_frames"
                            ),
                        },
                        "scheduled_this_frame": row.get(
                            "renderer_pixel_voxel_chunk_scheduled_this_frame"
                        ),
                        "demand": {
                            "present": row.get(
                                "renderer_pixel_voxel_chunk_demand_present"
                            ),
                            "active_attempt": row.get(
                                "renderer_pixel_voxel_chunk_demand_has_active_attempt"
                            ),
                            "active_stage": row.get(
                                "renderer_pixel_voxel_chunk_demand_active_stage"
                            ),
                            "desired_geom_revision": row.get(
                                "renderer_pixel_voxel_chunk_demand_desired_geom_rev"
                            ),
                        },
                    },
                    "pixel_shader": {
                        "min_ambient": row.get("renderer_pixel_shader_min_ambient"),
                        "day_factor": row.get("renderer_pixel_shader_day_factor"),
                        "night_factor": row.get("renderer_pixel_shader_night_factor"),
                        "sky_scale": row.get("renderer_pixel_shader_sky_scale"),
                        "light_debug_mode": row.get(
                            "renderer_pixel_shader_light_debug_mode"
                        ),
                    },
                    "opaque_vertex_light": {
                        "valid": row.get("renderer_pixel_opaque_vertex_light_valid"),
                        "sky": row.get("renderer_pixel_opaque_vertex_sky_light"),
                        "block": row.get("renderer_pixel_opaque_vertex_block_light"),
                        "preview": row.get(
                            "renderer_pixel_opaque_vertex_light_preview"
                        ),
                        "live_face_valid": row.get(
                            "renderer_pixel_opaque_live_face_light_valid"
                        ),
                        "live_face_source": row.get(
                            "renderer_pixel_opaque_live_face_light_source"
                        ),
                    },
                    "opaque_chunk_light": {
                        "published_revision": row.get(
                            "renderer_pixel_opaque_published_light_rev"
                        ),
                        "field_revision": row.get(
                            "renderer_pixel_opaque_field_light_rev"
                        ),
                        "demand_present": row.get(
                            "renderer_pixel_opaque_demand_present"
                        ),
                        "demand_settled": row.get(
                            "renderer_pixel_opaque_demand_has_settled_light"
                        ),
                        "demand_active_stage": row.get(
                            "renderer_pixel_opaque_demand_active_stage"
                        ),
                        "demand_desired_revision": row.get(
                            "renderer_pixel_opaque_demand_desired_light_rev"
                        ),
                        "demand_published_revision": row.get(
                            "renderer_pixel_opaque_demand_published_light_rev"
                        ),
                    },
                    "voxel_chunk_light": {
                        "pending": row.get(
                            "renderer_pixel_voxel_chunk_pending_light"
                        ),
                        "queue_kind": row.get(
                            "renderer_pixel_voxel_chunk_relight_queue_kind"
                        ),
                        "ticket_flags": row.get(
                            "renderer_pixel_voxel_chunk_flow_ticket_flags"
                        ),
                        "settled": row.get(
                            "renderer_pixel_voxel_chunk_has_settled_light"
                        ),
                        "settled_revision": row.get(
                            "renderer_pixel_voxel_chunk_settled_light_rev"
                        ),
                        "field_revision": row.get(
                            "renderer_pixel_voxel_chunk_field_light_rev"
                        ),
                    },
                }
                dark_pixels.append(record)
                light_state = {
                    "preview": record["opaque_vertex_light"]["preview"],
                    "sky": record["opaque_vertex_light"]["sky"],
                    "block": record["opaque_vertex_light"]["block"],
                    "demand_settled": record["opaque_chunk_light"][
                        "demand_settled"
                    ],
                    "voxel_pending": record["voxel_chunk_light"]["pending"],
                }
                dark_light_states[json.dumps(light_state, sort_keys=True)] += 1

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

    distance_deltas = [
        float(pixel["voxel_depth_distance_delta"])
        for pixel in dark_pixels
        if pixel["voxel_depth_distance_delta"] is not None
    ]
    dark_pixel_depth_join = {
        "depth_surface_valid_n": sum(
            int(pixel["depth_surface"]["valid"] or 0) == 1
            for pixel in dark_pixels
        ),
        "depth_surface_with_visible_mdi_pass_n": sum(
            int(pixel["depth_surface"]["opaque_mdi_visible_pass_flags"] or 0) != 0
            for pixel in dark_pixels
        ),
        "depth_source_face_within_0_1_n": sum(
            pixel["depth_surface"]["source_face"]["valid"] == 1
            and pixel["depth_surface"]["source_face"]["distance"] is not None
            and float(pixel["depth_surface"]["source_face"]["distance"]) <= 0.1
            for pixel in dark_pixels
        ),
        "voxel_depth_distance_delta_le_0_5_n": sum(
            delta <= 0.5 for delta in distance_deltas
        ),
        "voxel_depth_distance_delta_gt_2_n": sum(
            delta > 2.0 for delta in distance_deltas
        ),
    }
    report = {
        "perf_jsonl": str(args.perf_jsonl),
        "pixel_probe_count": probe_count,
        "dark_threshold_luma": args.threshold,
        "dark_pixel_count": len(dark_pixels),
        "voxel_ray_state_counts": dict(ray_states),
        "dark_pixel_light_state_counts": dict(dark_light_states),
        "screen_ray_frame_count": len(screen_rays_by_epoch),
        "dark_pixel_depth_join": dark_pixel_depth_join,
        "period_max_publication_oom": max_oom_period,
        "period_max_pool_fill": max_pool_period,
        "dark_pixels": dark_pixels,
        "interpretation_note": (
            "Voxel-ray source-face fields describe the DDA hit, which may differ from the "
            "framebuffer depth-hit surface (for example, with cutout geometry). The depth "
            "surface has separate chunk, MDI, and source-light witnesses. These joins "
            "localize mismatches but do not by themselves prove the responsible draw path."
        ),
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
