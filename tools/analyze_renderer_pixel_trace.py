#!/usr/bin/env python3
"""Summarize framebuffer pixel and screen-ray witnesses from perf JSONL."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from statistics import median
from typing import Any


def unpack_rgb(value: int) -> tuple[int, int, int]:
    return ((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF)


def luminance(rgb: tuple[int, int, int]) -> float:
    red, green, blue = rgb
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def parse_chunk_arg(value: str) -> tuple[int, int, int]:
    try:
        parts = tuple(int(part.strip()) for part in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "chunk must be three comma-separated integers: X,Y,Z"
        ) from exc
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(
            "chunk must be three comma-separated integers: X,Y,Z"
        )
    return parts


def fog_snapshot(row: dict[str, Any]) -> dict[str, Any] | None:
    if not row.get("renderer_pixel_fog_state_valid"):
        return None
    return {
        "fog_enabled": row.get("renderer_pixel_fog_enabled"),
        "air_fog_enabled": row.get("renderer_pixel_air_fog_enabled"),
        "horizontal": row.get("renderer_pixel_fog_horizontal"),
        "start": row.get("renderer_pixel_fog_start"),
        "end": row.get("renderer_pixel_fog_end"),
        "min_blend": row.get("renderer_pixel_fog_min_blend"),
        "density": row.get("renderer_pixel_fog_density"),
        "weather_multiplier": row.get("renderer_pixel_fog_env_multiplier"),
        "color": [
            row.get("renderer_pixel_fog_color_r"),
            row.get("renderer_pixel_fog_color_g"),
            row.get("renderer_pixel_fog_color_b"),
        ],
        "target_color": [
            row.get("renderer_pixel_fog_target_color_r"),
            row.get("renderer_pixel_fog_target_color_g"),
            row.get("renderer_pixel_fog_target_color_b"),
        ],
        "underwater_enabled": row.get(
            "renderer_pixel_underwater_fog_enabled"
        ),
        "underwater_submerged": row.get(
            "renderer_pixel_underwater_fog_submerged"
        ),
        "underwater_start": row.get("renderer_pixel_underwater_fog_start"),
        "underwater_end": row.get("renderer_pixel_underwater_fog_end"),
        "underwater_min_blend": row.get(
            "renderer_pixel_underwater_fog_min_blend"
        ),
    }


def changed_fog_fields(
    first: dict[str, Any] | None, second: dict[str, Any] | None
) -> list[str] | None:
    if first is None or second is None:
        return None

    changed: list[str] = []
    for key in first:
        left = first.get(key)
        right = second.get(key)
        if isinstance(left, list) and isinstance(right, list):
            differs = len(left) != len(right) or any(
                a != b
                if a is None or b is None
                else abs(float(a) - float(b)) > 0.001
                for a, b in zip(left, right)
            )
        elif isinstance(left, (int, float)) and isinstance(right, (int, float)):
            differs = abs(float(left) - float(right)) > 0.001
        else:
            differs = left != right
        if differs:
            changed.append(key)
    return changed


def fog_blend_estimate(row: dict[str, Any]) -> dict[str, float] | None:
    if not row.get("renderer_pixel_opaque_surface_valid"):
        return None

    fog_enabled = bool(row.get("renderer_pixel_fog_enabled"))
    air_fog_enabled = bool(row.get("renderer_pixel_air_fog_enabled"))
    if not fog_enabled and not air_fog_enabled:
        return None
    if row.get("renderer_pixel_underwater_fog_submerged"):
        return None

    camera = [
        row.get("renderer_pixel_camera_pos_x"),
        row.get("renderer_pixel_camera_pos_y"),
        row.get("renderer_pixel_camera_pos_z"),
    ]
    surface = [
        row.get("renderer_pixel_opaque_surface_x"),
        row.get("renderer_pixel_opaque_surface_y"),
        row.get("renderer_pixel_opaque_surface_z"),
    ]
    if any(value is None for value in (*camera, *surface)):
        return None

    dx = float(surface[0]) - float(camera[0])
    dy = float(surface[1]) - float(camera[1])
    dz = float(surface[2]) - float(camera[2])
    use_horizontal_air_distance = (
        air_fog_enabled
        and not fog_enabled
        and bool(row.get("renderer_pixel_fog_horizontal"))
    )
    distance = (
        (dx * dx + dz * dz) ** 0.5
        if use_horizontal_air_distance
        else (dx * dx + dy * dy + dz * dz) ** 0.5
    )
    fog_start = float(row.get("renderer_pixel_fog_start", 0.0))
    fog_end = float(row.get("renderer_pixel_fog_end", 1000.0))
    fog_min_blend = float(row.get("renderer_pixel_fog_min_blend", 0.0))
    fog_density = float(row.get("renderer_pixel_fog_density", 1.0))
    env_multiplier = float(row.get("renderer_pixel_fog_env_multiplier", 1.0))
    normalized = min(
        1.0,
        max(0.0, (distance - fog_start) / max(fog_end - fog_start, 0.001)),
    )
    exponent = max(fog_density * max(env_multiplier, 0.05), 0.1)
    factor = max(normalized**exponent, fog_min_blend)
    return {
        "distance": round(distance, 3),
        "factor": round(min(1.0, max(0.0, factor)), 5),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--threshold", type=float, default=32.0)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument(
        "--watch-chunk",
        action="append",
        type=parse_chunk_arg,
        default=[],
        metavar="X,Y,Z",
        help=(
            "retain all framebuffer probe observations whose opaque or voxel "
            "ray hit belongs to this chunk; repeat to watch multiple chunks"
        ),
    )
    args = parser.parse_args()

    probe_count = 0
    absolute_timestamp_probe_count = 0
    ray_states: Counter[str] = Counter()
    dark_light_states: Counter[str] = Counter()
    dark_pixels: list[dict[str, Any]] = []
    watched_chunk_samples: dict[
        tuple[int, int, int], list[dict[str, Any]]
    ] = {coord: [] for coord in args.watch_chunk}
    provisional_preview_sample_n = 0
    surface_preview_sample_n = 0
    surface_preview_seen: set[tuple[int, int, int, int, int, int]] = set()
    provisional_preview_chunks: dict[tuple[int, int, int], dict[int, dict[str, Any]]] = {}
    settled_nonpreview_chunks: dict[tuple[int, int, int], dict[int, dict[str, Any]]] = {}
    provisional_preview_seen: set[tuple[int, int, int, int, int, int]] = set()
    settled_nonpreview_seen: set[tuple[int, int, int, int, int, int]] = set()
    preview_pixel_events: dict[tuple[int, int, int], list[dict[str, Any]]] = {}
    settled_nonpreview_pixel_events: dict[
        tuple[int, int, int], list[dict[str, Any]]
    ] = {}
    demand_lifecycle_events: dict[
        tuple[int, int, int], list[dict[str, Any]]
    ] = {}
    mesh_job_stage_events: dict[
        tuple[int, int, int], list[dict[str, Any]]
    ] = {}
    opaque_unsettled_seen: set[tuple[int, int, int, int, int, int]] = set()
    screen_rays_by_epoch: dict[int, Counter[str]] = {}
    max_oom_period: dict[str, Any] | None = None
    max_pool_period: dict[str, Any] | None = None

    def sample_monotonic_ms(row: dict[str, Any]) -> float | None:
        steady_ms = row.get("renderer_pixel_sample_steady_ms")
        if steady_ms is not None:
            return float(steady_ms)
        relative_ms = row.get("renderer_pixel_sample_time_ms")
        return float(relative_ms) if relative_ms is not None else None

    with args.perf_jsonl.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue

            kind = row.get("kind")
            if kind == "renderer_pixel_probe":
                probe_count += 1
                absolute_timestamp_probe_count += int(
                    row.get("renderer_pixel_sample_steady_ms") is not None
                )
                rgba = int(row.get("renderer_pixel_rgba", 0))
                rgb = unpack_rgb(rgba)
                luma = luminance(rgb)
                ray_state = int(row.get("renderer_pixel_voxel_ray_state", 0))
                ray_states[str(ray_state)] += 1

                opaque_chunk = (
                    int(row.get("renderer_pixel_opaque_chunk_x", 0)),
                    int(row.get("renderer_pixel_opaque_chunk_y", 0)),
                    int(row.get("renderer_pixel_opaque_chunk_z", 0)),
                )
                voxel_chunk = (
                    int(row.get("renderer_pixel_voxel_chunk_x", 0)),
                    int(row.get("renderer_pixel_voxel_chunk_y", 0)),
                    int(row.get("renderer_pixel_voxel_chunk_z", 0)),
                )
                watch_matches = {
                    coord
                    for coord in args.watch_chunk
                    if (
                        row.get("renderer_pixel_opaque_surface_valid")
                        and opaque_chunk == coord
                    )
                    or (
                        ray_state == 1
                        and row.get("renderer_pixel_voxel_hit_x") is not None
                        and voxel_chunk == coord
                    )
                }
                for coord in watch_matches:
                    sample_time = sample_monotonic_ms(row)
                    field_rev = row.get("renderer_pixel_opaque_field_light_rev")
                    meshed_rev = row.get("renderer_pixel_opaque_meshed_light_rev")
                    published_rev = row.get(
                        "renderer_pixel_opaque_published_light_rev"
                    )
                    desired_geom_rev = row.get(
                        "renderer_pixel_opaque_demand_desired_geom_rev"
                    )
                    published_geom_rev = row.get(
                        "renderer_pixel_opaque_published_geom_rev"
                    )
                    watched_chunk_samples[coord].append(
                        {
                            "probe_id": int(
                                row.get("renderer_pixel_probe_id", 0)
                            ),
                            "frame_epoch": int(row.get("frame_epoch", 0)),
                            "world_epoch": row.get(
                                "renderer_pixel_opaque_world_epoch",
                                row.get("world_epoch"),
                            ),
                            "incarnation": row.get(
                                "renderer_pixel_opaque_incarnation",
                                row.get("incarnation"),
                            ),
                            "sample_time_ms": sample_time,
                            "sample_clock_aligned": row.get(
                                "renderer_pixel_sample_steady_ms"
                            )
                            is not None,
                            "pixel": [
                                int(row.get("renderer_pixel_x", 0)),
                                int(row.get("renderer_pixel_y", 0)),
                            ],
                            "camera_world_pos": [
                                row.get("renderer_pixel_camera_pos_x"),
                                row.get("renderer_pixel_camera_pos_y"),
                                row.get("renderer_pixel_camera_pos_z"),
                            ],
                            "rgb": list(rgb),
                            "luminance": round(luma, 3),
                            "fog_blend_estimate": fog_blend_estimate(row),
                            "depth_surface_chunk": (
                                list(opaque_chunk)
                                if row.get(
                                    "renderer_pixel_opaque_surface_valid"
                                )
                                else None
                            ),
                            "depth_surface_valid": bool(
                                row.get(
                                    "renderer_pixel_opaque_surface_valid"
                                )
                            ),
                            "opaque_surface_matches_watched_chunk": bool(
                                row.get(
                                    "renderer_pixel_opaque_surface_valid"
                                )
                                and opaque_chunk == coord
                            ),
                            "drawable": bool(
                                row.get("renderer_pixel_opaque_drawable")
                            ),
                            "draw_ready": bool(
                                row.get("renderer_pixel_opaque_draw_ready")
                            ),
                            "mdi_visible_pass_flags": row.get(
                                "renderer_pixel_opaque_mdi_visible_pass_flags"
                            ),
                            "mdi_visible_index_count": row.get(
                                "renderer_pixel_opaque_mdi_visible_index_count"
                            ),
                            "mesh_revision": row.get(
                                "renderer_pixel_opaque_mesh_revision"
                            ),
                            "desired_geometry_revision": desired_geom_rev,
                            "published_geometry_revision": published_geom_rev,
                            "geometry_revision_match": (
                                desired_geom_rev is not None
                                and published_geom_rev is not None
                                and int(desired_geom_rev) == int(published_geom_rev)
                            ),
                            "field_light_revision": field_rev,
                            "meshed_light_revision": meshed_rev,
                            "published_light_revision": published_rev,
                            "light_revisions_match": (
                                field_rev is not None
                                and meshed_rev is not None
                                and published_rev is not None
                                and int(field_rev) == int(meshed_rev)
                                and int(field_rev) == int(published_rev)
                            ),
                            "demand_settled": bool(
                                row.get(
                                    "renderer_pixel_opaque_demand_has_settled_light"
                                )
                            ),
                            "light_preview": bool(
                                row.get(
                                    "renderer_pixel_opaque_vertex_light_preview"
                                )
                                or row.get(
                                    "renderer_pixel_opaque_preview_shader_override"
                                )
                                or row.get(
                                    "renderer_pixel_surface_preview_shader_override"
                                )
                            ),
                            "stale_light_remesh_pending": bool(
                                row.get(
                                    "renderer_pixel_opaque_stale_light_remesh_pending"
                                )
                            ),
                            "mesh_work_owner_flags": row.get(
                                "renderer_pixel_opaque_mesh_work_owner_flags"
                            ),
                            "relight_owner_flags": row.get(
                                "renderer_pixel_opaque_relight_owner_flags"
                            ),
                            "voxel_ray_state": ray_state,
                            "voxel_ray_chunk": list(voxel_chunk)
                            if ray_state == 1
                            and row.get("renderer_pixel_voxel_hit_x")
                            is not None
                            else None,
                            "voxel_hit_block": [
                                row.get("renderer_pixel_voxel_hit_x"),
                                row.get("renderer_pixel_voxel_hit_y"),
                                row.get("renderer_pixel_voxel_hit_z"),
                            ]
                            if row.get("renderer_pixel_voxel_hit_x")
                            is not None
                            else None,
                        }
                    )

                if (
                    row.get("renderer_pixel_opaque_surface_valid")
                    and row.get("renderer_pixel_opaque_vertex_light_valid")
                ):
                    chunk = (
                        int(row.get("renderer_pixel_opaque_chunk_x", 0)),
                        int(row.get("renderer_pixel_opaque_chunk_y", 0)),
                        int(row.get("renderer_pixel_opaque_chunk_z", 0)),
                    )
                    probe_id = int(row.get("renderer_pixel_probe_id", 0))
                    pixel_x = int(row.get("renderer_pixel_x", 0))
                    pixel_y = int(row.get("renderer_pixel_y", 0))
                    capture_key = (probe_id, pixel_x, pixel_y, *chunk)
                    vertex_preview = bool(
                        row.get("renderer_pixel_opaque_vertex_light_preview", 0)
                    )
                    trace_schema = int(
                        row.get("renderer_pixel_light_trace_schema", 0)
                    )
                    shader_override_value = (
                        row.get("renderer_pixel_opaque_preview_shader_override")
                        if trace_schema >= 2
                        else None
                    )
                    shader_override = (
                        bool(shader_override_value)
                        if shader_override_value is not None
                        else False
                    )
                    preview_active = vertex_preview or shader_override
                    surface_override = bool(
                        row.get("renderer_pixel_surface_preview_shader_override", 0)
                    )
                    demand_present = bool(
                        row.get("renderer_pixel_opaque_demand_present", 0)
                    )
                    demand_settled = bool(
                        row.get("renderer_pixel_opaque_demand_has_settled_light", 0)
                    )
                    opaque_world_epoch = row.get(
                        "renderer_pixel_opaque_world_epoch"
                    )
                    opaque_incarnation = row.get(
                        "renderer_pixel_opaque_incarnation"
                    )
                    if not opaque_world_epoch:
                        opaque_world_epoch = row.get("world_epoch")
                    if not opaque_incarnation:
                        opaque_incarnation = row.get("incarnation")

                    if demand_present and not demand_settled:
                        opaque_unsettled_seen.add(capture_key)
                    if (
                        surface_override
                        and capture_key not in surface_preview_seen
                    ):
                        surface_preview_seen.add(capture_key)
                        surface_preview_sample_n += 1

                    if (
                        preview_active
                        and capture_key not in provisional_preview_seen
                    ):
                        provisional_preview_seen.add(capture_key)
                        provisional_preview_sample_n += 1
                        camera_x = row.get("camera_x")
                        camera_y = row.get("camera_y")
                        camera_z = row.get("camera_z")
                        preview_pixel_events.setdefault(chunk, []).append(
                            {
                                "probe_id": probe_id,
                                "sample_time_ms": sample_monotonic_ms(row),
                                "sample_clock_aligned": (
                                    row.get("renderer_pixel_sample_steady_ms")
                                    is not None
                                ),
                                "world_epoch": opaque_world_epoch,
                                "incarnation": opaque_incarnation,
                                "camera_world_pos": [
                                    row.get("renderer_pixel_camera_pos_x"),
                                    row.get("renderer_pixel_camera_pos_y"),
                                    row.get("renderer_pixel_camera_pos_z"),
                                ],
                                "pixel": [pixel_x, pixel_y],
                                "rgb": rgb,
                                "luminance": luma,
                                "fog": fog_snapshot(row),
                                "fog_blend_estimate": fog_blend_estimate(row),
                                "demand_present": demand_present,
                                "demand_settled": demand_settled,
                                "demand_active_attempt": bool(
                                    row.get(
                                        "renderer_pixel_opaque_demand_has_active_attempt",
                                        0,
                                    )
                                ),
                                "demand_attempt_age_ms": row.get(
                                    "renderer_pixel_opaque_demand_attempt_age_ms"
                                ),
                                "demand_progress_age_ms": row.get(
                                    "renderer_pixel_opaque_demand_progress_age_ms"
                                ),
                                "field_light_revision": row.get(
                                    "renderer_pixel_opaque_field_light_rev"
                                ),
                                "published_geometry_revision": row.get(
                                    "renderer_pixel_opaque_published_geom_rev"
                                ),
                                "mesh_revision": row.get(
                                    "renderer_pixel_opaque_mesh_revision"
                                ),
                                "mdi_visible_pass_flags": row.get(
                                    "renderer_pixel_opaque_mdi_visible_pass_flags"
                                ),
                                "mdi_visible_index_count": row.get(
                                    "renderer_pixel_opaque_mdi_visible_index_count"
                                ),
                                "meshed_light_revision": row.get(
                                    "renderer_pixel_opaque_meshed_light_rev"
                                ),
                                "published_light_revision": row.get(
                                    "renderer_pixel_opaque_published_light_rev"
                                ),
                                "stale_light_remesh_pending": bool(
                                    row.get(
                                        "renderer_pixel_opaque_stale_light_remesh_pending",
                                        0,
                                    )
                                ),
                                "mesh_dirty_queue_kind": row.get(
                                    "renderer_pixel_opaque_mesh_dirty_queue_kind"
                                ),
                                "relight_queue_kind": row.get(
                                    "renderer_pixel_opaque_relight_queue_kind"
                                ),
                                "mesh_work_owner_flags": row.get(
                                    "renderer_pixel_opaque_mesh_work_owner_flags"
                                ),
                                "relight_owner_flags": row.get(
                                    "renderer_pixel_opaque_relight_owner_flags"
                                ),
                                "column_flow_ticket_flags": row.get(
                                    "renderer_pixel_opaque_column_flow_ticket_flags"
                                ),
                            }
                        )
                        capture = provisional_preview_chunks.setdefault(
                            chunk, {}
                        ).setdefault(
                            probe_id,
                            {
                                "probe_id": probe_id,
                                "frame_epoch": int(row.get("frame_epoch", 0)),
                                "sample_time_ms": sample_monotonic_ms(row),
                                "camera_world_pos": [
                                    row.get("renderer_pixel_camera_pos_x"),
                                    row.get("renderer_pixel_camera_pos_y"),
                                    row.get("renderer_pixel_camera_pos_z"),
                                ],
                                "camera": [camera_x, camera_y, camera_z],
                                "preview_pixel_count": 0,
                                "demand_settled_preview_pixel_count": 0,
                                "settled_nonpreview_pixel_count": 0,
                            },
                        )
                        capture["preview_pixel_count"] += 1
                        capture["shader_override_pixel_count"] = capture.get(
                            "shader_override_pixel_count", 0
                        ) + int(shader_override)
                        capture["vertex_marker_pixel_count"] = capture.get(
                            "vertex_marker_pixel_count", 0
                        ) + int(vertex_preview)
                        capture["demand_unsettled_pixel_count"] = capture.get(
                            "demand_unsettled_pixel_count", 0
                        ) + int(demand_present and not demand_settled)
                        capture["demand_settled_preview_pixel_count"] = (
                            capture.get("demand_settled_preview_pixel_count", 0)
                            + int(demand_present and demand_settled)
                        )
                        capture["sample"] = {
                            "pixel": [pixel_x, pixel_y],
                            "rgb": rgb,
                            "sky_light": row.get(
                                "renderer_pixel_opaque_vertex_sky_light"
                            ),
                            "block_light": row.get(
                                "renderer_pixel_opaque_vertex_block_light"
                            ),
                            "live_face_light_packed": row.get(
                                "renderer_pixel_opaque_live_face_light_packed"
                            ),
                            "published_geometry_revision": row.get(
                                "renderer_pixel_opaque_published_geom_rev"
                            ),
                            "mesh_revision": row.get(
                                "renderer_pixel_opaque_mesh_revision"
                            ),
                            "mdi_visible_pass_flags": row.get(
                                "renderer_pixel_opaque_mdi_visible_pass_flags"
                            ),
                            "mdi_visible_index_count": row.get(
                                "renderer_pixel_opaque_mdi_visible_index_count"
                            ),
                            "published_light_revision": row.get(
                                "renderer_pixel_opaque_published_light_rev"
                            ),
                            "meshed_light_revision": row.get(
                                "renderer_pixel_opaque_meshed_light_rev"
                            ),
                            "field_light_revision": row.get(
                                "renderer_pixel_opaque_field_light_rev"
                            ),
                            "demand_settled": demand_settled,
                            "demand_active_attempt": bool(
                                row.get(
                                    "renderer_pixel_opaque_demand_has_active_attempt",
                                    0,
                                )
                            ),
                            "demand_attempt_age_ms": row.get(
                                "renderer_pixel_opaque_demand_attempt_age_ms"
                            ),
                            "demand_progress_age_ms": row.get(
                                "renderer_pixel_opaque_demand_progress_age_ms"
                            ),
                            "mesh_work_owner_flags": row.get(
                                "renderer_pixel_opaque_mesh_work_owner_flags"
                            ),
                            "column_flow_ticket_flags": row.get(
                                "renderer_pixel_opaque_column_flow_ticket_flags"
                            ),
                            "relight_owner_flags": row.get(
                                "renderer_pixel_opaque_relight_owner_flags"
                            ),
                            "stale_light_remesh_pending": bool(
                                row.get(
                                    "renderer_pixel_opaque_stale_light_remesh_pending",
                                    0,
                                )
                            ),
                            "mesh_dirty_queue_kind": row.get(
                                "renderer_pixel_opaque_mesh_dirty_queue_kind"
                            ),
                            "relight_queue_kind": row.get(
                                "renderer_pixel_opaque_relight_queue_kind"
                            ),
                        }

                    elif (
                        not preview_active
                        and demand_present
                        and demand_settled
                    ):
                        if capture_key not in settled_nonpreview_seen:
                            settled_nonpreview_seen.add(capture_key)
                            settled_nonpreview_pixel_events.setdefault(
                                chunk, []
                            ).append(
                                {
                                    "probe_id": probe_id,
                                    "sample_time_ms": sample_monotonic_ms(row),
                                    "sample_clock_aligned": (
                                        row.get(
                                            "renderer_pixel_sample_steady_ms"
                                        )
                                        is not None
                                    ),
                                    "world_epoch": opaque_world_epoch,
                                    "incarnation": opaque_incarnation,
                                    "camera_world_pos": [
                                        row.get(
                                            "renderer_pixel_camera_pos_x"
                                        ),
                                        row.get(
                                            "renderer_pixel_camera_pos_y"
                                        ),
                                        row.get(
                                            "renderer_pixel_camera_pos_z"
                                        ),
                                    ],
                                    "pixel": [pixel_x, pixel_y],
                                    "rgb": rgb,
                                    "luminance": luma,
                                    "fog": fog_snapshot(row),
                                    "fog_blend_estimate": fog_blend_estimate(row),
                                    "demand_present": demand_present,
                                    "demand_settled": demand_settled,
                                    "demand_active_attempt": bool(
                                        row.get(
                                            "renderer_pixel_opaque_demand_has_active_attempt",
                                            0,
                                        )
                                    ),
                                    "demand_attempt_age_ms": row.get(
                                        "renderer_pixel_opaque_demand_attempt_age_ms"
                                    ),
                                    "demand_progress_age_ms": row.get(
                                        "renderer_pixel_opaque_demand_progress_age_ms"
                                    ),
                                    "field_light_revision": row.get(
                                        "renderer_pixel_opaque_field_light_rev"
                                    ),
                                    "meshed_light_revision": row.get(
                                        "renderer_pixel_opaque_meshed_light_rev"
                                    ),
                                    "published_light_revision": row.get(
                                        "renderer_pixel_opaque_published_light_rev"
                                    ),
                                    "published_geometry_revision": row.get(
                                        "renderer_pixel_opaque_published_geom_rev"
                                    ),
                                    "mesh_revision": row.get(
                                        "renderer_pixel_opaque_mesh_revision"
                                    ),
                                    "mdi_visible_pass_flags": row.get(
                                        "renderer_pixel_opaque_mdi_visible_pass_flags"
                                    ),
                                    "mdi_visible_index_count": row.get(
                                        "renderer_pixel_opaque_mdi_visible_index_count"
                                    ),
                                    "stale_light_remesh_pending": bool(
                                        row.get(
                                            "renderer_pixel_opaque_stale_light_remesh_pending",
                                            0,
                                        )
                                    ),
                                    "mesh_dirty_queue_kind": row.get(
                                        "renderer_pixel_opaque_mesh_dirty_queue_kind"
                                    ),
                                    "relight_queue_kind": row.get(
                                        "renderer_pixel_opaque_relight_queue_kind"
                                    ),
                                    "mesh_work_owner_flags": row.get(
                                        "renderer_pixel_opaque_mesh_work_owner_flags"
                                    ),
                                    "relight_owner_flags": row.get(
                                        "renderer_pixel_opaque_relight_owner_flags"
                                    ),
                                    "column_flow_ticket_flags": row.get(
                                        "renderer_pixel_opaque_column_flow_ticket_flags"
                                    ),
                                }
                            )
                        settled_capture = settled_nonpreview_chunks.setdefault(
                            chunk, {}
                        ).setdefault(
                            probe_id,
                            {
                                "probe_id": probe_id,
                                "frame_epoch": int(row.get("frame_epoch", 0)),
                                "sample_time_ms": sample_monotonic_ms(row),
                                "camera_world_pos": [
                                    row.get("renderer_pixel_camera_pos_x"),
                                    row.get("renderer_pixel_camera_pos_y"),
                                    row.get("renderer_pixel_camera_pos_z"),
                                ],
                                "camera": [
                                    row.get("camera_x"),
                                    row.get("camera_y"),
                                    row.get("camera_z"),
                                ],
                                "settled_nonpreview_pixel_count": 0,
                            },
                        )
                        settled_capture["settled_nonpreview_pixel_count"] += 1
                        settled_capture["settled_sample"] = {
                            "pixel": [pixel_x, pixel_y],
                            "rgb": rgb,
                            "sky_light": row.get(
                                "renderer_pixel_opaque_vertex_sky_light"
                            ),
                            "block_light": row.get(
                                "renderer_pixel_opaque_vertex_block_light"
                            ),
                            "live_face_light_packed": row.get(
                                "renderer_pixel_opaque_live_face_light_packed"
                            ),
                            "published_geometry_revision": row.get(
                                "renderer_pixel_opaque_published_geom_rev"
                            ),
                            "mesh_revision": row.get(
                                "renderer_pixel_opaque_mesh_revision"
                            ),
                            "mdi_visible_pass_flags": row.get(
                                "renderer_pixel_opaque_mdi_visible_pass_flags"
                            ),
                            "mdi_visible_index_count": row.get(
                                "renderer_pixel_opaque_mdi_visible_index_count"
                            ),
                            "meshed_light_revision": row.get(
                                "renderer_pixel_opaque_meshed_light_rev"
                            ),
                        }
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
                    "fog": fog_snapshot(row),
                    "fog_blend_estimate": fog_blend_estimate(row),
                    "pretransparent_rgb": pre,
                    "transparent_changed_rgb": rgb != pre,
                    "surface_preview_shader_override": row.get(
                        "renderer_pixel_surface_preview_shader_override"
                    ),
                    "opaque_preview_shader_override": row.get(
                        "renderer_pixel_opaque_preview_shader_override"
                    ),
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
                        "work_owner_flags": row.get(
                            "renderer_pixel_opaque_mesh_work_owner_flags"
                        ),
                        "dirty_queue": {
                            "kind": row.get(
                                "renderer_pixel_opaque_mesh_dirty_queue_kind"
                            ),
                            "index": row.get(
                                "renderer_pixel_opaque_mesh_dirty_queue_index"
                            ),
                            "size": row.get(
                                "renderer_pixel_opaque_mesh_dirty_queue_size"
                            ),
                            "age_frames": row.get(
                                "renderer_pixel_opaque_mesh_dirty_queue_age_frames"
                            ),
                        },
                        "relight_owner_flags": row.get(
                            "renderer_pixel_opaque_relight_owner_flags"
                        ),
                        "relight_queue": {
                            "kind": row.get(
                                "renderer_pixel_opaque_relight_queue_kind"
                            ),
                            "index": row.get(
                                "renderer_pixel_opaque_relight_queue_index"
                            ),
                            "size": row.get(
                                "renderer_pixel_opaque_relight_queue_size"
                            ),
                        },
                        "column_emerge_stage": row.get(
                            "renderer_pixel_opaque_column_emerge_stage"
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
                        "meshed_revision": row.get(
                            "renderer_pixel_opaque_meshed_light_rev"
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
                        "demand_desired_geom_revision": row.get(
                            "renderer_pixel_opaque_demand_desired_geom_rev"
                        ),
                        "demand_published_geom_revision": row.get(
                            "renderer_pixel_opaque_demand_published_geom_rev"
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

            elif kind == "demand_transition_trace":
                coord = (
                    int(row.get("cx", 0)),
                    int(row.get("cy", 0)),
                    int(row.get("cz", 0)),
                )
                event_ms = row.get("event_ms")
                if event_ms is not None:
                    demand_lifecycle_events.setdefault(coord, []).append(
                        {
                            "event": row.get("event"),
                            "event_ms": float(event_ms),
                            "world_epoch": row.get("world_epoch"),
                            "incarnation": row.get("incarnation"),
                            "attempt_id": row.get("attempt_id"),
                            "settled_light_rev": row.get("settled_light_rev"),
                            "has_settled_light": row.get("has_settled_light"),
                            "mesh_dirty_queue_kind": row.get(
                                "mesh_dirty_queue_kind"
                            ),
                        }
                    )

            elif kind == "visual_lifecycle_trace":
                created_ms = row.get("created_ms")
                elapsed_ms = row.get("elapsed_ms")
                if created_ms is not None and elapsed_ms is not None:
                    coord = (
                        int(row.get("cx", 0)),
                        int(row.get("cy", 0)),
                        int(row.get("cz", 0)),
                    )
                    mesh_job_stage_events.setdefault(coord, []).append(
                        {
                            "event": f"mesh_job_{row.get('stage', 'unknown')}",
                            "event_ms": float(created_ms) + float(elapsed_ms),
                            "world_epoch": row.get("world_epoch"),
                            "incarnation": row.get("incarnation"),
                            "job_id": row.get("job_id"),
                            "attempt_id": row.get("attempt_id"),
                            "source_geom_rev": row.get("source_geom_rev"),
                            "published_geom_rev": row.get(
                                "published_geom_rev"
                            ),
                            "source_light_rev": row.get("source_light_rev"),
                            "published_light_rev": row.get(
                                "published_light_rev"
                            ),
                            "terminal_reason": row.get("terminal_reason"),
                        }
                    )

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

    preview_chunk_rows: list[dict[str, Any]] = []
    for chunk, captures_by_probe in sorted(provisional_preview_chunks.items()):
        captures = sorted(captures_by_probe.values(), key=lambda item: item["probe_id"])
        preview_captures = [
            item for item in captures if item["preview_pixel_count"] > 0
        ]
        if not preview_captures:
            continue
        first_preview = preview_captures[0]
        last_preview = preview_captures[-1]
        settled_after = next(
            (
                item
                for item in sorted(
                    settled_nonpreview_chunks.get(chunk, {}).values(),
                    key=lambda capture: capture["probe_id"],
                )
                if item["probe_id"] > last_preview["probe_id"]
                and item["settled_nonpreview_pixel_count"] > 0
            ),
            None,
        )
        last_preview_time_ms = last_preview.get("sample_time_ms")
        settled_time_ms = (
            settled_after.get("sample_time_ms") if settled_after else None
        )
        elapsed_ms_to_settled = (
            round(float(settled_time_ms) - float(last_preview_time_ms), 3)
            if last_preview_time_ms is not None
            and settled_time_ms is not None
            and float(settled_time_ms) >= float(last_preview_time_ms)
            else None
        )
        first_preview_time_ms = first_preview.get("sample_time_ms")
        first_to_settled_ms = (
            round(float(settled_time_ms) - float(first_preview_time_ms), 3)
            if first_preview_time_ms is not None
            and settled_time_ms is not None
            and float(settled_time_ms) >= float(first_preview_time_ms)
            else None
        )
        first_camera = first_preview.get("camera_world_pos")
        settled_camera = (
            settled_after.get("camera_world_pos") if settled_after else None
        )
        same_camera = bool(
            first_camera
            and settled_camera
            and all(value is not None for value in first_camera + settled_camera)
            and all(
                abs(float(a) - float(b)) <= 0.01
                for a, b in zip(first_camera, settled_camera)
            )
        )
        same_pixel = bool(
            first_preview.get("sample", {}).get("pixel")
            == (settled_after or {}).get("settled_sample", {}).get("pixel")
        )
        same_view = same_camera and same_pixel

        preview_chunk_rows.append(
            {
                "chunk": list(chunk),
                "preview_capture_count": len(preview_captures),
                "preview_pixel_count": sum(
                    item["preview_pixel_count"] for item in preview_captures
                ),
                "shader_override_pixel_count": sum(
                    item.get("shader_override_pixel_count", 0)
                    for item in preview_captures
                ),
                "vertex_marker_pixel_count": sum(
                    item.get("vertex_marker_pixel_count", 0)
                    for item in preview_captures
                ),
                "demand_unsettled_pixel_count": sum(
                    item.get("demand_unsettled_pixel_count", 0)
                    for item in preview_captures
                ),
                "demand_settled_preview_pixel_count": sum(
                    item.get("demand_settled_preview_pixel_count", 0)
                    for item in preview_captures
                ),
                "first_preview": first_preview,
                "last_preview": last_preview,
                "first_settled_nonpreview_after": settled_after,
                "observed_preview_to_settled_interval_ms": elapsed_ms_to_settled,
                "observed_first_preview_to_settled_ms": first_to_settled_ms,
                "settled_sample_same_view_as_first_preview": same_view,
                "same_view_unsettled_observed_ms": (
                    first_to_settled_ms if same_view else None
                ),
                "probe_id_delta_to_settled": (
                    settled_after["probe_id"] - last_preview["probe_id"]
                    if settled_after
                    else None
                ),
            }
        )

    def same_camera_position(left: dict[str, Any], right: dict[str, Any]) -> bool:
        left_camera = left.get("camera_world_pos")
        right_camera = right.get("camera_world_pos")
        return bool(
            left_camera
            and right_camera
            and len(left_camera) == len(right_camera)
            and all(value is not None for value in left_camera + right_camera)
            and all(
                abs(float(a) - float(b)) <= 0.01
                for a, b in zip(left_camera, right_camera)
            )
        )

    def interval_ms(start: dict[str, Any], end: dict[str, Any]) -> float | None:
        start_ms = start.get("sample_time_ms")
        end_ms = end.get("sample_time_ms")
        if (
            start_ms is None
            or end_ms is None
            or float(end_ms) < float(start_ms)
        ):
            return None
        return round(float(end_ms) - float(start_ms), 3)

    def causal_lifecycle_events(
        events: list[dict[str, Any]],
        start_ms: float,
        end_ms: float,
        fallback_source_light_rev: int | None = None,
        fallback_min_geom_rev: int | None = None,
    ) -> tuple[list[dict[str, Any]], int | None, int | None]:
        """Keep one demand attempt and its mesh job from a chunk time window.

        A chunk incarnation can go through several mesh attempts while a
        provisional sample remains visible. Joining by chunk/incarnation and
        time alone can pair a light-settled event with an unrelated mesh job.
        """
        window = sorted(
            (
                event
                for event in events
                if start_ms <= float(event["event_ms"]) <= end_ms
            ),
            key=lambda event: float(event["event_ms"]),
        )

        def event_id(event: dict[str, Any], name: str) -> int:
            try:
                return int(event.get(name) or 0)
            except (TypeError, ValueError):
                return 0

        settlement = next(
            (event for event in window if event.get("event") == "light_calculation_settled"),
            None,
        )
        anchor_ms = (
            float(settlement["event_ms"])
            if settlement is not None
            else start_ms
        )
        attempt_id = event_id(settlement, "attempt_id") if settlement else 0
        if attempt_id <= 0:
            owners = [
                event
                for event in window
                if float(event["event_ms"]) >= anchor_ms
                and event.get("event")
                in {
                    "stale_light_remesh_queued",
                    "stale_light_remesh_dirty_admitted",
                    "mesh_job_started",
                    "install_published",
                }
                and event_id(event, "attempt_id") > 0
            ]
            if owners:
                attempt_id = event_id(owners[0], "attempt_id")

        if attempt_id <= 0:
            # Some legitimate first-mesh jobs precede creation of a render-
            # demand attempt and therefore carry attempt_id=0. If the watched
            # chunk incarnation has exactly one post-preview job at the
            # sampled light revision (and no older geometry source), retain
            # that job by its stable job identity instead of dropping the
            # lifecycle altogether.
            fallback_jobs: dict[int, dict[str, Any]] = {}
            for event in window:
                if event.get("event") != "mesh_job_started":
                    continue
                if event_id(event, "attempt_id") > 0:
                    continue
                if float(event["event_ms"]) < anchor_ms:
                    continue
                job_id = event_id(event, "job_id")
                if job_id <= 0:
                    continue
                if fallback_source_light_rev is not None:
                    try:
                        source_light_rev = int(event.get("source_light_rev"))
                    except (TypeError, ValueError):
                        continue
                    if source_light_rev != int(fallback_source_light_rev):
                        continue
                if fallback_min_geom_rev is not None:
                    try:
                        source_geom_rev = int(event.get("source_geom_rev"))
                    except (TypeError, ValueError):
                        continue
                    if source_geom_rev < int(fallback_min_geom_rev):
                        continue
                fallback_jobs.setdefault(job_id, event)
            if len(fallback_jobs) == 1:
                fallback_job_id = next(iter(fallback_jobs))
                fallback_events = [
                    event
                    for event in window
                    if event_id(event, "job_id") == fallback_job_id
                    and str(event.get("event", "")).startswith("mesh_job_")
                ]
                fallback_events.sort(
                    key=lambda event: float(event["event_ms"])
                )
                return fallback_events, None, fallback_job_id
            # A light-settled sample can be useful even if no subsequent mesh
            # attempt was observed in the bounded trace window.
            return ([settlement] if settlement is not None else []), None, None

        job_starts = [
            event
            for event in window
            if event.get("event") == "mesh_job_started"
            and event_id(event, "attempt_id") == attempt_id
            and float(event["event_ms"]) >= anchor_ms
        ]
        if not job_starts:
            job_starts = [
                event
                for event in window
                if event.get("event") == "mesh_job_started"
                and event_id(event, "attempt_id") == attempt_id
            ]
        job_id = event_id(job_starts[0], "job_id") if job_starts else 0

        selected = []
        for event in window:
            if event is settlement:
                selected.append(event)
                continue
            if event_id(event, "attempt_id") != attempt_id:
                continue
            if str(event.get("event", "")).startswith("mesh_job_"):
                if job_id <= 0 or event_id(event, "job_id") != job_id:
                    continue
            selected.append(event)
        selected.sort(key=lambda event: float(event["event_ms"]))
        return selected, attempt_id, (job_id if job_id > 0 else None)

    lifecycle_by_chunk: list[dict[str, Any]] = []
    lifecycle_event_timeline_by_chunk: list[dict[str, Any]] = []
    all_preview_to_nonpreview_intervals: list[float] = []
    all_light_intervals: list[float] = []
    all_handoff_intervals: list[float] = []
    for chunk in sorted(preview_pixel_events):
        events_by_pixel: dict[
            tuple[int, int], dict[str, list[dict[str, Any]]]
        ] = {}
        for event in preview_pixel_events[chunk]:
            pixel = tuple(event["pixel"])
            events_by_pixel.setdefault(
                pixel, {"preview": [], "settled_nonpreview": []}
            )["preview"].append(event)
        for event in settled_nonpreview_pixel_events.get(chunk, []):
            pixel = tuple(event["pixel"])
            events_by_pixel.setdefault(
                pixel, {"preview": [], "settled_nonpreview": []}
            )["settled_nonpreview"].append(event)

        light_intervals: list[float] = []
        handoff_intervals: list[float] = []
        full_preview_intervals: list[float] = []
        pixel_lifecycle_rows: list[dict[str, Any]] = []
        light_view_candidates = 0
        handoff_view_candidates = 0
        full_view_candidates = 0
        for pixel_events in events_by_pixel.values():
            previews = sorted(
                pixel_events["preview"], key=lambda item: item["probe_id"]
            )
            nonpreviews = sorted(
                pixel_events["settled_nonpreview"],
                key=lambda item: item["probe_id"],
            )
            if not previews:
                continue

            first_preview = previews[0]
            first_light_settled_preview = next(
                (
                    event
                    for event in previews
                    if event["demand_present"] and event["demand_settled"]
                ),
                None,
            )
            if first_light_settled_preview is not None and same_camera_position(
                first_preview, first_light_settled_preview
            ):
                light_view_candidates += 1
                elapsed = interval_ms(first_preview, first_light_settled_preview)
                if elapsed is not None:
                    light_intervals.append(elapsed)
                    all_light_intervals.append(elapsed)

            last_preview = previews[-1]
            last_light_settled_preview = next(
                (
                    event
                    for event in reversed(previews)
                    if event["demand_present"] and event["demand_settled"]
                ),
                None,
            )
            first_nonpreview_after = next(
                (
                    event
                    for event in nonpreviews
                    if event["probe_id"] > last_preview["probe_id"]
                ),
                None,
            )
            if (
                first_nonpreview_after is not None
                and same_camera_position(first_preview, first_nonpreview_after)
            ):
                full_view_candidates += 1
                elapsed = interval_ms(first_preview, first_nonpreview_after)
                if elapsed is not None:
                    full_preview_intervals.append(elapsed)
                    all_preview_to_nonpreview_intervals.append(elapsed)
                light_elapsed = (
                    interval_ms(first_preview, first_light_settled_preview)
                    if first_light_settled_preview is not None
                    and same_camera_position(
                        first_preview, first_light_settled_preview
                    )
                    else None
                )
                handoff_elapsed = (
                    interval_ms(
                        last_light_settled_preview, first_nonpreview_after
                    )
                    if last_light_settled_preview is not None
                    and same_camera_position(
                        last_light_settled_preview, first_nonpreview_after
                    )
                    else None
                )

                stage_timeline: list[dict[str, Any]] = []
                stage_intervals: dict[str, float | None] = {}
                causal_attempt_id: int | None = None
                causal_job_id: int | None = None
                aligned_clock = bool(
                    first_preview.get("sample_clock_aligned")
                    and first_nonpreview_after.get("sample_clock_aligned")
                )
                start_ms = first_preview.get("sample_time_ms")
                end_ms = first_nonpreview_after.get("sample_time_ms")
                if (
                    aligned_clock
                    and start_ms is not None
                    and end_ms is not None
                    and first_preview.get("world_epoch")
                    == first_nonpreview_after.get("world_epoch")
                    and first_preview.get("incarnation")
                    == first_nonpreview_after.get("incarnation")
                ):
                    identity_world = first_preview.get("world_epoch")
                    identity_incarnation = first_preview.get("incarnation")
                    candidates = (
                        demand_lifecycle_events.get(chunk, [])
                        + mesh_job_stage_events.get(chunk, [])
                    )
                    window_stage_events = [
                        {
                            **event,
                            "relative_to_first_preview_ms": round(
                                float(event["event_ms"]) - float(start_ms), 3
                            ),
                        }
                        for event in candidates
                        if event.get("world_epoch") == identity_world
                        and event.get("incarnation") == identity_incarnation
                        and float(start_ms)
                        <= float(event["event_ms"])
                        <= float(end_ms)
                    ]
                    stage_timeline, causal_attempt_id, causal_job_id = (
                        causal_lifecycle_events(
                            window_stage_events,
                            float(start_ms),
                            float(end_ms),
                        )
                    )

                    def first_stage_time(name: str) -> float | None:
                        return next(
                            (
                                float(event["event_ms"])
                                for event in stage_timeline
                                if event["event"] == name
                            ),
                            None,
                        )

                    milestone_names = (
                        "light_calculation_settled",
                        "stale_light_remesh_queued",
                        "stale_light_remesh_dirty_admitted",
                        "mesh_job_started",
                        "mesh_job_built",
                        "mesh_job_uploaded",
                        "mesh_job_gpu_queued",
                        "mesh_job_gpu_kicked",
                        "mesh_job_gpu_counters_ready",
                        "mesh_job_gpu_ready",
                        "mesh_job_published",
                        "install_published",
                    )
                    milestone_times = {
                        name: first_stage_time(name) for name in milestone_names
                    }

                    def stage_interval(left: str, right: str) -> float | None:
                        left_ms = milestone_times[left]
                        right_ms = milestone_times[right]
                        if left_ms is None or right_ms is None or right_ms < left_ms:
                            return None
                        return round(right_ms - left_ms, 3)

                    stage_intervals = {
                        "preview_to_light_settlement_ms": (
                            round(
                                milestone_times["light_calculation_settled"]
                                - float(start_ms),
                                3,
                            )
                            if milestone_times["light_calculation_settled"]
                            is not None
                            else None
                        ),
                        "light_settlement_to_stale_queue_ms": stage_interval(
                            "light_calculation_settled",
                            "stale_light_remesh_queued",
                        ),
                        "stale_queue_to_dirty_admit_ms": stage_interval(
                            "stale_light_remesh_queued",
                            "stale_light_remesh_dirty_admitted",
                        ),
                        "dirty_admit_to_mesh_start_ms": stage_interval(
                            "stale_light_remesh_dirty_admitted",
                            "mesh_job_started",
                        ),
                        "mesh_build_ms": stage_interval(
                            "mesh_job_started", "mesh_job_built"
                        ),
                        "mesh_build_to_publish_ms": stage_interval(
                            "mesh_job_built", "mesh_job_published"
                        ),
                        "mesh_build_to_gpu_queue_ms": stage_interval(
                            "mesh_job_built", "mesh_job_gpu_queued"
                        ),
                        "gpu_queue_to_kick_ms": stage_interval(
                            "mesh_job_gpu_queued", "mesh_job_gpu_kicked"
                        ),
                        "gpu_kick_to_counters_ready_ms": stage_interval(
                            "mesh_job_gpu_kicked",
                            "mesh_job_gpu_counters_ready",
                        ),
                        "gpu_counters_ready_to_gpu_ready_ms": stage_interval(
                            "mesh_job_gpu_counters_ready",
                            "mesh_job_gpu_ready",
                        ),
                        "gpu_ready_to_nonpreview_ms": (
                            round(
                                float(end_ms)
                                - milestone_times["mesh_job_gpu_ready"],
                                3,
                            )
                            if milestone_times["mesh_job_gpu_ready"]
                            is not None
                            else None
                        ),
                        "publish_to_nonpreview_ms": (
                            round(
                                float(end_ms)
                                - milestone_times["mesh_job_published"],
                                3,
                            )
                            if milestone_times["mesh_job_published"]
                            is not None
                            else None
                        ),
                    }

                def stage_snapshot(event: dict[str, Any]) -> dict[str, Any]:
                    keys = (
                        "probe_id",
                        "sample_time_ms",
                        "rgb",
                        "luminance",
                        "demand_present",
                        "demand_settled",
                        "demand_active_attempt",
                        "demand_attempt_age_ms",
                        "demand_progress_age_ms",
                        "field_light_revision",
                        "meshed_light_revision",
                        "published_light_revision",
                        "stale_light_remesh_pending",
                        "mesh_dirty_queue_kind",
                        "relight_queue_kind",
                        "mesh_work_owner_flags",
                        "relight_owner_flags",
                        "column_flow_ticket_flags",
                        "published_geometry_revision",
                        "mesh_revision",
                        "mdi_visible_pass_flags",
                        "mdi_visible_index_count",
                        "fog",
                        "fog_blend_estimate",
                    )
                    return {key: event.get(key) for key in keys}

                fog_field_changes = changed_fog_fields(
                    first_preview.get("fog"),
                    first_nonpreview_after.get("fog"),
                )
                preview_luma = first_preview.get("luminance")
                settled_luma = first_nonpreview_after.get("luminance")
                first_fog_blend = first_preview.get("fog_blend_estimate")
                settled_fog_blend = first_nonpreview_after.get(
                    "fog_blend_estimate"
                )
                luma_change_fog_confounded = (
                    bool(
                        fog_field_changes
                        and preview_luma is not None
                        and settled_luma is not None
                        and abs(float(settled_luma) - float(preview_luma)) > 0.001
                    )
                    if fog_field_changes is not None
                    else None
                )

                pixel_lifecycle_rows.append(
                    {
                        "pixel": list(pixel_events["preview"][0]["pixel"]),
                        "camera_world_pos": first_preview.get(
                            "camera_world_pos"
                        ),
                        "observed_preview_to_nonpreview_ms": elapsed,
                        "observed_preview_to_light_settlement_ms": light_elapsed,
                        "observed_light_settlement_to_nonpreview_ms": (
                            handoff_elapsed
                        ),
                        "stage_clock_aligned": aligned_clock,
                        "causal_attempt_id": causal_attempt_id,
                        "causal_job_id": causal_job_id,
                        "stage_timeline": stage_timeline,
                        "stage_intervals_ms": stage_intervals,
                        "same_pixel_camera_verified": True,
                        "first_preview_rgb": first_preview.get("rgb"),
                        "first_preview_luminance": first_preview.get(
                            "luminance"
                        ),
                        "settled_nonpreview_rgb": first_nonpreview_after.get(
                            "rgb"
                        ),
                        "settled_nonpreview_luminance": first_nonpreview_after.get(
                            "luminance"
                        ),
                        "first_preview_fog": first_preview.get("fog"),
                        "settled_nonpreview_fog": first_nonpreview_after.get(
                            "fog"
                        ),
                        "first_preview_fog_blend_estimate": first_fog_blend,
                        "settled_nonpreview_fog_blend_estimate": (
                            settled_fog_blend
                        ),
                        "fully_fogged_in_both_samples": (
                            first_fog_blend is not None
                            and settled_fog_blend is not None
                            and first_fog_blend["factor"] >= 0.99
                            and settled_fog_blend["factor"] >= 0.99
                        )
                        if first_fog_blend is not None
                        and settled_fog_blend is not None
                        else None,
                        "changed_fog_fields": fog_field_changes,
                        "fog_state_changed": (
                            bool(fog_field_changes)
                            if fog_field_changes is not None
                            else None
                        ),
                        "luminance_delta_fog_context_changed": (
                            luma_change_fog_confounded
                        ),
                        "luminance_delta": (
                            round(
                                float(first_nonpreview_after["luminance"])
                                - float(first_preview["luminance"]),
                                3,
                            )
                            if first_nonpreview_after.get("luminance")
                            is not None
                            and first_preview.get("luminance") is not None
                            else None
                        ),
                        "first_preview_state": stage_snapshot(first_preview),
                        "first_light_settled_preview_state": (
                            stage_snapshot(first_light_settled_preview)
                            if first_light_settled_preview is not None
                            else None
                        ),
                        "last_preview_state": stage_snapshot(last_preview),
                        "settled_nonpreview_state": stage_snapshot(
                            first_nonpreview_after
                        ),
                    }
                )
            if (
                last_light_settled_preview is not None
                and first_nonpreview_after is not None
                and same_camera_position(
                    last_light_settled_preview, first_nonpreview_after
                )
            ):
                handoff_view_candidates += 1
                elapsed = interval_ms(
                    last_light_settled_preview, first_nonpreview_after
                )
                if elapsed is not None:
                    handoff_intervals.append(elapsed)
                    all_handoff_intervals.append(elapsed)

        lifecycle_by_chunk.append(
            {
                "chunk": list(chunk),
                "screen_pixel_trajectory_count": len(events_by_pixel),
                "same_view_preview_to_nonpreview_candidate_count": full_view_candidates,
                "same_view_preview_to_nonpreview_intervals_ms": full_preview_intervals,
                "same_view_pixel_lifecycles": pixel_lifecycle_rows,
                "same_view_light_interval_candidate_count": light_view_candidates,
                "same_view_light_settlement_intervals_ms": light_intervals,
                "same_view_preview_handoff_candidate_count": handoff_view_candidates,
                "same_view_settled_preview_to_nonpreview_intervals_ms": handoff_intervals,
            }
        )

    lifecycle_event_names = {
        "light_calculation_settled",
        "stale_light_remesh_queued",
        "stale_light_remesh_dirty_admitted",
        "mesh_job_admitted",
        "mesh_job_started",
        "mesh_job_built",
        "mesh_job_uploaded",
        "mesh_job_gpu_queued",
        "mesh_job_gpu_kicked",
        "mesh_job_gpu_counters_ready",
        "mesh_job_gpu_ready",
        "mesh_job_published",
        "install_published",
    }
    for chunk, previews in sorted(preview_pixel_events.items()):
        if not previews:
            continue
        first_preview = min(
            previews,
            key=lambda item: (
                item["sample_time_ms"]
                if item["sample_time_ms"] is not None
                else float("inf"),
                item["probe_id"],
            ),
        )
        identity_world = first_preview.get("world_epoch")
        identity_incarnation = first_preview.get("incarnation")
        first_time = first_preview.get("sample_time_ms")
        settled_nonpreview = next(
            (
                item
                for item in sorted(
                    settled_nonpreview_pixel_events.get(chunk, []),
                    key=lambda event: event["probe_id"],
                )
                if item.get("world_epoch") == identity_world
                and item.get("incarnation") == identity_incarnation
                and first_time is not None
                and item.get("sample_time_ms") is not None
                and float(item["sample_time_ms"]) >= float(first_time)
            ),
            None,
        )
        end_time = (
            settled_nonpreview.get("sample_time_ms")
            if settled_nonpreview
            else None
        )
        candidates = (
            demand_lifecycle_events.get(chunk, [])
            + mesh_job_stage_events.get(chunk, [])
        )
        event_timeline = [
            event
            for event in candidates
            if event.get("world_epoch") == identity_world
            and event.get("incarnation") == identity_incarnation
            and event["event"] in lifecycle_event_names
            and first_time is not None
            and float(event["event_ms"]) >= float(first_time)
            and (end_time is None or float(event["event_ms"]) <= float(end_time))
        ]
        event_timeline.sort(key=lambda event: event["event_ms"])
        event_window_end_ms = (
            float(end_time)
            if end_time is not None
            else max(
                (float(event["event_ms"]) for event in event_timeline),
                default=float(first_time or 0.0),
            )
        )
        causal_event_timeline, causal_attempt_id, causal_job_id = (
            causal_lifecycle_events(
                event_timeline,
                float(first_time or 0.0),
                event_window_end_ms,
                fallback_source_light_rev=first_preview.get(
                    "field_light_revision"
                ),
                fallback_min_geom_rev=first_preview.get(
                    "published_geometry_revision"
                ),
            )
        )

        def next_event_time(name: str, after_ms: float | None) -> float | None:
            return next(
                (
                    float(event["event_ms"])
                    for event in causal_event_timeline
                    if event["event"] == name
                    and (after_ms is None or float(event["event_ms"]) >= after_ms)
                ),
                None,
            )

        light_settled_ms = next_event_time("light_calculation_settled", first_time)
        if light_settled_ms is None and first_preview.get("demand_settled"):
            light_settled_ms = first_time
        queue_ms = next_event_time(
            "stale_light_remesh_queued",
            light_settled_ms if light_settled_ms is not None else first_time,
        )
        dirty_admitted_ms = next_event_time(
            "stale_light_remesh_dirty_admitted",
            queue_ms if queue_ms is not None else (
                light_settled_ms if light_settled_ms is not None else first_time
            ),
        )
        mesh_start_ms = next_event_time(
            "mesh_job_started",
            dirty_admitted_ms if dirty_admitted_ms is not None else (
                light_settled_ms if light_settled_ms is not None else first_time
            ),
        )
        mesh_built_ms = next_event_time("mesh_job_built", mesh_start_ms)
        mesh_published_ms = next_event_time(
            "mesh_job_published", mesh_built_ms
        )
        mesh_gpu_queued_ms = next_event_time(
            "mesh_job_gpu_queued", mesh_built_ms
        )
        mesh_gpu_kicked_ms = next_event_time(
            "mesh_job_gpu_kicked", mesh_gpu_queued_ms
        )
        mesh_gpu_counters_ready_ms = next_event_time(
            "mesh_job_gpu_counters_ready", mesh_gpu_kicked_ms
        )
        mesh_gpu_ready_ms = next_event_time(
            "mesh_job_gpu_ready", mesh_gpu_counters_ready_ms
        )
        install_published_ms = next_event_time(
            "install_published", mesh_published_ms
        )

        def elapsed_to(later_ms: float | None, earlier_ms: float | None) -> float | None:
            if later_ms is None or earlier_ms is None or later_ms < earlier_ms:
                return None
            return round(later_ms - earlier_ms, 3)

        lifecycle_event_timeline_by_chunk.append(
            {
                "chunk": list(chunk),
                "world_epoch": identity_world,
                "incarnation": identity_incarnation,
                "first_preview_sample_ms": first_time,
                "first_preview_camera_world_pos": first_preview.get(
                    "camera_world_pos"
                ),
                "first_preview_demand_settled": first_preview.get(
                    "demand_settled"
                ),
                "first_preview_draw_state": {
                    key: first_preview.get(key)
                    for key in (
                        "published_geometry_revision",
                        "mesh_revision",
                        "mdi_visible_pass_flags",
                        "mdi_visible_index_count",
                    )
                },
                "first_settled_nonpreview_sample_ms": end_time,
                "first_settled_nonpreview_camera_world_pos": (
                    settled_nonpreview.get("camera_world_pos")
                    if settled_nonpreview
                    else None
                ),
                "first_settled_nonpreview_draw_state": (
                    {
                        key: settled_nonpreview.get(key)
                        for key in (
                            "published_geometry_revision",
                            "mesh_revision",
                            "mdi_visible_pass_flags",
                            "mdi_visible_index_count",
                        )
                    }
                    if settled_nonpreview
                    else None
                ),
                "observed_chunk_preview_to_nonpreview_ms": elapsed_to(
                    end_time, first_time
                ),
                "same_pixel_camera_verified": False,
                "chunk_observation_event_count": len(event_timeline),
                "causal_attempt_id": causal_attempt_id,
                "causal_job_id": causal_job_id,
                "causal_join_mode": (
                    "attempt_and_job"
                    if causal_attempt_id is not None
                    else (
                        "incarnation_revision_job_fallback"
                        if causal_job_id is not None
                        else "none"
                    )
                ),
                "light_settlement_observed_ms": light_settled_ms,
                "stale_remesh_queue_ms": queue_ms,
                "stale_remesh_dirty_admitted_ms": dirty_admitted_ms,
                "mesh_job_started_ms": mesh_start_ms,
                "mesh_job_built_ms": mesh_built_ms,
                "mesh_job_published_ms": mesh_published_ms,
                "mesh_job_gpu_queued_ms": mesh_gpu_queued_ms,
                "mesh_job_gpu_kicked_ms": mesh_gpu_kicked_ms,
                "mesh_job_gpu_counters_ready_ms": mesh_gpu_counters_ready_ms,
                "mesh_job_gpu_ready_ms": mesh_gpu_ready_ms,
                "install_published_ms": install_published_ms,
                "stage_intervals_ms": {
                    "preview_to_light_settlement_ms": elapsed_to(
                        light_settled_ms, first_time
                    ),
                    "light_settlement_to_queue_ms": elapsed_to(
                        queue_ms, light_settled_ms
                    ),
                    "queue_to_dirty_admission_ms": elapsed_to(
                        dirty_admitted_ms, queue_ms
                    ),
                    "dirty_admission_to_mesh_start_ms": elapsed_to(
                        mesh_start_ms, dirty_admitted_ms
                    ),
                    "mesh_build_ms": elapsed_to(mesh_built_ms, mesh_start_ms),
                    "mesh_publish_ms": elapsed_to(
                        mesh_published_ms, mesh_built_ms
                    ),
                    "mesh_build_to_gpu_queue_ms": elapsed_to(
                        mesh_gpu_queued_ms, mesh_built_ms
                    ),
                    "gpu_queue_to_kick_ms": elapsed_to(
                        mesh_gpu_kicked_ms, mesh_gpu_queued_ms
                    ),
                    "gpu_kick_to_counters_ready_ms": elapsed_to(
                        mesh_gpu_counters_ready_ms, mesh_gpu_kicked_ms
                    ),
                    "gpu_counters_ready_to_gpu_ready_ms": elapsed_to(
                        mesh_gpu_ready_ms, mesh_gpu_counters_ready_ms
                    ),
                    "gpu_ready_to_nonpreview_ms": elapsed_to(
                        end_time, mesh_gpu_ready_ms
                    ),
                    "mesh_publish_to_nonpreview_ms": elapsed_to(
                        end_time, mesh_published_ms
                    ),
                },
                "stage_timeline": [
                    {
                        **event,
                        "relative_to_first_preview_ms": round(
                            float(event["event_ms"]) - float(first_time), 3
                        ),
                    }
                    for event in causal_event_timeline
                ],
            }
        )

    def summarize_intervals(values: list[float]) -> dict[str, Any]:
        ordered = sorted(values)
        return {
            "count": len(ordered),
            "min_ms": ordered[0] if ordered else None,
            "median_ms": round(median(ordered), 3) if ordered else None,
            "max_ms": ordered[-1] if ordered else None,
        }

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

    def as_int(value: Any) -> int:
        try:
            return int(value)
        except (TypeError, ValueError):
            return 0

    dark_post_pretransparent_changed = [
        pixel for pixel in dark_pixels if pixel["transparent_changed_rgb"]
    ]
    dark_post_pretransparent_unchanged = [
        pixel for pixel in dark_pixels if not pixel["transparent_changed_rgb"]
    ]

    def has_drawable_opaque_surface(pixel: dict[str, Any]) -> bool:
        surface = pixel["depth_surface"]
        return (
            as_int(surface["valid"]) == 1
            and as_int(surface["drawable"]) == 1
            and as_int(surface["opaque_mdi_visible_pass_flags"]) != 0
        )

    def has_unsettled_opaque_light(pixel: dict[str, Any]) -> bool:
        demand = pixel["opaque_chunk_light"]
        voxel = pixel["voxel_chunk_light"]
        published = demand["published_revision"]
        meshed = demand["meshed_revision"]
        field = demand["field_revision"]
        return (
            (
                as_int(demand["demand_present"]) == 1
                and as_int(demand["demand_settled"]) != 1
            )
            or as_int(voxel["pending"]) == 1
            or (
                published is not None
                and field is not None
                and as_int(published) < as_int(field)
            )
            or (
                meshed is not None
                and field is not None
                and as_int(meshed) < as_int(field)
            )
            or as_int(pixel["surface_preview_shader_override"]) == 1
            or as_int(pixel["opaque_preview_shader_override"]) == 1
            or as_int(pixel["opaque_vertex_light"]["preview"]) == 1
        )

    dark_opaque_surface = [
        pixel for pixel in dark_post_pretransparent_unchanged
        if has_drawable_opaque_surface(pixel)
    ]
    dark_opaque_unsettled_light = [
        pixel for pixel in dark_opaque_surface
        if has_unsettled_opaque_light(pixel)
    ]
    dark_opaque_preview = [
        pixel for pixel in dark_opaque_surface
        if as_int(pixel["surface_preview_shader_override"]) == 1
        or as_int(pixel["opaque_preview_shader_override"]) == 1
        or as_int(pixel["opaque_vertex_light"]["preview"]) == 1
    ]
    dark_opaque_revision_mismatch = [
        pixel for pixel in dark_opaque_surface
        if (
            pixel["opaque_chunk_light"]["field_revision"] is not None
            and pixel["opaque_chunk_light"]["published_revision"] is not None
            and as_int(pixel["opaque_chunk_light"]["published_revision"])
            < as_int(pixel["opaque_chunk_light"]["field_revision"])
        )
        or (
            pixel["opaque_chunk_light"]["field_revision"] is not None
            and pixel["opaque_chunk_light"]["meshed_revision"] is not None
            and as_int(pixel["opaque_chunk_light"]["meshed_revision"])
            < as_int(pixel["opaque_chunk_light"]["field_revision"])
        )
    ]
    dark_pixel_classification = {
        "luma_below_threshold_total_n": len(dark_pixels),
        "pretransparent_to_final_rgb_changed_n": len(
            dark_post_pretransparent_changed
        ),
        "pretransparent_to_final_rgb_unchanged_n": len(
            dark_post_pretransparent_unchanged
        ),
        "unchanged_rgb_with_drawable_opaque_surface_n": len(dark_opaque_surface),
        "unchanged_rgb_opaque_light_unsettled_or_preview_n": len(
            dark_opaque_unsettled_light
        ),
        "unchanged_rgb_opaque_preview_n": len(dark_opaque_preview),
        "unchanged_rgb_opaque_light_revision_mismatch_n": len(
            dark_opaque_revision_mismatch
        ),
        "opaque_surface_join_missing_n": len(dark_post_pretransparent_unchanged)
        - len(dark_opaque_surface),
        "semantics": (
            "Luma threshold applies to final sampled RGB. This separates RGB "
            "changed after the pretransparent sample from unchanged RGB; unchanged "
            "RGB is only an opaque dark-surface candidate when it joins a drawable "
            "visible opaque depth surface. Light state narrows that candidate but "
            "does not prove the operator-visible defect."
        ),
    }
    dark_pixel_fog_blend_counts: Counter[str] = Counter()
    dark_pixel_surface_groups: dict[str, dict[str, Any]] = {}
    for pixel in dark_pixels:
        blend = pixel.get("fog_blend_estimate")
        if blend is None:
            dark_pixel_fog_blend_counts["unknown"] += 1
        elif blend["factor"] >= 0.99:
            dark_pixel_fog_blend_counts["fully_fogged"] += 1
        elif blend["factor"] >= 0.9:
            dark_pixel_fog_blend_counts["heavily_fogged"] += 1
        elif blend["factor"] > 0.0:
            dark_pixel_fog_blend_counts["partly_fogged"] += 1
        else:
            dark_pixel_fog_blend_counts["outside_fog"] += 1

        source_face = (pixel.get("depth_surface") or {}).get("source_face") or {}
        block_id = source_face.get("block_id") if source_face.get("valid") else None
        group_key = str(block_id) if block_id is not None else "unknown"
        group = dark_pixel_surface_groups.setdefault(
            group_key,
            {
                "sample_count": 0,
                "face_counts": Counter(),
                "luminance_min": None,
                "luminance_max": None,
                "preview_sample_count": 0,
                "unsettled_demand_sample_count": 0,
            },
        )
        group["sample_count"] += 1
        face = source_face.get("face") if source_face.get("valid") else None
        group["face_counts"][str(face) if face is not None else "unknown"] += 1
        luma = float(pixel.get("luminance", 0.0))
        group["luminance_min"] = (
            luma if group["luminance_min"] is None
            else min(group["luminance_min"], luma)
        )
        group["luminance_max"] = (
            luma if group["luminance_max"] is None
            else max(group["luminance_max"], luma)
        )
        vertex_light = pixel.get("opaque_vertex_light") or {}
        chunk_light = pixel.get("opaque_chunk_light") or {}
        group["preview_sample_count"] += int(
            as_int(vertex_light.get("preview")) == 1
            or as_int(pixel.get("opaque_preview_shader_override")) == 1
            or as_int(pixel.get("surface_preview_shader_override")) == 1
        )
        group["unsettled_demand_sample_count"] += int(
            not bool(chunk_light.get("demand_settled"))
        )
    dark_pixel_surface_summary = {
        block_id: {
            **{
                key: value
                for key, value in group.items()
                if key != "face_counts"
            },
            "face_counts": dict(group["face_counts"]),
        }
        for block_id, group in sorted(dark_pixel_surface_groups.items())
    }
    watched_chunk_observation_rows: list[dict[str, Any]] = []
    for coord, raw_samples in sorted(watched_chunk_samples.items()):
        samples = sorted(
            raw_samples,
            key=lambda sample: (
                float(sample["sample_time_ms"])
                if sample["sample_time_ms"] is not None
                else float("inf")
            ),
        )
        same_view_groups: list[list[dict[str, Any]]] = []
        for sample in samples:
            group = next(
                (
                    existing
                    for existing in same_view_groups
                    if existing[0]["pixel"] == sample["pixel"]
                    and existing[0].get("world_epoch")
                    == sample.get("world_epoch")
                    and existing[0].get("incarnation")
                    == sample.get("incarnation")
                    and same_camera_position(existing[0], sample)
                ),
                None,
            )
            if group is None:
                same_view_groups.append([sample])
            else:
                group.append(sample)
        repeated_same_view = []
        for group in same_view_groups:
            if len(group) < 2:
                continue
            first = group[0]
            last = group[-1]
            first_ms = first.get("sample_time_ms")
            last_ms = last.get("sample_time_ms")
            repeated_same_view.append(
                {
                    "pixel": first["pixel"],
                    "camera_world_pos": first["camera_world_pos"],
                    "sample_count": len(group),
                    "first_sample_ms": first_ms,
                    "last_sample_ms": last_ms,
                    "observed_interval_ms": (
                        round(float(last_ms) - float(first_ms), 3)
                        if first_ms is not None
                        and last_ms is not None
                        and float(last_ms) >= float(first_ms)
                        else None
                    ),
                    "first_rgb": first["rgb"],
                    "last_rgb": last["rgb"],
                    "first_light_revisions": [
                        first["field_light_revision"],
                        first["meshed_light_revision"],
                        first["published_light_revision"],
                    ],
                    "last_light_revisions": [
                        last["field_light_revision"],
                        last["meshed_light_revision"],
                        last["published_light_revision"],
                    ],
                    "all_draw_ready": all(
                        item["draw_ready"] for item in group
                    ),
                    "all_light_revisions_match": all(
                        item["light_revisions_match"] for item in group
                    ),
                }
            )
        observed_times = [
            float(sample["sample_time_ms"])
            for sample in samples
            if sample["sample_time_ms"] is not None
        ]
        matching_depth_samples = [
            sample
            for sample in samples
            if sample["opaque_surface_matches_watched_chunk"]
        ]
        matching_voxel_ray_samples = [
            sample
            for sample in samples
            if sample["voxel_ray_chunk"] == list(coord)
        ]
        watched_chunk_observation_rows.append(
            {
                "chunk": list(coord),
                "sample_count": len(samples),
                "unique_pixel_count": len(
                    {tuple(sample["pixel"]) for sample in samples}
                ),
                "drawable_sample_count": sum(
                    sample["drawable"] for sample in matching_depth_samples
                ),
                "draw_ready_sample_count": sum(
                    sample["draw_ready"] for sample in matching_depth_samples
                ),
                "matching_light_revision_sample_count": sum(
                    sample["light_revisions_match"]
                    for sample in matching_depth_samples
                ),
                "matching_geometry_revision_sample_count": sum(
                    sample["geometry_revision_match"]
                    for sample in matching_depth_samples
                ),
                "geometry_revision_mismatch_sample_count": sum(
                    not sample["geometry_revision_match"]
                    for sample in matching_depth_samples
                ),
                "unsettled_demand_sample_count": sum(
                    not sample["demand_settled"]
                    for sample in matching_depth_samples
                ),
                "preview_sample_count": sum(
                    sample["light_preview"] for sample in matching_depth_samples
                ),
                "stale_remesh_pending_sample_count": sum(
                    sample["stale_light_remesh_pending"]
                    for sample in matching_depth_samples
                ),
                "depth_surface_sample_count": len(matching_depth_samples),
                "voxel_ray_hit_sample_count": len(matching_voxel_ray_samples),
                "first_sample_ms": min(observed_times)
                if observed_times
                else None,
                "last_sample_ms": max(observed_times)
                if observed_times
                else None,
                "repeated_same_view_pixel_groups": repeated_same_view,
                "samples": samples,
            }
        )
    report = {
        "perf_jsonl": str(args.perf_jsonl),
        "pixel_probe_count": probe_count,
        "absolute_steady_clock_pixel_probe_count": absolute_timestamp_probe_count,
        "dark_threshold_luma": args.threshold,
        "dark_pixel_count": len(dark_pixels),
        "dark_pixel_classification": dark_pixel_classification,
        "dark_pixel_fog_blend_counts": dict(dark_pixel_fog_blend_counts),
        "dark_pixel_surface_summary": dark_pixel_surface_summary,
        "watched_chunk_observations": watched_chunk_observation_rows,
        "opaque_unsettled_demand_sample_count": len(opaque_unsettled_seen),
        "surface_preview_shader_override_sample_count": surface_preview_sample_n,
        "provisional_light_preview_sample_count": provisional_preview_sample_n,
        "provisional_light_preview_chunk_count": len(preview_chunk_rows),
        "provisional_light_preview_chunks": preview_chunk_rows,
        "provisional_light_lifecycle_by_chunk": lifecycle_by_chunk,
        "provisional_light_event_timeline_by_chunk": (
            lifecycle_event_timeline_by_chunk
        ),
        "demand_lifecycle_trace_event_counts": dict(
            Counter(
                event["event"]
                for events in demand_lifecycle_events.values()
                for event in events
            )
        ),
        "mesh_job_stage_trace_event_counts": dict(
            Counter(
                event["event"]
                for events in mesh_job_stage_events.values()
                for event in events
            )
        ),
        "same_view_preview_to_nonpreview_summary": summarize_intervals(
            all_preview_to_nonpreview_intervals
        ),
        "same_view_preview_to_light_settlement_summary": summarize_intervals(
            all_light_intervals
        ),
        "same_view_settled_preview_to_nonpreview_summary": summarize_intervals(
            all_handoff_intervals
        ),
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
            "surface has separate chunk, MDI, source-light, dirty-queue, and work-owner "
            "witnesses. These joins localize mismatches but do not by themselves prove the "
            "responsible draw path. The dark-pixel threshold applies to final composed RGB; "
            "use dark_pixel_classification to separate post-pretransparent RGB changes from an "
            "opaque drawable-surface candidate. New traces use renderer_pixel_sample_steady_ms to align "
            "pixel observations with absolute steady-clock lifecycle events; older traces "
            "fall back to renderer_pixel_sample_time_ms, which cannot join absolute event_ms. "
            "Preview-to-settled intervals are observation bounds from sparse route "
            "samples, not exact transition timestamps. The lifecycle stage intervals group "
            "one exact screen pixel in one chunk and require camera positions within 0.01 "
            "world units; light settlement means the demand reports settled while its "
            "preview remains active, and handoff ends at the first settled nonpreview sample "
            "after the last preview observation. Provisional-light lifecycle rows also compare "
            "the mesh job's published geometry revision with the first and later pixel's "
            "published revision, mesh revision, and visible MDI state. A matching chunk revision "
            "at a moved camera does not establish a same-view handoff. RGB and luminance changes "
            "are ambiguous when recorded fog parameters change; compare the per-sample fog "
            "snapshots and estimated per-pixel fog blend before attributing a pixel change to "
            "lighting. A blend estimate at or above 0.99 means the surface is effectively hidden "
            "by fog in the sampled shader path. Low-luma pixels with no fog blend remain "
            "drawable surface samples; their RGB alone does not distinguish dark texture, "
            "surface orientation, or a lighting problem."
        ),
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
