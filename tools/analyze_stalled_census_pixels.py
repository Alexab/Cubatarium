#!/usr/bin/env python3
"""Join stalled visual-black census slices to same-frame framebuffer rays.

This is a sparse witness analyzer, not a full-frustum or blank-pixel detector.
It computes ray/AABB intersections for exact ``cause=3`` slices and reports
whether a sampled opaque depth surface lies before the ray enters that slice.
"""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


CAUSE_KIND = "visual_black_trace"
PIXEL_KIND = "renderer_pixel_probe"
STALLED_CAUSE = 3
STALLED_PIXEL_TRIGGER = 1 << 4
LIFECYCLE_KINDS = {
    "demand_transition_trace",
    "visual_lifecycle_trace",
    "mesh_repair_trace",
    "mesh_schedule_trace",
    "mesh_watched_schedule_trace",
    "view_draw_gate_trace",
    "view_frustum_coverage_trace",
    "screen_ray_candidate_trace",
}


def int3(row: dict[str, Any], prefix: str) -> tuple[int, int, int]:
    keys = ("cx", "cy", "cz") if not prefix else tuple(
        f"{prefix}_{axis}" for axis in "xyz"
    )
    return tuple(int(row.get(key, 0)) for key in keys)  # type: ignore[return-value]


def float3(row: dict[str, Any], prefix: str) -> tuple[float, float, float] | None:
    keys = [f"{prefix}_{axis}" for axis in "xyz"]
    if not all(key in row for key in keys):
        return None
    value = tuple(float(row[key]) for key in keys)
    if not all(math.isfinite(component) for component in value):
        return None
    return value  # type: ignore[return-value]


def ray_aabb_interval(
    origin: tuple[float, float, float],
    direction: tuple[float, float, float],
    minimum: tuple[float, float, float],
    maximum: tuple[float, float, float],
) -> tuple[float, float] | None:
    """Return nonnegative ray entry/exit distances for an axis-aligned box."""
    enter = -math.inf
    leave = math.inf
    for axis in range(3):
        d = direction[axis]
        o = origin[axis]
        if abs(d) <= 1.0e-12:
            if o < minimum[axis] or o > maximum[axis]:
                return None
            continue
        t0 = (minimum[axis] - o) / d
        t1 = (maximum[axis] - o) / d
        enter = max(enter, min(t0, t1))
        leave = min(leave, max(t0, t1))
        if leave < enter:
            return None
    if leave < 0.0:
        return None
    return max(enter, 0.0), leave


def ray_summary(pixel: dict[str, Any], target: tuple[int, int, int], chunk_size: int) -> dict[str, Any]:
    origin = float3(pixel, "renderer_pixel_ray_origin")
    direction = float3(pixel, "renderer_pixel_ray_direction")
    if origin is None or direction is None:
        return {"classification": "missing_ray"}
    length = math.sqrt(sum(value * value for value in direction))
    if not math.isfinite(length) or length <= 1.0e-12:
        return {"classification": "invalid_ray"}
    direction = tuple(value / length for value in direction)  # type: ignore[assignment]

    minimum = tuple(c * chunk_size for c in target)
    maximum = tuple((c + 1) * chunk_size for c in target)
    interval = ray_aabb_interval(origin, direction, minimum, maximum)
    if interval is None:
        return {"classification": "misses_chunk_aabb"}

    entry, leave = interval
    opaque_valid = int(pixel.get("renderer_pixel_opaque_surface_valid", 0)) == 1
    opaque_distance = float(pixel.get("renderer_pixel_opaque_hit_distance", -1.0))
    opaque_chunk = (
        [int(pixel.get(f"renderer_pixel_opaque_chunk_{axis}", 0)) for axis in "xyz"]
        if opaque_valid
        else None
    )
    opaque_surface = (
        [float(pixel.get(f"renderer_pixel_opaque_surface_{axis}", 0.0)) for axis in "xyz"]
        if opaque_valid
        else None
    )

    if opaque_valid and math.isfinite(opaque_distance) and 0.0 <= opaque_distance < entry:
        classification = "opaque_surface_before_chunk"
    elif opaque_valid and opaque_chunk == list(target):
        classification = "opaque_hit_in_target_chunk"
    elif opaque_valid and opaque_distance >= entry:
        classification = "opaque_surface_at_or_after_chunk_entry"
    else:
        classification = "no_opaque_depth_witness"

    return {
        "classification": classification,
        "ray_entry_distance": round(entry, 5),
        "ray_exit_distance": round(leave, 5),
        "opaque_surface_valid": opaque_valid,
        "opaque_distance": round(opaque_distance, 5) if opaque_valid else None,
        "opaque_chunk": opaque_chunk,
        "opaque_surface": opaque_surface,
        "pixel": [pixel.get("renderer_pixel_x"), pixel.get("renderer_pixel_y")],
        "rgb": [
            (int(pixel.get("renderer_pixel_rgba", 0)) >> 24) & 0xFF,
            (int(pixel.get("renderer_pixel_rgba", 0)) >> 16) & 0xFF,
            (int(pixel.get("renderer_pixel_rgba", 0)) >> 8) & 0xFF,
        ],
    }


