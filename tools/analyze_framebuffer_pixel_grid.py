#!/usr/bin/env python3
"""Summarize sparse in-renderer framebuffer probes from a perf JSONL log.

The capture is a grid of actual framebuffer samples, not a screenshot. This
tool reports raw color/depth and voxel-ray facts without treating any one
threshold or mesh proxy as proof of a visible terrain hole.
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = round((len(ordered) - 1) * fraction)
    return ordered[index]


def summarize(path: Path) -> dict[str, Any]:
    samples = 0
    malformed = 0
    captures: Counter[tuple[int, int]] = Counter()
    capture_flags: dict[tuple[int, int], int] = {}
    trigger_counts: Counter[str] = Counter()
    ray_states: Counter[str] = Counter()
    ray_state_color_counts: dict[str, Counter[str]] = defaultdict(Counter)
    pixel_counts: Counter[str] = Counter()
    focus_counts: dict[int, Counter[str]] = defaultdict(Counter)
    luminances: list[float] = []
    focus_cx_values: list[int] = []

    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("{"):
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                malformed += 1
                continue
            if row.get("kind") != "renderer_pixel_probe":
                continue

            samples += 1
            probe_id = int(row.get("renderer_pixel_probe_id", -1))
            frame_epoch = int(row.get("frame_epoch", -1))
            key = (probe_id, frame_epoch)
            captures[key] += 1
            flags = int(row.get("renderer_pixel_probe_trigger_flags", 0))
            capture_flags[key] = flags

            rgba = int(row.get("renderer_pixel_rgba", 0))
            red = (rgba >> 24) & 0xFF
            green = (rgba >> 16) & 0xFF
            blue = (rgba >> 8) & 0xFF
            luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue
            luminances.append(luminance)
            if max(red, green, blue) <= 8:
                pixel_counts["rgb_max_le_8"] += 1
            if max(red, green, blue) <= 16:
                pixel_counts["rgb_max_le_16"] += 1
            if luminance <= 32:
                pixel_counts["luminance_le_32"] += 1
            if luminance <= 64:
                pixel_counts["luminance_le_64"] += 1

            try:
                focus_cx = int(row.get("focus_cx", 0))
            except (TypeError, ValueError):
                focus_cx = 0
            focus_cx_values.append(focus_cx)
            focus = focus_counts[focus_cx]
            focus["samples"] += 1
            focus["luminance_sum"] += luminance
            if max(red, green, blue) <= 8:
                focus["rgb_max_le_8"] += 1

            depth = float(row.get("renderer_pixel_pretransparent_depth", 1.0))
            if depth < 0.99999:
                pixel_counts["opaque_depth_written"] += 1
            if int(row.get("renderer_pixel_surface_valid", 0)):
                pixel_counts["sea_plane_surface_ray_valid"] += 1

            state = int(row.get("renderer_pixel_voxel_ray_state", 0))
            state_key = str(state)
            ray_states[state_key] += 1
            state_color = ray_state_color_counts[state_key]
            state_color["samples"] += 1
            state_color["luminance_sum"] += luminance
            state_color["luminance_le_32"] += luminance <= 32
            state_color["luminance_le_64"] += luminance <= 64
            state_color["rgb_max_le_16"] += max(red, green, blue) <= 16
            fog_end = float(row.get("renderer_pixel_fog_end", -1.0))
            if state == 1:
                pixel_counts["opaque_voxel_ray_hit"] += 1
                hit_distance = float(
                    row.get("renderer_pixel_voxel_hit_distance", -1.0)
                )
                if depth >= 0.99999:
                    pixel_counts["opaque_voxel_hit_clear_opaque_depth"] += 1
                if fog_end > 0 and 0 <= hit_distance <= fog_end:
                    pixel_counts["opaque_voxel_hit_within_fog_end"] += 1
                    if depth >= 0.99999:
                        pixel_counts[
                            "opaque_voxel_hit_within_fog_clear_opaque_depth"
                        ] += 1
                        if int(row.get("renderer_pixel_surface_valid", 0)):
                            pixel_counts[
                                "opaque_voxel_hit_within_fog_clear_depth_with_surface_ray"
                            ] += 1
            elif state == 2:
                pixel_counts["unloaded_voxel_ray_witness"] += 1
                unloaded_distance = float(
                    row.get("renderer_pixel_voxel_unloaded_distance", -1.0)
                )
                if fog_end > 0 and 0 <= unloaded_distance <= fog_end:
                    pixel_counts["unloaded_witness_within_fog_end"] += 1
                elif fog_end > 0 and unloaded_distance > fog_end:
                    pixel_counts["unloaded_witness_beyond_fog_end"] += 1
            elif state == 3:
                pixel_counts["no_opaque_voxel_hit_in_range"] += 1

            if int(row.get("renderer_pixel_voxel_mesh_ray_gap", 0)):
                pixel_counts["opaque_mesh_ray_gap"] += 1
                mesh_distance = float(
                    row.get("renderer_pixel_voxel_mesh_ray_distance", -1.0)
                )
                if fog_end > 0 and 0 <= mesh_distance <= fog_end:
                    pixel_counts["opaque_mesh_ray_gap_within_fog_end"] += 1
                elif fog_end > 0 and mesh_distance > fog_end:
                    pixel_counts["opaque_mesh_ray_gap_beyond_fog_end"] += 1

    for flags in capture_flags.values():
        trigger_counts[str(flags)] += 1

    if not samples:
        raise ValueError(f"no renderer_pixel_probe rows found in {path}")

    capture_size_counts = Counter(str(count) for count in captures.values())
    ray_state_color = {
        state: {
            "samples": counts["samples"],
            "rgb_max_le_16": counts["rgb_max_le_16"],
            "luminance_le_32": counts["luminance_le_32"],
            "luminance_le_64": counts["luminance_le_64"],
            "mean_luminance": round(
                counts["luminance_sum"] / counts["samples"], 3
            ),
        }
        for state, counts in sorted(ray_state_color_counts.items())
    }
    focus_summary = {
        str(cx): {
            "samples": counts["samples"],
            "rgb_max_le_8": counts["rgb_max_le_8"],
            "mean_luminance": round(
                counts["luminance_sum"] / counts["samples"], 3
            ),
        }
        for cx, counts in sorted(focus_counts.items())
        if -6 <= cx <= 6
    }
    return {
        "schema": "framebuffer_pixel_grid_summary.v1",
        "source_perf_jsonl": str(path),
        "scope": (
            "Sparse samples read from the renderer framebuffer; this is not a "
            "full-frame capture or full-frustum guarantee."
        ),
        "samples": samples,
        "captures": len(captures),
        "capture_samples_per_frame": dict(sorted(capture_size_counts.items())),
        "capture_trigger_flag_counts": dict(sorted(trigger_counts.items())),
        "malformed_rows_skipped": malformed,
        "focus_cx_range": [min(focus_cx_values), max(focus_cx_values)],
        "m335_start_corridor_pixels_by_focus_cx": focus_summary,
        "color": {
            "rgb_max_le_8": pixel_counts["rgb_max_le_8"],
            "rgb_max_le_16": pixel_counts["rgb_max_le_16"],
            "luminance_le_32": pixel_counts["luminance_le_32"],
            "luminance_le_64": pixel_counts["luminance_le_64"],
            "luminance_min": round(min(luminances), 3),
            "luminance_p01": round(percentile(luminances, 0.01) or 0.0, 3),
            "luminance_p10": round(percentile(luminances, 0.10) or 0.0, 3),
            "luminance_median": round(statistics.median(luminances), 3),
        },
        "voxel_ray_states": dict(sorted(ray_states.items())),
        "voxel_ray_state_color": ray_state_color,
        "ray_and_depth_counts": dict(sorted(pixel_counts.items())),
        "interpretation_limits": [
            "A raw near-black threshold is not by itself a missing-terrain diagnosis.",
            "An unloaded ray witness is a visibility candidate only when its distance is within the recorded fog end.",
            "A clear opaque depth with a solid voxel ray can be explained by transparent water; inspect the recorded surface ray and color before classifying it.",
            "Periodic pixel grids can miss transient defects between captures; use event-aligned captures to join hole edges to pixels.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Summarize renderer framebuffer pixel-grid samples and their "
            "voxel-ray/depth witnesses."
        )
    )
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    summary = summarize(args.perf_jsonl)
    encoded = json.dumps(summary, indent=2, ensure_ascii=False) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(json.dumps({
        "samples": summary["samples"],
        "captures": summary["captures"],
        "capture_samples_per_frame": summary["capture_samples_per_frame"],
        "capture_trigger_flag_counts": summary["capture_trigger_flag_counts"],
        "color": summary["color"],
        "voxel_ray_states": summary["voxel_ray_states"],
        "voxel_ray_state_color": summary["voxel_ray_state_color"],
        "ray_and_depth_counts": summary["ray_and_depth_counts"],
        "json_out": str(args.json_out) if args.json_out else None,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
