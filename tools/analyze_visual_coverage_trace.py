#!/usr/bin/env python3
"""Summarize opt-in voxel renderer, focus, and screen-ray traces in JSONL."""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path

SCREEN_RAY_ROWS = (0.125, 0.375, 0.5625, 0.625, 0.875)


def add(counter: collections.Counter, value: object) -> None:
    counter[str(value)] += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument(
        "--fog-end-distance",
        type=float,
        help=(
            "optional full-blend fog distance in blocks; reports how many "
            "ray candidates fall beyond it, without treating them as visible"
        ),
    )
    args = parser.parse_args()

    trace_counts: collections.Counter[str] = collections.Counter()
    screen_state: collections.Counter[str] = collections.Counter()
    screen_candidates_by_state: collections.Counter[str] = collections.Counter()
    screen_selected_by_state: collections.Counter[str] = collections.Counter()
    screen_combos: collections.Counter[str] = collections.Counter()
    screen_examples: list[dict[str, object]] = []
    screen_candidate_distances: list[float] = []
    screen_candidate_coords: set[tuple[int, int, int]] = set()
    screen_candidate_rows: list[dict[str, object]] = []
    pixel_probe_rows: list[dict[str, object]] = []
    focus_states: collections.Counter[str] = collections.Counter()
    focus_visual_classes: collections.Counter[str] = collections.Counter()
    focus_terrain_complete: collections.Counter[str] = collections.Counter()
    frustum_states: collections.Counter[str] = collections.Counter()
    frustum_draw_summary: collections.Counter[str] = collections.Counter()
    peak_rows: dict[str, dict[tuple[int, int, int], dict[str, object]]] = {
        "camera_band_no_drawable_peak_slice_trace": {},
        "camera_band_unowned_peak_slice_trace": {},
    }
    peak_totals: dict[str, int] = {}

    focus_fields = (
        "focus_state",
        "focus_column_visual_class",
        "focus_column_terrain_complete",
        "focus_column_in_unfinished_keys",
    )
    ray_fields = (
        "screen_ray_x",
        "screen_ray_y",
        "screen_ray_distance",
        "screen_ray_column",
        "screen_ray_row",
        "screen_ray_state",
        "screen_ray_block_x",
        "screen_ray_block_y",
        "screen_ray_block_z",
        "screen_ray_in_focus_radius",
        "screen_ray_in_height_band",
        "screen_ray_mesh_satisfying",
        "screen_ray_geometry_debt",
        "screen_ray_repairable_geometry_debt",
        "screen_ray_light_debt",
        "screen_ray_needs_refresh",
        "screen_ray_candidate",
        "screen_ray_selected",
    )
    peak_fields = (
        "frame_epoch",
        "cx",
        "cy",
        "cz",
        "focus_cx",
        "focus_cz",
        "camera_x",
        "camera_y",
        "camera_z",
        "non_air_blocks",
        "focus_state",
        "mesh_dirty_queue_kind",
        "mesh_dirty_queue_index",
        "mesh_work_owner_flags",
        "relight_owner_flags",
        "relight_queue_kind",
        "column_flow_ticket_flags",
        "active_stage",
        "attempt_id",
        "incarnation",
        "chunk_content_revision",
        "mesh_revision",
        "published_geom_rev",
        "demand_published_geom_rev",
        "desired_geom_rev",
        "desired_light_rev",
        "camera_band_solid_no_drawable_n",
        "camera_band_solid_unowned_n",
    )

    with args.perf_jsonl.open("r", encoding="utf-8", errors="replace") as source:
        for line in source:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            kind = row.get("kind")
            if not isinstance(kind, str):
                continue
            trace_counts[kind] += 1

            if kind == "focus_slice_trace":
                for field, counter in (
                    ("focus_state", focus_states),
                    ("focus_column_visual_class", focus_visual_classes),
                    ("focus_column_terrain_complete", focus_terrain_complete),
                ):
                    if field in row:
                        add(counter, row[field])

            elif kind == "screen_ray_candidate_trace":
                state = str(row.get("screen_ray_state", 0))
                candidate = int(row.get("screen_ray_candidate", 0) or 0)
                selected = int(row.get("screen_ray_selected", 0) or 0)
                add(screen_state, state)
                if candidate:
                    screen_candidate_rows.append(row)
                    add(screen_candidates_by_state, state)
                    screen_candidate_coords.add(
                        (
                            int(row.get("cx", 0)),
                            int(row.get("cy", 0)),
                            int(row.get("cz", 0)),
                        )
                    )
                    try:
                        distance = float(row.get("screen_ray_distance", -1.0))
                    except (TypeError, ValueError):
                        distance = -1.0
                    if distance >= 0.0:
                        screen_candidate_distances.append(distance)
                    combo = (
                        state,
                        int(row.get("screen_ray_mesh_satisfying", 0) or 0),
                        int(row.get("screen_ray_geometry_debt", 0) or 0),
                        int(row.get("screen_ray_repairable_geometry_debt", 0) or 0),
                        int(row.get("screen_ray_light_debt", 0) or 0),
                        int(row.get("screen_ray_needs_refresh", 0) or 0),
                    )
                    screen_combos[str(combo)] += 1
                    if selected:
                        add(screen_selected_by_state, state)
                    if len(screen_examples) < 32:
                        screen_examples.append(
                            {field: row.get(field) for field in (
                                "frame_epoch",
                                "cx",
                                "cy",
                                "cz",
                                "focus_cx",
                                "focus_cz",
                                *ray_fields,
                            ) if field in row}
                        )

            elif kind == "renderer_pixel_probe":
                pixel_fields = (
                    "frame_epoch",
                    "renderer_pixel_probe_id",
                    "renderer_pixel_x",
                    "renderer_pixel_y",
                    "renderer_pixel_rgba",
                    "renderer_pixel_pretransparent_rgba",
                    "renderer_pixel_pretransparent_depth",
                    "renderer_pixel_fog_start",
                    "renderer_pixel_fog_end",
                    "renderer_pixel_opaque_surface_valid",
                    "renderer_pixel_voxel_ray_state",
                    "renderer_pixel_voxel_hit_x",
                    "renderer_pixel_voxel_hit_y",
                    "renderer_pixel_voxel_hit_z",
                    "renderer_pixel_voxel_face_source_valid",
                    "renderer_pixel_voxel_chunk_mdi_visible_index_count",
                )
                pixel_probe_rows.append(
                    {field: row[field] for field in pixel_fields if field in row}
                )

            elif kind == "view_frustum_coverage_trace":
                state = str(row.get("focus_state", 0))
                add(frustum_states, state)
                visible_indices = int(row.get("renderer_mdi_visible_index_count", 0) or 0)
                resident_indices = int(row.get("renderer_mdi_index_count", 0) or 0)
                add(frustum_draw_summary, f"state={state},visible={int(visible_indices > 0)},resident={int(resident_indices > 0)}")

            elif kind in peak_rows:
                coord = (int(row.get("cx", 0)), int(row.get("cy", 0)), int(row.get("cz", 0)))
                peak_rows[kind][coord] = {field: row[field] for field in peak_fields if field in row}
                for field in ("camera_band_solid_no_drawable_n", "camera_band_solid_unowned_n"):
                    if field in row:
                        peak_totals[field] = max(peak_totals.get(field, 0), int(row[field]))

    def sorted_counter(counter: collections.Counter[str]) -> dict[str, int]:
        return dict(sorted(counter.items(), key=lambda item: item[0]))

    ordered_candidate_distances = sorted(screen_candidate_distances)

    def quantile(q: float) -> float | None:
        if not ordered_candidate_distances:
            return None
        index = round((len(ordered_candidate_distances) - 1) * q)
        return ordered_candidate_distances[index]

    candidate_distance = {
        "sample_count": len(ordered_candidate_distances),
        "min_blocks": quantile(0.0),
        "p50_blocks": quantile(0.50),
        "p95_blocks": quantile(0.95),
        "max_blocks": quantile(1.0),
        "unique_candidate_coords": len(screen_candidate_coords),
    }
    if args.fog_end_distance is not None:
        within_fog_end = sum(
            distance <= args.fog_end_distance
            for distance in ordered_candidate_distances
        )
        candidate_distance["fog_end_distance_blocks"] = args.fog_end_distance
        candidate_distance["at_or_before_fog_end"] = within_fog_end
        candidate_distance["beyond_fog_end"] = (
            len(ordered_candidate_distances) - within_fog_end
        )
        candidate_distance["beyond_fog_end_rate"] = (
            (len(ordered_candidate_distances) - within_fog_end)
            / len(ordered_candidate_distances)
            if ordered_candidate_distances
            else None
        )

    pixel_xs = sorted(
        {int(row["renderer_pixel_x"]) for row in pixel_probe_rows
         if "renderer_pixel_x" in row}
    )
    pixel_ys = sorted(
        {int(row["renderer_pixel_y"]) for row in pixel_probe_rows
         if "renderer_pixel_y" in row}
    )
    pixel_probe_by_ray_sample: dict[tuple[int, int, int], dict[str, object]] = {}
    pixel_join_mapping_available = len(pixel_xs) == 20 and len(pixel_ys) in (4, 5, 8)
    pixel_join_mapping_reason = None
    if pixel_join_mapping_available:
        # Pixel probes use GL bottom-left framebuffer coordinates. Synchronized
        # five-row probes add the .5625 screen-ray scanline to the regular four
        # rows; infer that viewport from the .125/.875 endpoint spacing.
        if len(pixel_ys) == 5:
            viewport_height = (pixel_ys[-1] - pixel_ys[0]) / 0.75
            viewport_y = pixel_ys[0] + 0.5 - viewport_height * 0.125
        else:
            viewport_height = (pixel_ys[-1] - pixel_ys[0]) * len(pixel_ys) / (
                len(pixel_ys) - 1
            )
            viewport_y = pixel_ys[0] - viewport_height / (2 * len(pixel_ys))
        pixel_y_to_ray_row: dict[int, int] = {}
        for pixel_y in pixel_ys:
            top_y = viewport_height - (pixel_y - viewport_y) - 0.5
            normalized_y = top_y / viewport_height
            nearest_row = min(
                range(len(SCREEN_RAY_ROWS)),
                key=lambda index: abs(SCREEN_RAY_ROWS[index] - normalized_y),
            )
            if abs(SCREEN_RAY_ROWS[nearest_row] - normalized_y) * viewport_height <= 1.0:
                pixel_y_to_ray_row[pixel_y] = nearest_row
        pixel_x_to_column = {x: index for index, x in enumerate(pixel_xs)}
        for pixel in pixel_probe_rows:
            pixel_x = int(pixel.get("renderer_pixel_x", -1))
            pixel_y = int(pixel.get("renderer_pixel_y", -1))
            column = pixel_x_to_column.get(pixel_x)
            ray_row = pixel_y_to_ray_row.get(pixel_y)
            if column is None or ray_row is None:
                continue
            key = (int(pixel.get("frame_epoch", 0)), column, ray_row)
            pixel_probe_by_ray_sample[key] = pixel
    else:
        pixel_join_mapping_reason = (
            f"expected 20 pixel columns and 4, 5, or 8 pixel rows; found "
            f"{len(pixel_xs)} columns and {len(pixel_ys)} rows"
        )

    pixel_join_pairs: list[tuple[dict[str, object], dict[str, object]]] = []
    if pixel_join_mapping_available:
        for ray in screen_candidate_rows:
            key = (
                int(ray.get("frame_epoch", 0)),
                int(ray.get("screen_ray_column", -1)),
                int(ray.get("screen_ray_row", -1)),
            )
            pixel = pixel_probe_by_ray_sample.get(key)
            if pixel is not None:
                pixel_join_pairs.append((ray, pixel))

    def pixel_hit_matches(ray: dict[str, object], pixel: dict[str, object]) -> bool:
        return (
            int(pixel.get("renderer_pixel_voxel_ray_state", 0) or 0) == 1
            and tuple(
                int(ray.get(field, 0) or 0)
                for field in (
                    "screen_ray_block_x",
                    "screen_ray_block_y",
                    "screen_ray_block_z",
                )
            )
            == tuple(
                int(pixel.get(field, 0) or 0)
                for field in (
                    "renderer_pixel_voxel_hit_x",
                    "renderer_pixel_voxel_hit_y",
                    "renderer_pixel_voxel_hit_z",
                )
            )
        )

    pixel_depths = [
        float(pixel.get("renderer_pixel_pretransparent_depth", 1.0) or 0.0)
        for _, pixel in pixel_join_pairs
    ]
    unmatched_candidate_rows = [
        ray
        for ray in screen_candidate_rows
        if (
            int(ray.get("frame_epoch", 0)),
            int(ray.get("screen_ray_column", -1)),
            int(ray.get("screen_ray_row", -1)),
        )
        not in pixel_probe_by_ray_sample
    ]
    clear_depth_pairs = [
        (ray, pixel)
        for ray, pixel in pixel_join_pairs
        if float(pixel.get("renderer_pixel_pretransparent_depth", 1.0) or 0.0)
        >= 0.999999
    ]

    def candidate_distance_summary(
        samples: list[dict[str, object]],
    ) -> dict[str, object]:
        distances = sorted(
            float(ray.get("screen_ray_distance", -1.0) or -1.0)
            for ray in samples
        )

        def sample_quantile(q: float) -> float | None:
            if not distances:
                return None
            return distances[round((len(distances) - 1) * q)]

        summary: dict[str, object] = {
            "sample_count": len(distances),
            "min_blocks": sample_quantile(0.0),
            "p50_blocks": sample_quantile(0.50),
            "p95_blocks": sample_quantile(0.95),
            "max_blocks": sample_quantile(1.0),
        }
        if args.fog_end_distance is not None:
            within = sum(distance <= args.fog_end_distance for distance in distances)
            summary["at_or_before_fog_end"] = within
            summary["beyond_fog_end"] = len(distances) - within
        return summary

    def unpack_pixel_rgba(pixel: dict[str, object], field: str) -> list[int]:
        value = int(pixel.get(field, 0) or 0)
        return [
            (value >> 24) & 0xFF,
            (value >> 16) & 0xFF,
            (value >> 8) & 0xFF,
            value & 0xFF,
        ]

    pixel_voxel_states: collections.Counter[str] = collections.Counter()
    for _, pixel in pixel_join_pairs:
        add(pixel_voxel_states, pixel.get("renderer_pixel_voxel_ray_state", 0))
    pixel_join = {
        "mapping_available": pixel_join_mapping_available,
        "mapping_reason": pixel_join_mapping_reason,
        "candidate_samples": len(screen_candidate_rows),
        "same_frame_same_sample_matches": len(pixel_join_pairs),
        "candidate_samples_without_pixel_probe": (
            len(screen_candidate_rows) - len(pixel_join_pairs)
        ),
        "candidate_samples_without_pixel_probe_at_or_before_fog_end": (
            sum(
                float(ray.get("screen_ray_distance", -1.0) or -1.0)
                <= args.fog_end_distance
                for ray in unmatched_candidate_rows
            )
            if args.fog_end_distance is not None
            else None
        ),
        "candidate_samples_without_pixel_probe_beyond_fog_end": (
            sum(
                float(ray.get("screen_ray_distance", -1.0) or -1.0)
                > args.fog_end_distance
                for ray in unmatched_candidate_rows
            )
            if args.fog_end_distance is not None
            else None
        ),
        "unmatched_candidate_distance": candidate_distance_summary(
            unmatched_candidate_rows
        ),
        "unmatched_candidate_focus_chunk_range": (
            [
                min(int(ray.get("focus_cx", 0)) for ray in unmatched_candidate_rows),
                max(int(ray.get("focus_cx", 0)) for ray in unmatched_candidate_rows),
            ]
            if unmatched_candidate_rows
            else None
        ),
        "opaque_surface_valid": sum(
            int(pixel.get("renderer_pixel_opaque_surface_valid", 0) or 0) == 1
            for _, pixel in pixel_join_pairs
        ),
        "pretransparent_depth_has_surface": sum(depth < 0.999999 for depth in pixel_depths),
        "pretransparent_depth_clear": sum(depth >= 0.999999 for depth in pixel_depths),
        "clear_depth_candidates_at_or_before_fog_end": (
            sum(
                float(ray.get("screen_ray_distance", -1.0) or -1.0)
                <= args.fog_end_distance
                for ray, _ in clear_depth_pairs
            )
            if args.fog_end_distance is not None
            else None
        ),
        "clear_depth_candidates_beyond_fog_end": (
            sum(
                float(ray.get("screen_ray_distance", -1.0) or -1.0)
                > args.fog_end_distance
                for ray, _ in clear_depth_pairs
            )
            if args.fog_end_distance is not None
            else None
        ),
        "clear_depth_samples": [
            {
                "frame_epoch": ray.get("frame_epoch"),
                "focus_chunk": [ray.get("focus_cx"), ray.get("focus_cz")],
                "pixel_xy": [
                    pixel.get("renderer_pixel_x"),
                    pixel.get("renderer_pixel_y"),
                ],
                "candidate_chunk": [ray.get("cx"), ray.get("cy"), ray.get("cz")],
                "candidate_block": [
                    ray.get("screen_ray_block_x"),
                    ray.get("screen_ray_block_y"),
                    ray.get("screen_ray_block_z"),
                ],
                "distance_blocks": ray.get("screen_ray_distance"),
                "mesh_satisfying": ray.get("screen_ray_mesh_satisfying"),
                "geometry_debt": ray.get("screen_ray_geometry_debt"),
                "pixel_depth": pixel.get("renderer_pixel_pretransparent_depth"),
                "pixel_rgba_pretransparent": unpack_pixel_rgba(
                    pixel, "renderer_pixel_pretransparent_rgba"
                ),
                "pixel_rgba_posttransparent": unpack_pixel_rgba(
                    pixel, "renderer_pixel_rgba"
                ),
                "pixel_fog_start": pixel.get("renderer_pixel_fog_start"),
                "pixel_fog_end": pixel.get("renderer_pixel_fog_end"),
                "opaque_surface_valid": pixel.get(
                    "renderer_pixel_opaque_surface_valid"
                ),
                "pixel_chunk_visible_mdi_indices": pixel.get(
                    "renderer_pixel_voxel_chunk_mdi_visible_index_count"
                ),
            }
            for ray, pixel in clear_depth_pairs
        ],
        "voxel_ray_state_counts": sorted_counter(pixel_voxel_states),
        "voxel_hit_same_block_as_stream_ray": sum(
            pixel_hit_matches(ray, pixel) for ray, pixel in pixel_join_pairs
        ),
        "voxel_hit_different_block_from_stream_ray": sum(
            int(pixel.get("renderer_pixel_voxel_ray_state", 0) or 0) == 1
            and not pixel_hit_matches(ray, pixel)
            for ray, pixel in pixel_join_pairs
        ),
        "voxel_face_source_valid": sum(
            int(pixel.get("renderer_pixel_voxel_face_source_valid", 0) or 0) == 1
            for _, pixel in pixel_join_pairs
        ),
        "pixel_chunk_visible_mdi_indices_positive": sum(
            int(pixel.get("renderer_pixel_voxel_chunk_mdi_visible_index_count", 0) or 0) > 0
            for _, pixel in pixel_join_pairs
        ),
        "matched_candidates_at_or_before_fog_end": (
            sum(
                float(ray.get("screen_ray_distance", -1.0) or -1.0)
                <= args.fog_end_distance
                for ray, _ in pixel_join_pairs
            )
            if args.fog_end_distance is not None
            else None
        ),
        "matched_candidates_beyond_fog_end": (
            sum(
                float(ray.get("screen_ray_distance", -1.0) or -1.0)
                > args.fog_end_distance
                for ray, _ in pixel_join_pairs
            )
            if args.fog_end_distance is not None
            else None
        ),
        "samples": [
            {
                "frame_epoch": ray.get("frame_epoch"),
                "chunk": [ray.get("cx"), ray.get("cy"), ray.get("cz")],
                "block": [
                    ray.get("screen_ray_block_x"),
                    ray.get("screen_ray_block_y"),
                    ray.get("screen_ray_block_z"),
                ],
                "distance": ray.get("screen_ray_distance"),
                "mesh_satisfying": ray.get("screen_ray_mesh_satisfying"),
                "repairable_geometry_debt": ray.get(
                    "screen_ray_repairable_geometry_debt"
                ),
                "pixel_depth": pixel.get("renderer_pixel_pretransparent_depth"),
                "pixel_rgba": pixel.get("renderer_pixel_rgba"),
                "pixel_rgba_pretransparent": unpack_pixel_rgba(
                    pixel, "renderer_pixel_pretransparent_rgba"
                ),
                "pixel_fog_start": pixel.get("renderer_pixel_fog_start"),
                "pixel_fog_end": pixel.get("renderer_pixel_fog_end"),
                "pixel_opaque_surface_valid": pixel.get(
                    "renderer_pixel_opaque_surface_valid"
                ),
                "pixel_voxel_ray_state": pixel.get(
                    "renderer_pixel_voxel_ray_state"
                ),
                "pixel_voxel_hit": [
                    pixel.get("renderer_pixel_voxel_hit_x"),
                    pixel.get("renderer_pixel_voxel_hit_y"),
                    pixel.get("renderer_pixel_voxel_hit_z"),
                ],
                "pixel_voxel_face_source_valid": pixel.get(
                    "renderer_pixel_voxel_face_source_valid"
                ),
                "pixel_chunk_visible_mdi_indices": pixel.get(
                    "renderer_pixel_voxel_chunk_mdi_visible_index_count"
                ),
            }
            for ray, pixel in pixel_join_pairs[:32]
        ],
    }

    result = {
        "perf_jsonl": str(args.perf_jsonl),
        "trace_counts": dict(sorted(trace_counts.items())),
        "focus_slices": {
            "states": sorted_counter(focus_states),
            "visual_classes": sorted_counter(focus_visual_classes),
            "terrain_complete": sorted_counter(focus_terrain_complete),
        },
        "screen_rays": {
            "state_counts": sorted_counter(screen_state),
            "candidate_counts_by_state": sorted_counter(screen_candidates_by_state),
            "selected_counts_by_state": sorted_counter(screen_selected_by_state),
            "candidate_issue_combinations": sorted_counter(screen_combos),
            "candidate_distance": candidate_distance,
            "candidate_samples": screen_examples,
            "same_frame_pixel_join": pixel_join,
        },
        "view_frustum": {
            "sample_states": sorted_counter(frustum_states),
            "draw_summary": sorted_counter(frustum_draw_summary),
            "state_semantics": {
                "1": "no drawable",
                "2": "drawable missing from CPU/packed snapshot",
                "3": "CPU/packed ref rejected by render-ready gate",
                "4": "ready CPU/packed ref sampled for post-cull MDI state",
            },
        },
        "camera_band_peak_totals": peak_totals,
        "camera_band_peak_slices": {
            kind: [peak_rows[kind][coord] for coord in sorted(peak_rows[kind])]
            for kind in peak_rows
        },
        "metric_caveat": (
            "draw_oracle_missing_resident_n is currently assigned directly from "
            "unfinished_visual; it is not independent framebuffer evidence."
        ),
    }

    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")

    print(
        f"visual trace summary: {sum(trace_counts.values())} trace rows; "
        f"{trace_counts['screen_ray_candidate_trace']} screen rays, "
        f"{len(peak_rows['camera_band_unowned_peak_slice_trace'])} unowned peak slices",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
