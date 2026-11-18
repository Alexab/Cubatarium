"""Independent analytic opaque-depth checks for the prepared cave fixtures.

The reference uses only the scene dimensions, camera ray and framebuffer depth.
It never uses the engine's voxel-ray hit, mesh-ready flags or pixel brightness.
Checks are explicitly limited to nearby cave walls, floor and ceiling; they do
not grant general visual/light acceptance.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def near_cave_surface(scene: dict, origin: list[float], direction: list[float]) -> float | None:
    xmin = scene["chunk_x_bounds"][0] * 16 - 0.5
    xmax = (scene["chunk_x_bounds"][1] + 1) * 16 - 0.5
    if scene.get("name") == "open-sky-plane":
        bounds = scene["plane_bounds"]
        if direction[1] >= -1e-8 or origin[1] <= bounds["floor"]:
            return None
        t = (bounds["floor"] - origin[1]) / direction[1]
        x, z = origin[0] + direction[0]*t, origin[2] + direction[2]*t
        return t if (0.05 <= t <= 32 and xmin < x < xmax and
                     bounds["z_low"] < z < bounds["z_high"]) else None
    bounds = scene.get("cave_bounds", {"floor": -43.5, "ceiling": -30.5,
                                       "z_low": 44.5, "z_high": 57.5})
    if not (xmin < origin[0] < xmax and bounds["floor"] < origin[1] < bounds["ceiling"] and
            bounds["z_low"] < origin[2] < bounds["z_high"]):
        return None
    candidates = []
    for axis, low, high in ((1, bounds["floor"], bounds["ceiling"]),
                            (2, bounds["z_low"], bounds["z_high"])):
        component = direction[axis]
        if abs(component) > 1e-8:
            t = ((high if component > 0 else low) - origin[axis]) / component
            x = origin[0] + direction[0] * t
            if 0.05 <= t <= 32 and xmin < x < xmax:
                candidates.append(t)
    return min(candidates) if candidates else None


def expected_surface_section(scene: dict, origin: list[float],
                             direction: list[float], distance: float) -> tuple[int, int, int]:
    """Attribute a failed analytic hit to its solid-side voxel, not to the ray origin."""
    hit = [a + b * distance for a, b in zip(origin, direction)]
    cell = [math.floor(value + .5) for value in hit]
    if scene.get("name") == "open-sky-plane":
        planes = [(1, scene["plane_bounds"]["floor"], -1)]
    else:
        bounds = scene.get("cave_bounds", dict(floor=-43.5, ceiling=-30.5,
                                               z_low=44.5, z_high=57.5))
        planes = [(1, bounds["floor"], -1), (1, bounds["ceiling"], 1),
                  (2, bounds["z_low"], -1), (2, bounds["z_high"], 1)]
    for axis, boundary, sign in planes:
        if abs(hit[axis] - boundary) < .0001:
            cell[axis] = math.floor(hit[axis] + .5 + sign * .0001)
    return tuple(value // 16 for value in cell)


def note_fault_section(sections: dict, coord: tuple[int, int, int], row: dict,
                       expected: float, tolerance: float) -> None:
    """Diagnostic metadata never participates in the independent depth verdict."""
    section = sections.setdefault(coord, dict(coord=list(coord), faults=0,
        frame_epochs=set(), voxel_matches_reference=0, no_published_geometry=0,
        queued_without_published_geometry=0, reported_settled_revision=0,
        maximum_dirty_age=0, work_owner_flag_counts={}))
    section["faults"] += 1
    epoch = row.get("frame_epoch")
    if isinstance(epoch, int) and not isinstance(epoch, bool):
        section["frame_epochs"].add(epoch)
    prefix = "renderer_pixel_voxel_chunk_"
    voxel_coord = tuple(row.get(prefix + c) for c in "xyz")
    voxel_distance = row.get("renderer_pixel_voxel_hit_distance")
    if (not all(isinstance(v, int) and not isinstance(v, bool) for v in voxel_coord) or
            voxel_coord != coord or
            not isinstance(voxel_distance, (int, float)) or isinstance(voxel_distance, bool) or
            not math.isfinite(voxel_distance) or abs(voxel_distance - expected) > tolerance):
        return
    section["voxel_matches_reference"] += 1
    def integer(name):
        value = row.get(prefix + name)
        return value if isinstance(value, int) and not isinstance(value, bool) else None
    missing = integer("published_geom_rev") == 0
    section["no_published_geometry"] += missing
    section["queued_without_published_geometry"] += (
        missing and integer("dirty_queue_kind") in (1, 2, 3))
    settled, field = integer("settled_light_rev"), integer("field_light_rev")
    section["reported_settled_revision"] += (
        integer("has_settled_light") == 1 and settled is not None and settled == field)
    age = integer("dirty_queue_age_frames")
    if age is not None:
        section["maximum_dirty_age"] = max(section["maximum_dirty_age"], age)
    owner = integer("work_owner_flags")
    if owner is not None:
        key = str(owner)
        section["work_owner_flag_counts"][key] = section["work_owner_flag_counts"].get(key, 0) + 1


def evaluate(scene: dict, perf: Path | None) -> dict:
    if (scene.get("schema") != "engine-smoke-fixture.v1" or
            scene.get("name") not in ("dry-negative-cave", "wet-negative-cave", "dry-wide-negative-cave", "open-sky-plane")):
        return {"status": "UNTESTED", "reason": "unsupported_reference_scene"}
    if perf is None or not perf.is_file():
        return {"status": "UNTESTED", "reason": "missing_framebuffer_trace"}
    checked = 0
    faults = 0
    skipped = 0
    epochs = set()
    examples = []
    fault_sections = {}
    try:
        with perf.open(encoding="utf-8") as stream:
            for line in stream:
                if not line.strip():
                    continue
                row = json.loads(line)
                if not isinstance(row, dict):
                    raise ValueError("trace rows must be objects")
                if row.get("kind") != "renderer_pixel_probe":
                    continue
                origin = [row.get("renderer_pixel_ray_origin_" + c) for c in "xyz"]
                direction = [row.get("renderer_pixel_ray_direction_" + c) for c in "xyz"]
                distance = row.get("renderer_pixel_opaque_hit_distance")
                values = origin + direction + [distance]
                if not all(isinstance(x, (int, float)) and not isinstance(x, bool) and
                           math.isfinite(x) for x in values):
                    skipped += 1
                    continue
                if abs(sum(v * v for v in direction) - 1) > 0.01:
                    skipped += 1
                    continue
                expected = near_cave_surface(scene, origin, direction)
                if expected is None:
                    skipped += 1
                    continue
                tolerance = 0.05 + expected * 0.001
                # A nearer opaque surface may be a creature or other legitimate
                # occluder. It cannot prove the wall behind it is present.
                if 0 <= distance < expected - tolerance:
                    skipped += 1
                    continue
                checked += 1
                epoch = row.get("frame_epoch")
                if isinstance(epoch, int) and not isinstance(epoch, bool):
                    epochs.add(epoch)
                fault = distance < 0 or distance > expected + tolerance
                faults += fault
                if fault:
                    note_fault_section(fault_sections,
                        expected_surface_section(scene, origin, direction, expected),
                        row, expected, tolerance)
                if fault and len(examples) < 8:
                    examples.append({"frame_epoch": epoch, "origin": origin,
                                     "direction": direction, "expected": expected,
                                     "framebuffer_distance": distance})
    except (ValueError, TypeError, KeyError):
        return {"status": "FAIL", "reason": "malformed_reference_trace"}
    status = "FAIL" if faults else ("PASS" if checked >= 100 and len(epochs) >= 3 else "UNTESTED")
    sections = []
    for section in sorted(fault_sections.values(), key=lambda s: (-s["faults"], s["coord"])):
        frames = section.pop("frame_epochs")
        section.update(frame_epochs=len(frames), first_frame_epoch=min(frames) if frames else None,
                       last_frame_epoch=max(frames) if frames else None)
        sections.append(section)
    return {"status": status, "reason": "analytic_near_plane_opaque_depth" if scene.get("name") == "open-sky-plane" else "analytic_near_cave_opaque_depth",
            "checked": checked, "faults": faults, "frame_epochs": len(epochs),
            "skipped_unscoped_or_occluded": skipped, "examples": examples,
            "fault_attribution": {"schema": "engine-depth-fault-attribution.v1",
                "section_count": len(sections), "sections": sections[:32],
                "scope": "analytic solid-side sections with optional same-hit engine metadata; reported settlement is not a current physical proof; metadata never changes the depth verdict"},
            "scope": ("opaque floor" if scene.get("name") == "open-sky-plane" else "opaque cave surfaces") +
                     " within 32 blocks; no light, transparent or full-screen acceptance"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--perf", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    verdict = evaluate(json.loads(args.fixture.read_text(encoding="utf-8")), args.perf)
    args.out.write_text(json.dumps(verdict, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict))
    return {"PASS": 0, "FAIL": 1, "UNTESTED": 2}[verdict["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
