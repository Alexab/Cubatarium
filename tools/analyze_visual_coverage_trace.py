#!/usr/bin/env python3
"""Summarize opt-in voxel renderer, focus, and screen-ray traces in JSONL."""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path


def add(counter: collections.Counter, value: object) -> None:
    counter[str(value)] += 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    trace_counts: collections.Counter[str] = collections.Counter()
    screen_state: collections.Counter[str] = collections.Counter()
    screen_candidates_by_state: collections.Counter[str] = collections.Counter()
    screen_selected_by_state: collections.Counter[str] = collections.Counter()
    screen_combos: collections.Counter[str] = collections.Counter()
    screen_examples: list[dict[str, object]] = []
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
                    add(screen_candidates_by_state, state)
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
            "candidate_samples": screen_examples,
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
