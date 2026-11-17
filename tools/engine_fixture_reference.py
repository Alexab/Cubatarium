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
                if fault and len(examples) < 8:
                    examples.append({"frame_epoch": epoch, "origin": origin,
                                     "direction": direction, "expected": expected,
                                     "framebuffer_distance": distance})
    except (ValueError, TypeError, KeyError):
        return {"status": "FAIL", "reason": "malformed_reference_trace"}
    status = "FAIL" if faults else ("PASS" if checked >= 100 and len(epochs) >= 3 else "UNTESTED")
    return {"status": status, "reason": "analytic_near_plane_opaque_depth" if scene.get("name") == "open-sky-plane" else "analytic_near_cave_opaque_depth",
            "checked": checked, "faults": faults, "frame_epochs": len(epochs),
            "skipped_unscoped_or_occluded": skipped, "examples": examples,
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
