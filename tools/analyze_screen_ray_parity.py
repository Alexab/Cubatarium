#!/usr/bin/env python3
"""Join streaming screen-ray samples to renderer pixel-ray witnesses."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(fraction * len(ordered)) - 1))
    return ordered[index]


def vector(row: dict[str, Any], prefix: str, axes: str = "xyz") -> tuple[float, ...]:
    return tuple(float(row.get(f"{prefix}_{axis}", 0.0)) for axis in axes)


def normalize(value: tuple[float, ...]) -> tuple[float, ...] | None:
    length = math.sqrt(sum(component * component for component in value))
    if length <= 1.0e-9 or not math.isfinite(length):
        return None
    return tuple(component / length for component in value)


def summarize(values: list[float]) -> dict[str, float | int | None]:
    if not values:
        return {"count": 0, "p50": None, "p95": None, "max": None}
    return {
        "count": len(values),
        "p50": statistics.median(values),
        "p95": percentile(values, 0.95),
        "max": max(values),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--viewport-width", type=int, required=True)
    parser.add_argument("--viewport-height", type=int, required=True)
    parser.add_argument("--camera-x-min", type=float)
    parser.add_argument("--camera-x-max", type=float)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    if args.viewport_width <= 0 or args.viewport_height <= 0:
        parser.error("viewport dimensions must be positive")
    if (args.camera_x_min is None) != (args.camera_x_max is None):
        parser.error("provide both --camera-x-min and --camera-x-max")

    rays: dict[tuple[int, int, int], list[dict[str, Any]]] = defaultdict(list)
    pixels: list[dict[str, Any]] = []
    with args.perf_jsonl.open("r", encoding="utf-8") as source:
        for line in source:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            kind = row.get("kind")
            if kind == "screen_ray_candidate_trace":
                origin_x = float(row.get("screen_ray_origin_x", 0.0))
                if args.camera_x_min is not None and not (
                    args.camera_x_min <= origin_x <= args.camera_x_max
                ):
                    continue
                # Screen-ray coordinates are top-left pixel centers.
                key = (
                    int(row.get("frame_epoch", 0)),
                    round(float(row.get("screen_ray_x", 0.0)) * 2.0),
                    round(float(row.get("screen_ray_y", 0.0)) * 2.0),
                )
                rays[key].append(row)
            elif kind == "renderer_pixel_probe":
                camera_x = float(row.get("renderer_pixel_camera_pos_x", 0.0))
                if args.camera_x_min is not None and not (
                    args.camera_x_min <= camera_x <= args.camera_x_max
                ):
                    continue
                pixels.append(row)

    joined: list[tuple[dict[str, Any], dict[str, Any]]] = []
    for pixel in pixels:
        # GL readback coordinates are bottom-left pixel indices. Convert the
        # pixel center to the top-left center used by the streaming selector.
        screen_x = float(pixel.get("renderer_pixel_x", 0)) + 0.5
        screen_y = args.viewport_height - (
            float(pixel.get("renderer_pixel_y", 0)) + 0.5
        )
        key = (
            int(pixel.get("frame_epoch", 0)),
            round(screen_x * 2.0),
            round(screen_y * 2.0),
        )
        joined.extend((pixel, ray) for ray in rays.get(key, ()))

    origin_deltas: list[float] = []
    origin_axis_deltas: dict[str, list[float]] = {axis: [] for axis in "xyz"}
    direction_errors: list[float] = []
    origin_basis_counts: Counter[str] = Counter()
    direction_basis_counts: Counter[str] = Counter()
    pixel_trigger_flags: Counter[str] = Counter()
    state_pairs: Counter[str] = Counter()
    hit_blocks: Counter[str] = Counter()
    mismatch_examples: list[dict[str, Any]] = []
    for pixel, ray in joined:
        trigger_flags = int(pixel.get("renderer_pixel_probe_trigger_flags", 0))
        pixel_trigger_flags[str(trigger_flags)] += 1
        has_renderer_ray = all(
            f"renderer_pixel_ray_origin_{axis}" in pixel for axis in "xyz"
        )
        if has_renderer_ray:
            pixel_origin = vector(pixel, "renderer_pixel_ray_origin")
            origin_basis = "renderer_camera_space_ray"
            origin_basis_counts["renderer_camera_space_ray"] += 1
        else:
            pixel_origin = vector(pixel, "renderer_pixel_camera_pos")
            origin_basis = "renderer_position_fallback"
            origin_basis_counts["renderer_position_fallback"] += 1
        ray_origin = vector(ray, "screen_ray_origin")
        delta = tuple(pixel_origin[i] - ray_origin[i] for i in range(3))
        origin_deltas.append(math.sqrt(sum(component * component for component in delta)))
        for index, axis in enumerate("xyz"):
            origin_axis_deltas[axis].append(delta[index])

        pixel_state = int(pixel.get("renderer_pixel_voxel_ray_state", 0))
        ray_state = int(ray.get("screen_ray_state", 0))
        state_pairs[f"stream_{ray_state}_pixel_{pixel_state}"] += 1
        block_differs = False
        if ray_state == pixel_state == 1:
            ray_block = tuple(
                int(ray.get(f"screen_ray_block_{axis}", 0)) for axis in "xyz"
            )
            pixel_block = tuple(
                int(pixel.get(f"renderer_pixel_voxel_hit_{axis}", 0))
                for axis in "xyz"
            )
            block_differs = ray_block != pixel_block
            hit_blocks["different" if block_differs else "same"] += 1

        if all(f"renderer_pixel_ray_direction_{axis}" in pixel for axis in "xyz"):
            renderer_direction = normalize(
                vector(pixel, "renderer_pixel_ray_direction")
            )
            direction_basis_counts["renderer_camera_space_ray"] += 1
        elif int(pixel.get("renderer_pixel_opaque_surface_valid", 0)):
            depth_vector = tuple(
                float(pixel.get(f"renderer_pixel_opaque_surface_{axis}", 0.0))
                - pixel_origin[i]
                for i, axis in enumerate("xyz")
            )
            renderer_direction = normalize(depth_vector)
            direction_basis_counts["opaque_depth_line_of_sight_fallback"] += 1
        else:
            renderer_direction = None
        streaming_direction = normalize(vector(ray, "screen_ray_direction"))
        if renderer_direction is not None and streaming_direction is not None:
            dot = sum(
                renderer_direction[i] * streaming_direction[i]
                for i in range(3)
            )
            direction_errors.append(
                math.degrees(math.acos(max(-1.0, min(1.0, dot))))
            )

        if block_differs and len(mismatch_examples) < 20:
            mismatch_examples.append(
                {
                    "frame_epoch": int(pixel.get("frame_epoch", 0)),
                    "screen": [screen_x, screen_y],
                    "renderer_ray_origin": pixel_origin,
                    "renderer_origin_basis": origin_basis,
                    "stream_ray_origin": ray_origin,
                    "origin_delta_renderer_minus_stream": delta,
                    "stream_block": ray_block,
                    "renderer_block": pixel_block,
                }
            )

    report = {
        "perf_jsonl": str(args.perf_jsonl),
        "viewport": [args.viewport_width, args.viewport_height],
        "camera_x_window": [args.camera_x_min, args.camera_x_max],
        "sample_counts": {
            "stream_rays_in_window": sum(len(samples) for samples in rays.values()),
            "renderer_pixels_in_window": len(pixels),
            "exact_coordinate_joins": len(joined),
            "camera_band_event_pixel_probes": sum(
                bool(
                    int(pixel.get("renderer_pixel_probe_trigger_flags", 0))
                    & (1 << 3)
                )
                for pixel in pixels
            ),
        },
        "pixel_probe_trigger_flag_counts": dict(pixel_trigger_flags),
        "renderer_ray_minus_stream_ray_origin_delta_blocks": summarize(origin_deltas),
        "origin_comparison_basis_counts": dict(origin_basis_counts),
        "renderer_minus_stream_origin_axis_delta_blocks": {
            axis: {
                "mean": statistics.mean(values) if values else None,
                "p50": statistics.median(values) if values else None,
                "min": min(values) if values else None,
                "max": max(values) if values else None,
            }
            for axis, values in origin_axis_deltas.items()
        },
        "renderer_vs_stream_direction_delta_degrees": summarize(direction_errors),
        "direction_comparison_basis_counts": dict(direction_basis_counts),
        "ray_state_pairs": dict(state_pairs),
        "opaque_hit_block_pairs": dict(hit_blocks),
        "different_hit_examples": mismatch_examples,
        "interpretation_note": (
            "When available, origin and direction come from the exact renderer "
            "CameraSpaceRay used by its voxel DDA; older logs fall back to raw camera "
            "position and the line to the reconstructed depth point. Ray-state and "
            "block differences can also reflect world residency changes between "
            "streaming and draw phases. A join is diagnostic and does not by itself "
            "prove a framebuffer defect."
        ),
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
