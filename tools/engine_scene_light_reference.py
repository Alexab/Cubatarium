"""Join the first-person item's canonical sample to every actual world paint.

The independent open-plane reference is sky15/block0. This observes the CPU
sample supplied to the viewmodel, not its final shader color or other objects.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

from engine_camera_light_reference import evaluate as evaluate_camera


def evaluate(scene: dict, logs: list[Path], *, expected_frames: int | None = None) -> dict:
    camera = evaluate_camera(scene, logs, expected_frames=expected_frames)
    if scene.get("name") != "open-sky-plane":
        return dict(status="UNTESTED", reason="unsupported_scene_light_scene")
    if camera.get("reason") == "invalid_camera_light_fixture":
        return dict(status="FAIL", reason="invalid_scene_light_fixture")
    epoch = None
    paints = set()
    observations = []
    faults = 0
    examples = []
    air_sections = set()
    ready = set()
    unsupported = skipped = 0

    def fault(reason):
        nonlocal faults
        faults += 1
        if len(examples) < 8:
            examples.append(reason)

    for path in sorted(logs):
        with path.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if "[FirstWorldFrameWitness]" in line and epoch is None:
                    try:
                        marker = json.loads(line[line.index("{"):])
                        if marker.get("phase") == "first_world_paint":
                            epoch = marker["world_epoch"]
                    except (ValueError, KeyError, TypeError, AttributeError):
                        fault("malformed_first_paint")
                elif "[CameraLightSample]" in line:
                    values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                    try:
                        paints.add((int(values["world_epoch"]), int(values["frame_epoch"])))
                    except (ValueError, KeyError):
                        fault("malformed_paint_frame")
                elif "[SceneLightSample]" in line:
                    values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                    if values.get("consumer") == "fp_item":
                        observations.append(values)

    paints = {key for key in paints if key[0] == epoch}
    seen = set()
    for values in observations:
        try:
            key = int(values["world_epoch"]), int(values["frame_epoch"])
            def triple(name):
                match = re.fullmatch(r"\((-?\d+),(-?\d+),(-?\d+)\)", values[name])
                if match is None:
                    raise ValueError("coordinate")
                return tuple(map(int, match.groups()))
            cell, coord = triple("cell"), triple("coord")
            physical, resident, available = (int(values[k]) for k in ("physical", "resident", "ready"))
            state, sky, block, identity, revision = (int(values[k]) for k in
                ("state", "sky", "block_light", "field_incarnation", "field_revision"))
            if (min(key) < 0 or any(v not in (0, 1) for v in (physical, resident, available)) or
                    state not in range(5) or not 0 <= sky <= 15 or not 0 <= block <= 15 or
                    min(identity, revision) < 0 or coord != tuple(v // 16 for v in cell)):
                raise ValueError("sample")
        except (KeyError, ValueError, TypeError):
            fault("malformed_fp_item_sample")
            continue
        if key[0] != epoch:
            skipped += 1
            continue
        bounds = scene["plane_bounds"]
        lo, hi = scene["chunk_x_bounds"]
        if not (lo*16+16 <= cell[0] < (hi+1)*16-16 and
                bounds["z_low"]+16 <= cell[2] < bounds["z_high"]-16 and
                cell[1] > round(bounds["floor"]-.5)):
            skipped += 1
            continue
        if key not in paints:
            fault("fp_item_without_world_paint")
        if key in seen:
            fault("duplicate_fp_item_frame")
        seen.add(key)
        if not physical:
            unsupported += 1
        elif not available or state != 2 or identity == 0:
            fault("fp_item_without_current_computed_field")
        elif sky != 15 or block != 0:
            fault("fp_item_light_mismatch")
        else:
            ready.add(key)
            if not resident:
                air_sections.add(coord)

    coverage = dict(camera_paints=camera["status"] == "PASS", every_paint=bool(paints) and
                    seen == ready == paints, physical_mode=unsupported == 0,
                    no_unscoped_samples=skipped == 0,
                    absent_geometry_sections=len(air_sections) >= 8)
    return dict(schema="engine-scene-light-reference.v1",
                status="FAIL" if faults else "PASS" if all(coverage.values()) else "UNTESTED",
                reason="first_person_item_canonical_field_at_paint", ready_frames=len(ready),
                paint_frames=len(paints), distinct_air_sections=len(air_sections),
                skipped_unscoped=skipped, unsupported_mode_samples=unsupported,
                faults=faults, examples=examples,
                missing_coverage=[k for k, valid in coverage.items() if not valid],
                scope="CPU field supplied to first-person item; no creature/world-item, "
                      "shader RGB, GPU page, smoothing, latency or full-screen acceptance")
