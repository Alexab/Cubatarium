"""Independent prepared-cave floor blocklight reference.

Joins analytic floor/depth to CPU published draw-source vertex lighting.
Does not prove GPU bound light, shading/RGB, transparency or full-screen output.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
from engine_fixture_reference import near_cave_surface


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def expected_floor_light(scene, x, z):
    """Shortest unobstructed path exits an opaque floor emitter upward first."""
    fixture = scene["lighting_fixture"]
    floor_y = round(scene["cave_bounds"]["floor"] - 0.5)
    return max(0, max(fixture["emission"] - abs(x - e[0]) - abs(z - e[2]) - 1
                      for e in fixture["emitters"] if e[1] == floor_y))


def evaluate(scene: dict, perf: Path | None) -> dict:
    fixture = scene.get("lighting_fixture", {})
    if (scene.get("name") != "dry-negative-cave" or
            fixture.get("schema") != "engine-positive-blocklight-fixture.v1"):
        return {"status": "UNTESTED", "reason": "missing_positive_floor_fixture"}
    try:
        floor_y = round(scene["cave_bounds"]["floor"] - 0.5)
        emitters = fixture["emitters"]
        if (fixture["emission"] != 15 or fixture["material_id"] != 376 or
                not emitters or any(len(e) != 3 or
                    any(not isinstance(v, int) or isinstance(v, bool) for v in e) or
                    e[1] != floor_y for e in emitters)):
            raise ValueError("unsupported emitter geometry/material")
        emitter_cells = {tuple(e) for e in emitters}
    except (KeyError, TypeError, ValueError):
        return {"status": "FAIL", "reason": "invalid_positive_floor_fixture"}
    if perf is None or not perf.is_file():
        return {"status": "UNTESTED", "reason": "missing_floor_draw_source_trace"}
    checked = faults = skipped = positive = 0
    epochs = set()
    observed_emitters = set()
    observed_floor_cells = set()
    expected_levels = set()
    examples = []
    prefix = "renderer_pixel_opaque_"
    try:
        with perf.open(encoding="utf-8") as stream:
            for line in stream:
                if not line.strip(): continue
                row = json.loads(line)
                if row.get("kind") != "renderer_pixel_probe": continue
                origin = [row.get("renderer_pixel_ray_origin_" + c) for c in "xyz"]
                direction = [row.get("renderer_pixel_ray_direction_" + c) for c in "xyz"]
                distance = row.get(prefix + "hit_distance")
                if (not all(finite(v) for v in origin + direction + [distance]) or
                        abs(sum(v*v for v in direction) - 1) > 0.01 or direction[1] >= -1e-6):
                    skipped += 1; continue
                expected_depth = near_cave_surface(scene, origin, direction)
                floor_depth = (scene["cave_bounds"]["floor"] - origin[1]) / direction[1]
                if (expected_depth is None or abs(floor_depth - expected_depth) > 0.001 or
                        abs(distance - expected_depth) > 0.05 + expected_depth * 0.001):
                    skipped += 1; continue
                px = origin[0] + direction[0] * expected_depth
                pz = origin[2] + direction[2] * expected_depth
                x, z = math.floor(px + 0.5), math.floor(pz + 0.5)
                # Exclude voxel borders and unmodeled influence beyond scene ends.
                lo, hi = scene["chunk_x_bounds"]
                if (abs(px-x) > 0.45 or abs(pz-z) > 0.45 or
                        x < lo*16+16 or x >= (hi+1)*16-16):
                    skipped += 1; continue
                if (row.get(prefix + "surface_valid") != 1 or
                        row.get(prefix + "vertex_light_valid") != 1 or
                        row.get(prefix + "vertex_light_face_index") != 4 or
                        row.get(prefix + "drawable") != 1 or row.get(prefix + "draw_ready") != 1 or
                        not finite(row.get(prefix + "source_index_count")) or
                        row[prefix + "source_index_count"] <= 0 or
                        not finite(row.get(prefix + "vertex_light_distance")) or
                        row[prefix + "vertex_light_distance"] > 0.03 or
                        row.get(prefix + "vertex_light_preview") != 0 or
                        row.get(prefix + "preview_shader_override") != 0):
                    skipped += 1; continue
                actual = row.get(prefix + "vertex_block_light")
                sky = row.get(prefix + "vertex_sky_light")
                epoch = row.get("frame_epoch")
                if (not finite(actual) or not finite(sky) or not isinstance(epoch, int) or
                        isinstance(epoch, bool)):
                    skipped += 1; continue
                expected = expected_floor_light(scene, x, z)
                is_emitter = (x, floor_y, z) in emitter_cells
                expected_id = fixture["material_id"] if is_emitter else 560
                fault = (abs(actual - expected/15) > 0.001 or abs(sky) > 0.001 or
                         row.get(prefix + "vertex_light_block_id") != expected_id)
                checked += 1; faults += fault; positive += expected > 0
                epochs.add(epoch)
                observed_floor_cells.add((x, z))
                expected_levels.add(expected)
                if is_emitter and not fault: observed_emitters.add((x, floor_y, z))
                if fault and len(examples) < 8:
                    examples.append({"frame_epoch": epoch, "floor_cell": [x,floor_y,z],
                                     "expected_block_level": expected, "source_block_light": actual,
                                     "source_sky_light": sky, "source_material_id": row.get(prefix + "vertex_light_block_id")})
    except (ValueError, TypeError, KeyError, AttributeError):
        return {"status": "FAIL", "reason": "malformed_floor_reference_trace"}
    status = "FAIL" if faults else ("PASS" if checked >= 25 and positive >= 25 and
             len(epochs) >= 3 and len(observed_floor_cells) >= 8 and
             len(expected_levels) >= 3 and observed_emitters else "UNTESTED")
    return {"status": status, "reason": "analytic_floor_cpu_draw_source_light",
            "checked": checked, "positive_expected": positive, "faults": faults,
            "frame_epochs": len(epochs), "observed_emitter_cells": len(observed_emitters),
            "distinct_floor_cells": len(observed_floor_cells),
            "distinct_expected_levels": len(expected_levels),
            "skipped": skipped, "examples": examples,
            "scope": "prepared dry-cave opaque floor CPU draw-source light joined to analytic framebuffer depth; no GPU-bound-light, RGB, transparency or full-screen acceptance"}