def lifecycle_summary(row: dict[str, Any]) -> dict[str, Any]:
    kind = str(row.get("kind", ""))
    common = {
        key: row[key]
        for key in (
            "kind", "frame_epoch", "world_epoch", "incarnation", "cx", "cy", "cz"
        )
        if key in row
    }
    if kind == "demand_transition_trace":
        fields = (
            "event", "result", "attempt_id", "previous_attempt_id", "stage",
            "previous_stage", "desired_geom_rev", "published_geom_rev",
            "has_active_attempt", "mesh_dirty_queue_kind", "mesh_dirty_queue_size",
            "mesh_owner_flags", "async_builder_inflight", "gpu_extract_inflight",
            "pending_gpu_apply", "pending_gpu_queued",
        )
    elif kind == "visual_lifecycle_trace":
        fields = (
            "stage", "terminal_reason", "attempt_id", "desired_geom_rev",
            "source_geom_rev", "published_geom_rev", "source_light_rev",
            "published_light_rev", "outcome", "elapsed_ms",
        )
    elif kind == "screen_ray_candidate_trace":
        fields = (
            "screen_ray_x", "screen_ray_y", "screen_ray_state", "screen_ray_gap",
            "screen_ray_block_x", "screen_ray_block_y", "screen_ray_block_z",
        )
    else:
        fields = (
            "cause", "active_stage", "attempt_id", "desired_geom_rev",
            "published_geom_rev", "demand_published_geom_rev", "mesh_revision",
            "mesh_dirty_queue_kind", "mesh_dirty_queue_index", "mesh_dirty_queue_size",
            "mesh_dirty_queue_age_frames", "mesh_work_owner_flags", "flags",
            "relight_owner_flags", "column_flow_ticket_flags",
        )
    common.update({key: row[key] for key in fields if key in row})
    return common


