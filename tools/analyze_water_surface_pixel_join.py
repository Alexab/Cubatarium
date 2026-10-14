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
        ):
            totals[field] += int(capture[field])

    return {
        "schema": "water_surface_pixel_join.v1",
        "perf_jsonl": str(perf_path),
        "world_dir": str(world_dir),
        "water_block_id": water_id,
        "camera_x_window": [min_camera_x, max_camera_x],
        "capture_count": len(captures),
        "totals": dict(totals),
        "captures": [captures[k] for k in sorted(captures)],
        "samples": samples,
        "decoded_chunks": {
            "%d,%d,%d" % key: None if value is None else len(value)
            for key, value in decoded.items()
        },
        "skipped": dict(skipped),
        "interpretation_limits": [
            "A missing .cchunk file is unknown, not proof that the runtime column lacked data.",
            "A source fluid triangle match is not proof of a ready/visible GPU draw command.",
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
