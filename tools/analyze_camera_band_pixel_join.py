#!/usr/bin/env python3
"""Join camera-band no-drawable slice traces to same-frame pixel voxel rays.

This is a candidate-witness extractor, not a blank-pixel detector. A voxel DDA
hit may pass through cutout material, and a sampled pixel may be occluded by
nearer geometry. Review the returned depth distance, chunk, material, and RGB
before classifying any match as a visible hole.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


TARGET_KINDS = {
    "camera_band_no_drawable_peak_slice_trace",
    "camera_band_unowned_peak_slice_trace",
}
FRUSTUM_KIND = "view_frustum_coverage_trace"


def key_for(frame_epoch: Any, cx: Any, cy: Any, cz: Any) -> tuple[int, int, int, int]:
    return int(frame_epoch), int(cx), int(cy), int(cz)


def pixel_summary(record: dict[str, Any]) -> dict[str, Any]:
    rgba = int(record.get("renderer_pixel_rgba", 0))
    rgb = [(rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255]
    depth_valid = int(record.get("renderer_pixel_opaque_surface_valid", 0)) == 1
    voxel_distance = float(record.get("renderer_pixel_voxel_hit_distance", -1.0))
    depth_distance = float(record.get("renderer_pixel_opaque_hit_distance", -1.0))
    distance_delta = (
        round(depth_distance - voxel_distance, 4)
        if depth_valid and voxel_distance >= 0.0 and depth_distance >= 0.0
        else None
    )
    return {
        "pixel": [int(record.get("renderer_pixel_x", 0)),
                  int(record.get("renderer_pixel_y", 0))],
        "rgb": rgb,
        "luminance": round(0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2], 2),
        "voxel_hit_block_id": int(record.get("renderer_pixel_voxel_hit_block_id", -1)),
        "voxel_hit_distance": voxel_distance,
        "depth_surface_valid": depth_valid,
        "depth_chunk": [
            int(record.get("renderer_pixel_opaque_chunk_x", 0)),
            int(record.get("renderer_pixel_opaque_chunk_y", 0)),
            int(record.get("renderer_pixel_opaque_chunk_z", 0)),
        ] if depth_valid else None,
        "depth_hit_distance": depth_distance if depth_valid else None,
        "depth_minus_voxel_distance": distance_delta,
        "depth_ref_flags": int(record.get("renderer_pixel_opaque_ref_flags", 0)),
        "voxel_face_source_valid": int(
            record.get("renderer_pixel_voxel_face_source_valid", 0)
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    traces: dict[str, list[dict[str, Any]]] = defaultdict(list)
    pixel_hits: dict[tuple[int, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    frustum_hits: dict[tuple[int, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    pixel_probe_epochs: set[int] = set()
    frustum_probe_epochs: set[int] = set()
    kind_counts: Counter[str] = Counter()

    with args.perf_jsonl.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            try:
                record = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"invalid JSON on line {line_number}: {exc}") from exc
            kind = record.get("kind")
            if kind in TARGET_KINDS:
                kind_counts[kind] += 1
                traces[kind].append(record)
            elif kind == FRUSTUM_KIND:
                frustum_probe_epochs.add(int(record.get("frame_epoch", 0)))
                frustum_key = key_for(
                    record.get("frame_epoch", 0), record.get("cx", 0),
                    record.get("cy", 0), record.get("cz", 0),
                )
                frustum_hits[frustum_key].append({
                    "renderer_runtime_cull_visible": int(
                        record.get("renderer_runtime_cull_visible", 0)
                    ),
                    "mdi_resident_pass_flags": int(
                        record.get("renderer_mdi_resident_pass_flags", 0)
                    ),
                    "mdi_visible_pass_flags": int(
                        record.get("renderer_mdi_visible_pass_flags", 0)
                    ),
                    "mdi_visible_index_count": int(
                        record.get("renderer_mdi_visible_index_count", 0)
                    ),
                })
            elif kind == "renderer_pixel_probe":
                pixel_probe_epochs.add(int(record.get("frame_epoch", 0)))
                if int(record.get("renderer_pixel_voxel_ray_state", 0)) != 1:
                    continue
                voxel_key = key_for(
                    record.get("frame_epoch", 0),
                    record.get("renderer_pixel_voxel_chunk_x", 0),
                    record.get("renderer_pixel_voxel_chunk_y", 0),
                    record.get("renderer_pixel_voxel_chunk_z", 0),
                )
                pixel_hits[voxel_key].append(pixel_summary(record))

    joined_by_kind: dict[str, list[dict[str, Any]]] = {}
    for kind in sorted(TARGET_KINDS):
        joined: list[dict[str, Any]] = []
        for trace in traces.get(kind, []):
            target_key = key_for(
                trace.get("frame_epoch", 0), trace.get("cx", 0),
                trace.get("cy", 0), trace.get("cz", 0),
            )
            matched = pixel_hits.get(target_key, [])
            matched_frustum = frustum_hits.get(target_key, [])
            joined.append({
                "frame_epoch": target_key[0],
                "pixel_probe_frame_present": target_key[0] in pixel_probe_epochs,
                "frustum_probe_frame_present": target_key[0] in frustum_probe_epochs,
                "chunk": list(target_key[1:]),
                "focus_chunk": [
                    int(trace.get("focus_cx", 0)),
                    int(trace.get("focus_cz", 0)),
                ],
                "camera": [
                    int(trace.get("camera_x", 0)),
                    int(trace.get("camera_y", 0)),
                    int(trace.get("camera_z", 0)),
                ],
                "non_air_blocks": int(trace.get("non_air_blocks", 0)),
                "mesh_dirty_queue_kind": int(trace.get("mesh_dirty_queue_kind", 0)),
                "mesh_work_owner_flags": int(trace.get("mesh_work_owner_flags", 0)),
                "demand_geom_rev": int(trace.get("desired_geom_rev", 0)),
                "published_geom_rev": int(trace.get("published_geom_rev", 0)),
                "same_frame_voxel_pixel_hit_count": len(matched),
                "pixel_hits": matched,
                "same_frame_frustum_witness_count": len(matched_frustum),
                "frustum_witnesses": matched_frustum,
            })
        joined_by_kind[kind] = joined

    result = {
        "schema": "camera_band_pixel_join.v1",
        "perf_jsonl": str(args.perf_jsonl),
        "join_definition": "same frame_epoch and voxel-DDA hit chunk coordinate",
        "trace_counts": dict(kind_counts),
        "pixel_probe_frame_count": len(pixel_probe_epochs),
        "frustum_probe_frame_count": len(frustum_probe_epochs),
        "joins": {
            kind: {
                "trace_rows": len(rows),
                "trace_frames_with_pixel_probes": len({
                    row["frame_epoch"] for row in rows
                    if row["pixel_probe_frame_present"]
                }),
                "trace_frames_with_frustum_probes": len({
                    row["frame_epoch"] for row in rows
                    if row["frustum_probe_frame_present"]
                }),
                "matched_trace_rows": sum(
                    row["same_frame_voxel_pixel_hit_count"] > 0 for row in rows
                ),
                "same_frame_pixel_hits": sum(
                    row["same_frame_voxel_pixel_hit_count"] for row in rows
                ),
                "matched_frustum_trace_rows": sum(
                    row["same_frame_frustum_witness_count"] > 0 for row in rows
                ),
                "same_frame_frustum_records": sum(
                    row["same_frame_frustum_witness_count"] for row in rows
                ),
                "rows": rows,
            }
            for kind, rows in joined_by_kind.items()
        },
        "limitations": [
            "A voxel DDA hit does not prove the sampled texel should be opaque; cutout geometry may intentionally reveal background.",
            "A nearer depth surface may occlude the target chunk; compare depth and voxel-hit distances and chunk coordinates.",
            "The pixel probes are sparse and only unmatched sampled rays are inconclusive.",
        ],
    }
    rendered = json.dumps(result, indent=2, sort_keys=True)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