def lifecycle_identity(sample: dict[str, Any], event: dict[str, Any]) -> str:
    """Classify lifecycle correlation without treating missing IDs as a match."""
    missing = False
    for key in ("world_epoch", "incarnation"):
        sample_id = int(sample.get(key) or 0)
        event_id = int(event.get(key) or 0)
        if sample_id <= 0 or event_id <= 0:
            missing = True
        elif sample_id != event_id:
            return "mismatch"
    return "unverified" if missing else "exact"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--chunk-size", type=int, default=16)
    parser.add_argument("--lifecycle-frame-window", type=int, default=512)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    if args.chunk_size <= 0:
        parser.error("--chunk-size must be positive")
    if args.lifecycle_frame_window < 0:
        parser.error("--lifecycle-frame-window must be nonnegative")

    census: dict[tuple[int, tuple[int, int, int]], dict[str, Any]] = {}
    pixels: dict[int, list[dict[str, Any]]] = defaultdict(list)
    lifecycle: dict[tuple[int, int, int], list[dict[str, Any]]] = defaultdict(list)
    with args.perf_jsonl.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            epoch = int(row.get("frame_epoch", 0))
            if row.get("kind") == CAUSE_KIND and int(row.get("cause", -1)) == STALLED_CAUSE:
                census[(epoch, int3(row, ""))] = {
                    "line": line_number,
                    "frame_epoch": epoch,
                    "chunk": list(int3(row, "")),
                    "world_epoch": row.get("world_epoch"),
                    "incarnation": row.get("incarnation"),
                    "attempt_id": row.get("attempt_id"),
                    "active_stage": row.get("active_stage"),
                    "desired_geom_rev": row.get("desired_geom_rev"),
                    "published_geom_rev": row.get("published_geom_rev"),
                    "mesh_dirty_queue_kind": row.get("mesh_dirty_queue_kind"),
                    "mesh_dirty_queue_size": row.get("mesh_dirty_queue_size"),
                    "mesh_work_owner_flags": row.get("mesh_work_owner_flags"),
                    "relight_owner_flags": row.get("relight_owner_flags"),
                    "column_flow_ticket_flags": row.get("column_flow_ticket_flags"),
                    "renderer_column_draw_ok": row.get("renderer_column_draw_ok"),
                }
            elif row.get("kind") == PIXEL_KIND and epoch:
                trigger_flags = int(row.get("renderer_pixel_probe_trigger_flags", 0))
                if trigger_flags & STALLED_PIXEL_TRIGGER:
                    pixels[epoch].append(row)
            elif row.get("kind") in LIFECYCLE_KINDS:
                coord = int3(row, "")
                lifecycle[coord].append(lifecycle_summary(row))

    result_rows: list[dict[str, Any]] = []
    classifications: Counter[str] = Counter()
    for key, sample in sorted(census.items()):
        epoch, chunk = key
        witnesses = [
            ray_summary(pixel, chunk, args.chunk_size)
            for pixel in pixels.get(epoch, ())
        ]
        incarnation = sample.get("incarnation")
        world_epoch = sample.get("world_epoch")
        lifecycle_candidates = [
            event
            for event in lifecycle.get(chunk, ())
            if epoch <= int(event.get("frame_epoch", 0))
            <= epoch + args.lifecycle_frame_window
        ]
        identity_groups: dict[str, list[dict[str, Any]]] = {
            "exact": [],
            "unverified": [],
            "mismatch": [],
        }
        for event in lifecycle_candidates:
            identity_groups[lifecycle_identity(sample, event)].append(event)
        lifecycle_rows = identity_groups["exact"]
        lifecycle_rows.sort(key=lambda event: int(event.get("frame_epoch", 0)))
        for rows in identity_groups.values():
            rows.sort(key=lambda event: int(event.get("frame_epoch", 0)))
        per_sample = Counter(str(witness["classification"]) for witness in witnesses)
        classifications.update(per_sample)
        result_rows.append(
            {
                **sample,
                "event_pixel_count": len(pixels.get(epoch, ())),
                "ray_classification_counts": dict(sorted(per_sample.items())),
                "lifecycle_window_frames": args.lifecycle_frame_window,
                "lifecycle_event_count": len(lifecycle_rows),
                "lifecycle_events": lifecycle_rows,
                "lifecycle_identity_unverified_count": len(
                    identity_groups["unverified"]
                ),
                "lifecycle_identity_unverified_events": identity_groups[
                    "unverified"
                ],
                "lifecycle_identity_mismatch_count": len(
                    identity_groups["mismatch"]
                ),
                "lifecycle_identity_mismatch_events": identity_groups[
                    "mismatch"
                ],
                "intersecting_rays": [
                    witness
                    for witness in witnesses
                    if witness["classification"] != "misses_chunk_aabb"
                ],
            }
        )

    result = {
        "perf_jsonl": str(args.perf_jsonl),
        "chunk_size": args.chunk_size,
        "stalled_census_slice_count": len(census),
        "event_pixel_frame_count": len(pixels),
        "event_pixel_count": sum(len(rows) for rows in pixels.values()),
        "same_epoch_census_with_pixels": sum(
            1 for epoch, _ in census if pixels.get(epoch)
        ),
        "ray_classification_counts": dict(sorted(classifications.items())),
        "limitations": [
            "Pixel rays are a sparse screen sample; misses do not imply frustum exclusion.",
            "A census cause is a streaming/repair attribution, not proof of a visible framebuffer hole.",
            "An opaque surface before a chunk AABB entry only establishes occlusion for that sampled ray.",
            "Ray classifications are per census-slice comparison; a pixel row repeats when multiple slices share its frame.",
            "Lifecycle events with missing IDs or a mismatched incarnation/world epoch are reported separately from exact matches.",
        ],
        "samples": result_rows,
    }
    payload = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(payload, encoding="utf-8")
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
