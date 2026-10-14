#!/usr/bin/env python3
"""Join near-focus visual-hole edges to same-epoch screen rays and pixels."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


SCREEN_ROW_FRACTIONS = {
    0: 0.125,
    1: 0.375,
    2: 0.5625,
    3: 0.625,
    4: 0.875,
}
EVENT_PIXEL_FLAG = 1 << 5


def load_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                # Perf logging may have left one partial final line if a run
                # was interrupted; it cannot be joined to a complete event.
                continue
            if isinstance(row, dict):
                rows.append(row)
    return rows


def unpack_rgba(value: Any) -> dict[str, int] | None:
    try:
        packed = int(value)
    except (TypeError, ValueError):
        return None
    return {
        "r": (packed >> 24) & 0xFF,
        "g": (packed >> 16) & 0xFF,
        "b": (packed >> 8) & 0xFF,
        "a": packed & 0xFF,
    }


def selected_pixel_for_ray(
    ray: dict[str, Any], pixels: list[dict[str, Any]]
) -> dict[str, Any] | None:
    row = int(ray.get("screen_ray_row", -1))
    fraction = SCREEN_ROW_FRACTIONS.get(row)
    if fraction is None:
        return None
    try:
        screen_x = float(ray["screen_ray_x"])
        screen_y = float(ray["screen_ray_y"])
        height = round((screen_y + 0.5) / fraction)
    except (KeyError, TypeError, ValueError, ZeroDivisionError):
        return None
    expected_x = round(screen_x - 0.5)
    expected_gl_y = round(height - 1.0 - screen_y)
    candidates: list[tuple[int, dict[str, Any]]] = []
    for pixel in pixels:
        try:
            dx = abs(int(pixel["renderer_pixel_x"]) - expected_x)
            dy = abs(int(pixel["renderer_pixel_y"]) - expected_gl_y)
        except (KeyError, TypeError, ValueError):
            continue
        if dx <= 1 and dy <= 1:
            candidates.append((dx + dy, pixel))
    if not candidates:
        return None
    _, pixel = min(candidates, key=lambda item: item[0])
    rgba = unpack_rgba(pixel.get("renderer_pixel_rgba"))
    pre_rgba = unpack_rgba(pixel.get("renderer_pixel_pretransparent_rgba"))
    return {
        "pixel_x": pixel.get("renderer_pixel_x"),
        "pixel_y_gl": pixel.get("renderer_pixel_y"),
        "screen_ray_x": screen_x,
        "screen_ray_y_top": screen_y,
        "pixel_delta_x": int(pixel["renderer_pixel_x"]) - expected_x,
        "pixel_delta_y_gl": int(pixel["renderer_pixel_y"]) - expected_gl_y,
        "rgba": rgba,
        "pretransparent_rgba": pre_rgba,
        "pretransparent_depth": pixel.get("renderer_pixel_pretransparent_depth"),
        "near_black_rgb_le_8": bool(
            rgba is not None and rgba["r"] <= 8 and rgba["g"] <= 8 and rgba["b"] <= 8
        ),
        "probe_trigger_flags": pixel.get("renderer_pixel_probe_trigger_flags"),
    }


def analyze(path: Path) -> dict[str, Any]:
    rows = load_rows(path)
    events = [
        row
        for row in rows
        if row.get("kind") == "visual_holes_enter"
        and float(row.get("movement_speed", 0.0) or 0.0) > 2.0
        and row.get("frame_epoch") is not None
    ]
    rays_by_epoch: dict[int, list[dict[str, Any]]] = defaultdict(list)
    pixels_by_epoch: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        epoch = row.get("frame_epoch")
        if epoch is None:
            continue
        epoch = int(epoch)
        if row.get("kind") == "screen_ray_candidate_trace":
            rays_by_epoch[epoch].append(row)
        elif row.get("kind") == "renderer_pixel_probe":
            pixels_by_epoch[epoch].append(row)

    output_events: list[dict[str, Any]] = []
    matched_samples: list[dict[str, Any]] = []
    pixel_event_epochs: set[int] = set()
    exact_selected_ray_events = 0
    selected_ray_count = 0
    event_frame_count = 0
    missing_query_but_screen_ray_candidate = 0
    for event in events:
        epoch = int(event["frame_epoch"])
        coords = (
            event.get("miss_cx"),
            event.get("miss_cy"),
            event.get("miss_cz"),
        )
        ray_rows = rays_by_epoch.get(epoch, [])
        exact_selected = [
            ray
            for ray in ray_rows
            if ray.get("screen_ray_selected") == 1
            and (ray.get("cx"), ray.get("cy"), ray.get("cz")) == coords
        ]
        event_pixels = [
            pixel
            for pixel in pixels_by_epoch.get(epoch, [])
            if int(pixel.get("renderer_pixel_probe_trigger_flags", 0) or 0)
            & EVENT_PIXEL_FLAG
        ]
        if event_pixels:
            event_frame_count += 1
            pixel_event_epochs.add(epoch)
        if exact_selected:
            exact_selected_ray_events += 1
        if event.get("near_focus_hole_probe_result") == 1 and event.get(
            "miss_screen_ray_candidate"
        ) == 1:
            missing_query_but_screen_ray_candidate += 1

        samples = []
        for ray in exact_selected:
            selected_ray_count += 1
            pixel = selected_pixel_for_ray(ray, event_pixels)
            if pixel is None:
                continue
            sample = {
                "frame_epoch": epoch,
                "focus_cx": event.get("focus_cx"),
                "candidate_chunk": list(coords),
                "screen_ray_column": ray.get("screen_ray_column"),
                "screen_ray_row": ray.get("screen_ray_row"),
                "screen_ray_block": [
                    ray.get("screen_ray_block_x"),
                    ray.get("screen_ray_block_y"),
                    ray.get("screen_ray_block_z"),
                ],
                "selection_mesh_satisfying": ray.get(
                    "screen_ray_mesh_satisfying"
                ),
                "selection_repairable_geometry_debt": ray.get(
                    "screen_ray_repairable_geometry_debt"
                ),
                **pixel,
            }
            samples.append(sample)
            matched_samples.append(sample)
        output_events.append(
            {
                "frame_epoch": epoch,
                "focus_cx": event.get("focus_cx"),
                "miss_chunk": list(coords),
                "query_probe_result": event.get("near_focus_hole_probe_result"),
                "query_witness": [
                    event.get("near_focus_hole_probe_witness_cx"),
                    event.get("near_focus_hole_probe_witness_cy"),
                    event.get("near_focus_hole_probe_witness_cz"),
                ]
                if event.get("near_focus_hole_probe_witness_valid") == 1
                else None,
                "screen_ray_trace_rows": len(ray_rows),
                "selected_ray_rows_for_miss_chunk": len(exact_selected),
                "event_pixel_rows": len(event_pixels),
                "matched_pixel_samples": samples,
            }
        )

    return {
        "perf_jsonl": str(path),
        "event_scope": "visual_holes_enter rising edges with movement_speed > 2",
        "pixel_event_flag_bit": 5,
        "counts": {
            "visual_holes_enter_events": len(events),
            "events_with_same_epoch_screen_ray_trace": sum(
                bool(rays_by_epoch.get(int(event["frame_epoch"])))
                for event in events
            ),
            "events_with_selected_ray_for_exact_miss_chunk": exact_selected_ray_events,
            "exact_selected_screen_ray_rows": selected_ray_count,
            "events_with_event_triggered_pixel_capture": event_frame_count,
            "exact_selected_ray_pixel_matches": len(matched_samples),
            "near_black_matched_pixels_rgb_le_8": sum(
                sample["near_black_rgb_le_8"] for sample in matched_samples
            ),
            "query_miss_and_screen_ray_candidate_events": missing_query_but_screen_ray_candidate,
        },
        "pixel_probe_epoch_count": len(pixel_event_epochs),
        "events": output_events,
        "limitations": [
            "Only event-triggered pixel rows (trigger flag bit 5) are joined.",
            "Pixel-to-ray centers are matched within one pixel using the recorded screen row and viewport-height mapping.",
            "A sampled framebuffer color is direct pixel evidence only for that exact sample; it does not cover the full frustum or unsampled frames.",
            "Near-black is reported as a raw RGB threshold and is not by itself proof of missing terrain because fog, sky, and legitimate dark materials can also be dark.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    result = analyze(args.perf_jsonl)
    payload = json.dumps(result, indent=2, ensure_ascii=False)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(payload + "\n", encoding="utf-8")
    else:
        print(payload)
    print(json.dumps(result["counts"], ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
