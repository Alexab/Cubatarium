"""Independent floor blocklight and unobstructed direct-skylight references.

Joins analytic floor/depth to published CPU vertices or actual packed GPU draw-buffer lighting.
Does not prove shaded RGB, transparency or full-screen output.
"""
from __future__ import annotations
import json
import math
from pathlib import Path
from engine_fixture_reference import near_cave_surface
from engine_bound_field_evidence import read_bound_field


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
    direct_sky = (scene.get("name") == "open-sky-plane" and
                  fixture.get("schema") == "engine-direct-skylight-fixture.v1")
    if not direct_sky and (scene.get("name") != "dry-negative-cave" or
            fixture.get("schema") != "engine-positive-blocklight-fixture.v1"):
        return {"status": "UNTESTED", "reason": "missing_positive_floor_fixture"}
    try:
        bounds = scene["plane_bounds"] if direct_sky else scene["cave_bounds"]
        floor_y = round(bounds["floor"] - 0.5)
        emitters = [] if direct_sky else fixture["emitters"]
        if direct_sky:
            if fixture.get("sky_level") != 15 or fixture.get("material_id") != 560:
                raise ValueError("unsupported direct-sky reference")
        elif (fixture["emission"] != 15 or fixture["material_id"] != 376 or
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
    missing_bound = bound_baked_zero = 0
    source_counts = {"cpu": 0, "packed_gpu": 0}
    skip_reasons = {}
    def skip(reason):
        nonlocal skipped
        skipped += 1
        skip_reasons[reason] = skip_reasons.get(reason, 0) + 1
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
                    skip("not_downward_valid_ray"); continue
                expected_depth = near_cave_surface(scene, origin, direction)
                floor_depth = (bounds["floor"] - origin[1]) / direction[1]
                if (expected_depth is None or abs(floor_depth - expected_depth) > 0.001 or
                        abs(distance - expected_depth) > 0.05 + expected_depth * 0.001):
                    skip("not_joined_floor_depth"); continue
                px = origin[0] + direction[0] * expected_depth
                pz = origin[2] + direction[2] * expected_depth
                x, z = math.floor(px + 0.5), math.floor(pz + 0.5)
                # Exclude voxel borders and unmodeled influence beyond scene ends.
                lo, hi = scene["chunk_x_bounds"]
                if (abs(px-x) > 0.45 or abs(pz-z) > 0.45 or
                        x < lo*16+16 or x >= (hi+1)*16-16 or
                        (direct_sky and not bounds["z_low"] + 16 <= z < bounds["z_high"] - 16)):
                    skip("unscoped_floor_cell"); continue
                if (row.get(prefix + "surface_valid") != 1 or
                        row.get(prefix + "vertex_light_valid") != 1 or
                        row.get(prefix + "vertex_light_face_index") != 4 or
                        row.get(prefix + "drawable") != 1 or row.get(prefix + "draw_ready") != 1 or
                        not finite(row.get(prefix + "vertex_light_distance")) or
                        row[prefix + "vertex_light_distance"] > 0.03 or
                        row.get(prefix + "vertex_light_preview") != 0 or
                        row.get(prefix + "preview_shader_override") != 0):
                    skip("missing_or_provisional_source_match"); continue
                source_kind = row.get(prefix + "light_source_kind", 1)
                cpu_source = (source_kind == 1 and finite(row.get(prefix + "source_index_count")) and
                              row[prefix + "source_index_count"] > 0)
                gpu_source = (source_kind == 2 and row.get(prefix + "packed_light_payload_valid") == 1 and
                              finite(row.get(prefix + "packed_light_quad_count")) and
                              row[prefix + "packed_light_quad_count"] > 0)
                if not cpu_source and not gpu_source:
                    skip("missing_readable_draw_source"); continue
                bound = read_bound_field(row)
                wants_bound = bool(scene.get("rgb_fixture")) or (
                    isinstance(row.get(prefix+"bound_field"),dict) and
                    row[prefix+"bound_field"].get("requested") is True)
                if wants_bound and bound is None:
                    missing_bound += 1
                    skip("missing_draw_time_bound_field"); continue
                if bound is not None:
                    actual = (bound["packed"] >> 4)/15
                    sky = (bound["packed"] & 15)/15
                    source_counts["bound_gpu"] = source_counts.get("bound_gpu",0)+1
                    bound_baked_zero += row.get(prefix+"vertex_block_light")==0 and actual>0
                else:
                    actual = row.get(prefix + "vertex_block_light")
                    sky = row.get(prefix + "vertex_sky_light")
                epoch = row.get("frame_epoch")
                if (not finite(actual) or not finite(sky) or not isinstance(epoch, int) or
                        isinstance(epoch, bool)):
                    skip("missing_finite_light_or_epoch"); continue
                expected = 0 if direct_sky else expected_floor_light(scene, x, z)
                expected_sky = 15 if direct_sky else 0
                is_emitter = (x, floor_y, z) in emitter_cells
                expected_id = fixture["material_id"] if is_emitter else 560
                fault = (abs(actual - expected/15) > 0.001 or abs(sky - expected_sky/15) > 0.001 or
                         row.get(prefix + "vertex_light_block_id") != expected_id)
                checked += 1; faults += fault; positive += max(expected, expected_sky) > 0
                source_counts["cpu" if cpu_source else "packed_gpu"] += 1
                epochs.add(epoch)
                observed_floor_cells.add((x, z))
                expected_levels.add(expected_sky if direct_sky else expected)
                if is_emitter and not fault: observed_emitters.add((x, floor_y, z))
                if fault and len(examples) < 8:
                    examples.append({"frame_epoch": epoch, "floor_cell": [x,floor_y,z],
                                     "expected_block_level": expected, "source_block_light": actual,
                                     "expected_sky_level": expected_sky,
                                     "source_sky_light": sky, "source_material_id": row.get(prefix + "vertex_light_block_id")})
    except (ValueError, TypeError, KeyError, AttributeError):
        return {"status": "FAIL", "reason": "malformed_floor_reference_trace"}
    coverage = {"samples": checked >= 25, "positive_samples": positive >= 25,
                "frame_epochs": len(epochs) >= 3, "floor_cells": len(observed_floor_cells) >= 8,
                "requested_bound_fields": missing_bound==0,
                **({} if direct_sky else {"light_levels": len(expected_levels) >= 3,
                                         "emitter_surface": bool(observed_emitters)})}
    status = "FAIL" if faults else "PASS" if all(coverage.values()) else "UNTESTED"
    return {"status": status, "reason": "analytic_direct_sky_floor_draw_source_light" if direct_sky else "analytic_floor_draw_source_light",
            "checked": checked, "positive_expected": positive, "faults": faults,
            "source_counts": source_counts,
            "missing_bound_fields": missing_bound, "positive_bound_with_baked_zero": bound_baked_zero,
            "frame_epochs": len(epochs), "observed_emitter_cells": len(observed_emitters),
            "distinct_floor_cells": len(observed_floor_cells),
            "distinct_expected_levels": len(expected_levels),
            "skipped": skipped, "skip_reasons": skip_reasons,
            "missing_coverage": [k for k,v in coverage.items() if not v], "examples": examples,
            "scope": ("unobstructed constant direct-skylight floor" if direct_sky else "prepared dry-cave opaque floor") +
                     " CPU/packed baked light or explicit same-draw GPU-bound field readback joined to analytic framebuffer depth; requested bound proof never falls back to baked data; no shaded RGB, transparency or full-screen acceptance"}
