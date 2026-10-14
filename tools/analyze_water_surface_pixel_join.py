#!/usr/bin/env python3
"""Join framebuffer sea-plane probes to persisted water blocks and source fluid meshes.

The renderer's ``renderer_pixel_fluid_triangle_match`` is a CPU/source-geometry
witness. It does not prove that a transparent GPU command was submitted or that
the fragment changed the framebuffer; compare the recorded pre/post RGB too.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter
from pathlib import Path
from typing import Any


def unpack_rgb(rgba: int) -> int:
    return (rgba >> 8) & 0xFFFFFF


def row_float(row: dict[str, Any], key: str, default: float = -1.0) -> float:
    value = row.get(key)
    return default if value is None else float(value)


def row_int_optional(row: dict[str, Any], key: str) -> int | None:
    value = row.get(key)
    return None if value is None else int(value)


def summarize(perf_path: Path, world_dir: Path, water_id: int,
              min_camera_x: float, max_camera_x: float) -> dict[str, Any]:
    # worldgen_metrics_lib is kept in tools/ beside this script.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from worldgen_metrics_lib import decode_chunk

    chunk_dir = world_dir / "chunks"
    decoded: dict[tuple[int, int, int], dict[tuple[int, int, int], int] | None] = {}
    captures: dict[tuple[int, int], dict[str, Any]] = {}
    samples: list[dict[str, Any]] = []
    skipped: Counter[str] = Counter()

    with perf_path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("{"):
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                skipped["malformed_json"] += 1
                continue
            if row.get("kind") != "renderer_pixel_probe":
                continue

            try:
                camera_x = float(row["camera_x"])
                surface_valid = int(row.get("renderer_pixel_surface_valid", 0))
            except (KeyError, TypeError, ValueError):
                skipped["missing_camera_or_surface_state"] += 1
                continue
            if not surface_valid or not min_camera_x <= camera_x <= max_camera_x:
                continue

            try:
                point = tuple(float(row[f"renderer_pixel_surface_{axis}"])
                              for axis in "xyz")
                probe_id = int(row.get("renderer_pixel_probe_id", -1))
                frame_epoch = int(row.get("frame_epoch", -1))
                rgba = int(row.get("renderer_pixel_rgba", 0))
                before_rgba = int(row.get("renderer_pixel_pretransparent_rgba", 0))
            except (KeyError, TypeError, ValueError):
                skipped["invalid_pixel_fields"] += 1
                continue

            block = tuple(math.floor(v) for v in point)
            chunk = tuple(math.floor(v / 16) for v in block)
            if chunk not in decoded:
                file_path = chunk_dir / ("%d_%d_%d.cchunk" % chunk)
                decoded[chunk] = decode_chunk(file_path) if file_path.is_file() else None
            chunk_blocks = decoded[chunk]
            block_id = chunk_blocks.get(block, 0) if chunk_blocks is not None else None
            source_triangle = bool(int(row.get("renderer_pixel_fluid_triangle_match", 0)))
            rgb_changed = unpack_rgb(rgba) != unpack_rgb(before_rgba)
            depth = float(row.get("renderer_pixel_pretransparent_depth", 1.0))
            fluid_state = {
                "surface_chunk": [
                    row.get("renderer_pixel_fluid_chunk_x"),
                    row.get("renderer_pixel_fluid_chunk_y"),
                    row.get("renderer_pixel_fluid_chunk_z"),
                ],
                "source_block_id": int(
                    row.get("renderer_pixel_fluid_block_id", -1)
                    if row.get("renderer_pixel_fluid_block_id") is not None
                    else -1
                ),
                "mdi_probe_checked": row_int_optional(
                    row, "renderer_pixel_fluid_mdi_probe_checked"
                ),
                "payload_check_attempted": row_int_optional(
                    row, "renderer_pixel_fluid_payload_check_attempted"
                ),
                "source_vertex_count": row_int_optional(
                    row, "renderer_pixel_fluid_source_vertex_count"
                ),
                "source_index_count": row_int_optional(
                    row, "renderer_pixel_fluid_source_index_count"
                ),
                "gpu_vertex_count": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_vertex_count"
                ),
                "gpu_index_count": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_index_count"
                ),
                "gpu_index_count_gl": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_index_count_gl"
                ),
                "vertex_count_matches": row_int_optional(
                    row, "renderer_pixel_fluid_vertex_count_matches"
                ),
                "index_count_matches": row_int_optional(
                    row, "renderer_pixel_fluid_index_count_matches"
                ),
                "vertex_payload_matches": row_int_optional(
                    row, "renderer_pixel_fluid_vertex_payload_matches"
                ),
                "index_payload_matches": row_int_optional(
                    row, "renderer_pixel_fluid_index_payload_matches"
                ),
                "vertex_payload_readback_valid": row_int_optional(
                    row, "renderer_pixel_fluid_vertex_payload_readback_valid"
                ),
                "index_payload_readback_valid": row_int_optional(
                    row, "renderer_pixel_fluid_index_payload_readback_valid"
                ),
                "source_batch_index": row_int_optional(
                    row, "renderer_pixel_fluid_source_batch_index"
                ),
                "gpu_batch_index": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_batch_index"
                ),
                "gpu_batch_vector_index": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_batch_vector_index"
                ),
                "gpu_compact_active": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_compact_active"
                ),
                "gpu_vbo_byte_offset": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_vbo_byte_offset"
                ),
                "gpu_ebo_byte_offset": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_ebo_byte_offset"
                ),
                "gpu_vbo_buffer_bytes": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_vbo_buffer_bytes"
                ),
                "gpu_ebo_buffer_bytes": row_int_optional(
                    row, "renderer_pixel_fluid_gpu_ebo_buffer_bytes"
                ),
                "vertex_payload_first_mismatch_byte": row_int_optional(
                    row,
                    "renderer_pixel_fluid_vertex_payload_first_mismatch_byte",
                ),
                "index_payload_first_mismatch": row_int_optional(
                    row, "renderer_pixel_fluid_index_payload_first_mismatch"
                ),
                "index_payload_source_value": row_int_optional(
                    row, "renderer_pixel_fluid_index_payload_source_value"
                ),
                "index_payload_gpu_value": row_int_optional(
                    row, "renderer_pixel_fluid_index_payload_gpu_value"
                ),
                "mdi_command_instance_count": row_int_optional(
                    row, "renderer_mdi_command_instance_count"
                ),
                "mdi_command_first_index": row_int_optional(
                    row, "renderer_mdi_command_first_index"
                ),
                "mdi_command_base_vertex": row_int_optional(
                    row, "renderer_mdi_command_base_vertex"
                ),
                "marker_mode": row.get("renderer_pixel_marker_mode"),
                "marker_visible": row.get("renderer_pixel_marker_visible"),
                "marker_coverage": row_float(
                    row, "renderer_pixel_marker_coverage", default=-1.0
                ),
                "chunk_render_ready": int(
                    row.get("renderer_pixel_fluid_chunk_render_ready", 0) or 0
                ),
                "raw_ref_count": int(
                    row.get("renderer_pixel_fluid_raw_ref_count", 0) or 0
                ),
                "ready_ref_count": int(
                    row.get("renderer_pixel_fluid_ready_ref_count", 0) or 0
                ),
                "ready_triangle_match": int(
                    row.get("renderer_pixel_ready_fluid_triangle_match", 0) or 0
                ),
                "gate_flags": int(
                    row.get("renderer_pixel_fluid_chunk_gate_flags", 0) or 0
                ),
                "gpu_resident": int(
                    row.get("renderer_pixel_fluid_chunk_gpu_resident", 0) or 0
                ),
                "gpu_slot_quad_count": int(
                    row.get("renderer_pixel_fluid_chunk_gpu_slot_quad_count", 0) or 0
                ),
                "mdi_resident_pass_flags": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_resident_pass_flags", 0) or 0
                ),
                "mdi_visible_pass_flags": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_visible_pass_flags", 0) or 0
                ),
                "mdi_command_count": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_command_count", 0) or 0
                ),
                "mdi_visible_command_count": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_visible_command_count", 0) or 0
                ),
                "mdi_first_block_id": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_first_block_id", 0) or 0
                ),
                "mdi_index_count": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_index_count", 0) or 0
                ),
                "mdi_visible_index_count": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_visible_index_count", 0) or 0
                ),
                "source_top_face_quads": int(
                    row.get("renderer_pixel_fluid_chunk_source_top_face_quads", 0) or 0
                ),
                "texture_ready": int(
                    row.get("renderer_pixel_fluid_chunk_texture_ready", 0) or 0
                ),
                "mdi_payload_flags": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_payload_flags", 0) or 0
                ),
                "mdi_command_flags": int(
                    row.get("renderer_pixel_fluid_chunk_mdi_command_flags", 0) or 0
                ),
            }
            transparent_bit = 1 << 2
            state_checks = {
                "chunk_render_ready": bool(fluid_state["chunk_render_ready"]),
                "raw_ref_present": fluid_state["raw_ref_count"] > 0,
                "ready_ref_present": fluid_state["ready_ref_count"] > 0,
                "ready_triangle_match": bool(fluid_state["ready_triangle_match"]),
                "transparent_mdi_resident": bool(
                    fluid_state["mdi_resident_pass_flags"] & transparent_bit
                ),
                "transparent_mdi_visible": bool(
                    fluid_state["mdi_visible_pass_flags"] & transparent_bit
                ),
                "transparent_mdi_command": fluid_state["mdi_command_count"] > 0,
                "transparent_mdi_visible_command":
                    fluid_state["mdi_visible_command_count"] > 0,
                "transparent_mdi_indexed": fluid_state["mdi_index_count"] > 0,
                "transparent_mdi_visible_indexed":
                    fluid_state["mdi_visible_index_count"] > 0,
                "source_top_face_present": fluid_state["source_top_face_quads"] > 0,
                "texture_ready": bool(fluid_state["texture_ready"]),
                "mdi_probe_checked": (
                    None if fluid_state["mdi_probe_checked"] is None
                    else bool(fluid_state["mdi_probe_checked"])
                ),
                "payload_check_attempted": (
                    None if fluid_state["payload_check_attempted"] is None
                    else bool(fluid_state["payload_check_attempted"])
                ),
                "vertex_count_matches": (
                    None if fluid_state["vertex_count_matches"] is None
                    else bool(fluid_state["vertex_count_matches"])
                ),
                "index_count_matches": (
                    None if fluid_state["index_count_matches"] is None
                    else bool(fluid_state["index_count_matches"])
                ),
                "vertex_payload_matches": (
                    None if fluid_state["vertex_payload_matches"] is None
                    else bool(fluid_state["vertex_payload_matches"])
                ),
                "index_payload_matches": (
                    None if fluid_state["index_payload_matches"] is None
                    else bool(fluid_state["index_payload_matches"])
                ),
                "vertex_payload_readback_valid": (
                    None if fluid_state["vertex_payload_readback_valid"] is None
                    else bool(fluid_state["vertex_payload_readback_valid"])
                ),
                "index_payload_readback_valid": (
                    None if fluid_state["index_payload_readback_valid"] is None
                    else bool(fluid_state["index_payload_readback_valid"])
                ),
                "gpu_compact_active": (
                    None if fluid_state["gpu_compact_active"] is None
                    else bool(fluid_state["gpu_compact_active"])
                ),
                "transparent_mdi_command_readback_checked": bool(
                    fluid_state["mdi_command_flags"] & (1 << 0)
                ),
                "transparent_mdi_command_matches_batch": bool(
                    fluid_state["mdi_command_flags"] & (1 << 1)
                ),
                "transparent_mdi_gpu_command_instance_visible": bool(
                    fluid_state["mdi_command_flags"] & (1 << 2)
                ),
                "transparent_fragment_marker_mode": (
                    None if fluid_state["marker_mode"] is None
                    else bool(fluid_state["marker_mode"])
                ),
                "transparent_fragment_marker_visible": (
                    None if fluid_state["marker_visible"] is None
                    else bool(fluid_state["marker_visible"])
                ),
            }

            key = (probe_id, frame_epoch)
            capture = captures.setdefault(key, {
                "probe_id": probe_id,
                "frame_epoch": frame_epoch,
                "camera": [row.get("camera_x"), row.get("camera_y"), row.get("camera_z")],
                "samples": 0,
                "block_file_present": 0,
                "water_blocks": 0,
                "source_fluid_triangles": 0,
                "water_with_source_triangle": 0,
                "water_without_source_triangle": 0,
                "triangle_without_water_block": 0,
                "rgb_changed_after_transparent": 0,
                "water_triangle_rgb_changed": 0,
                "opaque_depth_written": 0,
                "transparent_fragment_marker_mode_samples": 0,
                "transparent_fragment_marker_visible_samples": 0,
                "gpu_state_counts": {name: 0 for name in state_checks},
                "water_gpu_state_counts": {name: 0 for name in state_checks},
                "water_source_triangle_gpu_state_counts": {
                    name: 0 for name in state_checks
                },
            })
            capture["samples"] += 1
            capture["block_file_present"] += chunk_blocks is not None
            is_water = block_id == water_id
            capture["water_blocks"] += is_water
            capture["source_fluid_triangles"] += source_triangle
            capture["water_with_source_triangle"] += is_water and source_triangle
            capture["water_without_source_triangle"] += is_water and not source_triangle
            capture["triangle_without_water_block"] += source_triangle and block_id is not None and not is_water
            capture["rgb_changed_after_transparent"] += rgb_changed
            capture["water_triangle_rgb_changed"] += is_water and source_triangle and rgb_changed
            capture["opaque_depth_written"] += depth < 0.99999
            capture["transparent_fragment_marker_mode_samples"] += (
                fluid_state["marker_mode"] is not None
                and bool(fluid_state["marker_mode"])
            )
            capture["transparent_fragment_marker_visible_samples"] += (
                fluid_state["marker_visible"] is not None
                and bool(fluid_state["marker_visible"])
            )
            for name, present in state_checks.items():
                if present is None:
                    continue
                capture["gpu_state_counts"][name] += present
                if is_water:
                    capture["water_gpu_state_counts"][name] += present
                    if source_triangle:
                        capture["water_source_triangle_gpu_state_counts"][name] += present

            samples.append({
                "probe_id": probe_id,
                "frame_epoch": frame_epoch,
                "camera_x": camera_x,
                "pixel": [row.get("renderer_pixel_x"), row.get("renderer_pixel_y")],
                "surface_world": list(point),
                "block_world": list(block),
                "chunk": list(chunk),
                "block_file_present": chunk_blocks is not None,
                "block_id": block_id,
                "is_water": is_water if block_id is not None else None,
                "source_fluid_triangle_match": source_triangle,
                "rgb_changed_after_transparent": rgb_changed,
                "rgba_before": before_rgba,
                "rgba_after": rgba,
                "pretransparent_depth": depth,
                "fluid_triangle_distance": row_float(
                    row, "renderer_pixel_fluid_triangle_distance"
                ),
                "opaque_hit_distance": row_float(
                    row, "renderer_pixel_opaque_hit_distance"
                ),
                "camera_to_surface_distance": math.dist(
                    (float(row.get("camera_x", 0.0)),
                     float(row.get("camera_y", 0.0)),
                     float(row.get("camera_z", 0.0))),
                    point,
                ),
                "fluid_surface_renderer_state": fluid_state,
                "fluid_surface_renderer_state_checks": state_checks,
                "transparent_fragment_marker_mode": fluid_state["marker_mode"],
                "transparent_fragment_marker_visible": fluid_state[
                    "marker_visible"
                ],
                "transparent_fragment_marker_coverage": fluid_state[
                    "marker_coverage"
                ],
            })

    if not samples:
        raise ValueError(f"no valid sea-plane pixel probes found in {perf_path}")

    totals: Counter[str] = Counter()
    for capture in captures.values():
        for field in (
            "samples", "block_file_present", "water_blocks", "source_fluid_triangles",
            "water_with_source_triangle", "water_without_source_triangle",
            "triangle_without_water_block", "rgb_changed_after_transparent",
            "water_triangle_rgb_changed", "opaque_depth_written",
            "transparent_fragment_marker_mode_samples",
            "transparent_fragment_marker_visible_samples",
        ):
            totals[field] += int(capture[field])

    state_names = next(iter(captures.values()))["gpu_state_counts"]

    def aggregate_state_counts(key: str) -> dict[str, int]:
        return {
            name: sum(capture[key][name] for capture in captures.values())
            for name in state_names
        }

    return {
        "schema": "water_surface_pixel_join.v3",
        "perf_jsonl": str(perf_path),
        "world_dir": str(world_dir),
        "water_block_id": water_id,
        "camera_x_window": [min_camera_x, max_camera_x],
        "capture_count": len(captures),
        "totals": dict(totals),
        "captures": [captures[k] for k in sorted(captures)],
        "gpu_state_totals": aggregate_state_counts("gpu_state_counts"),
        "water_gpu_state_totals": aggregate_state_counts("water_gpu_state_counts"),
        "water_source_triangle_gpu_state_totals": aggregate_state_counts(
            "water_source_triangle_gpu_state_counts"
        ),
        "mdi_probe_check_known_sample_count": sum(
            sample["fluid_surface_renderer_state"]["mdi_probe_checked"] is not None
            for sample in samples
        ),
        "samples": samples,
        "decoded_chunks": {
            "%d,%d,%d" % key: None if value is None else len(value)
            for key, value in decoded.items()
        },
        "skipped": dict(skipped),
        "interpretation_limits": [
            "A missing .cchunk file is unknown, not proof that the runtime column lacked data.",
            "A source fluid triangle match is not proof of a ready/visible GPU draw command.",
            "Transparent MDI pass flags use bit 2; no matching MDI batch may also mean the sample lies outside the submitted/cull range, so compare camera-to-surface distance and fluid draw telemetry before calling it a missing upload.",
            "The CPU drawInstanceCount proxy in mdi_visible_pass_flags is not the post-cull GPU command. Use renderer_mdi_command_flags bits 0-2 for checked/readback-matched GPU command state.",
            "Payload comparison is only meaningful when source and GPU batch indexes match, the readback-valid bit is set, and the byte/index offsets lie within the backing buffer size.",
            "RGB change compares the sampled framebuffer before and after the transparent pass; sparse probes do not cover the full frame.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("world_dir", type=Path)
    parser.add_argument("--water-id", type=int, default=575)
    parser.add_argument("--min-camera-x", type=float, default=-math.inf)
    parser.add_argument("--max-camera-x", type=float, default=math.inf)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    report = summarize(args.perf_jsonl, args.world_dir, args.water_id,
                       args.min_camera_x, args.max_camera_x)
    encoded = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(encoded, encoding="utf-8")
    print(json.dumps({
        "capture_count": report["capture_count"],
        "totals": report["totals"],
        "json_out": str(args.json_out) if args.json_out else None,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
