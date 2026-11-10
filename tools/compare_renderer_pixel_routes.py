#!/usr/bin/env python3
"""Compare renderer pixel witnesses by camera-X bins on matched flight corridors."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


def luminance(packed_rgba: int) -> float:
    red = (packed_rgba >> 24) & 0xFF
    green = (packed_rgba >> 16) & 0xFF
    blue = (packed_rgba >> 8) & 0xFF
    return 0.2126 * red + 0.7152 * green + 0.0722 * blue


def summarize(path: Path, min_x: float, max_x: float, bin_width: int) -> dict[str, Any]:
    bins: dict[int, Counter[str]] = {}
    blocks_by_bin: dict[int, Counter[str]] = {}
    ignored_probe_rows = 0

    with path.open("r", encoding="utf-8") as source:
        for line in source:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if row.get("kind") != "renderer_pixel_probe":
                continue
            # A zero probe id is the trace's empty/sentinel record, not a pixel.
            if int(row.get("renderer_pixel_probe_id", 0)) == 0:
                ignored_probe_rows += 1
                continue

            camera_x = row.get("camera_x")
            if camera_x is None or not min_x <= float(camera_x) < max_x:
                continue
            bin_index = int((float(camera_x) - min_x) // bin_width)
            counters = bins.setdefault(bin_index, Counter())
            luma = luminance(int(row.get("renderer_pixel_rgba", 0)))
            counters["probes"] += 1
            for threshold in (32, 64, 96):
                if luma < threshold:
                    counters[f"luma_lt_{threshold}"] += 1

            depth_valid = int(row.get("renderer_pixel_opaque_surface_valid", 0)) != 0
            counters["opaque_depth_valid"] += int(depth_valid)
            mesh_revision = int(row.get("renderer_pixel_opaque_mesh_revision", 0))
            published_revision = int(
                row.get("renderer_pixel_opaque_published_geom_rev", 0)
            )
            counters["geometry_newer_than_published"] += int(
                depth_valid and mesh_revision > published_revision
            )
            counters["opaque_demand_unsettled"] += int(
                depth_valid
                and int(row.get("renderer_pixel_opaque_demand_present", 0)) != 0
                and int(row.get("renderer_pixel_opaque_demand_has_settled_light", 0)) == 0
            )

            if luma < 96:
                block_id = int(row.get("renderer_pixel_opaque_vertex_light_block_id", -1))
                blocks_by_bin.setdefault(bin_index, Counter())[str(block_id)] += 1

    rendered_bins = []
    for bin_index in sorted(bins):
        counters = bins[bin_index]
        count = counters["probes"]
        blocks = blocks_by_bin.get(bin_index, Counter())
        rendered_bins.append(
            {
                "camera_x": [
                    min_x + bin_index * bin_width,
                    min(max_x, min_x + (bin_index + 1) * bin_width),
                ],
                "probes": count,
                "dark_luma_counts": {
                    str(threshold): counters[f"luma_lt_{threshold}"]
                    for threshold in (32, 64, 96)
                },
                "dark_luma_rates": {
                    str(threshold): round(counters[f"luma_lt_{threshold}"] / count, 6)
                    for threshold in (32, 64, 96)
                },
                "opaque_depth_valid": counters["opaque_depth_valid"],
                "geometry_newer_than_published": counters[
                    "geometry_newer_than_published"
                ],
                "opaque_demand_unsettled": counters["opaque_demand_unsettled"],
                "dark_luma_lt_96_top_source_face_block_ids": [
                    {"block_id": int(block_id), "count": n}
                    for block_id, n in blocks.most_common(8)
                    if block_id.lstrip("-").isdigit()
                ],
            }
        )

    total = Counter()
    for counters in bins.values():
        total.update(counters)
    count = total["probes"]
    return {
        "perf_jsonl": str(path),
        "matched_camera_x": [min_x, max_x],
        "bin_width_blocks": bin_width,
        "ignored_zero_id_probe_rows": ignored_probe_rows,
        "matched_probe_count": count,
        "matched_dark_luma_counts": {
            str(threshold): total[f"luma_lt_{threshold}"]
            for threshold in (32, 64, 96)
        },
        "matched_dark_luma_rates": {
            str(threshold): round(total[f"luma_lt_{threshold}"] / count, 6)
            if count
            else None
            for threshold in (32, 64, 96)
        },
        "matched_opaque_depth_valid": total["opaque_depth_valid"],
        "matched_geometry_newer_than_published": total[
            "geometry_newer_than_published"
        ],
        "matched_opaque_demand_unsettled": total["opaque_demand_unsettled"],
        "bins": rendered_bins,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--label", action="append", required=True, help="run label")
    parser.add_argument("--perf-jsonl", action="append", required=True, type=Path)
    parser.add_argument("--min-x", type=float, default=-8192.0)
    parser.add_argument("--max-x", type=float, default=-1024.0)
    parser.add_argument("--bin-width", type=int, default=512)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    if len(args.label) != len(args.perf_jsonl):
        parser.error("provide one --label for every --perf-jsonl")
    if args.bin_width <= 0 or args.max_x <= args.min_x:
        parser.error("require positive --bin-width and --max-x > --min-x")

    report = {
        "matched_camera_x": [args.min_x, args.max_x],
        "bin_width_blocks": args.bin_width,
        "runs": {
            label: summarize(path, args.min_x, args.max_x, args.bin_width)
            for label, path in zip(args.label, args.perf_jsonl)
        },
        "interpretation_note": (
            "Low luminance includes dark materials, lighting and fog; it is not an empty-chunk "
            "counter. Valid opaque depth indicates a rendered surface. A CPU mesh revision "
            "newer than its published geometry marks an update/publication lag witness."
        ),
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
