"""Check creature CPU light requests in the authored open vertical columns.

Sky is independently 15 above the floor. Near a lateral fixture boundary an
external emitter may contribute blocklight, so only its range is constrained.
This observes sample consumers, not creature identity, GPU issue or RGB.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path


def evaluate(scene: dict, logs: list[Path], *, expected_frames: int | None = None) -> dict:
    if scene.get("name") != "open-sky-plane":
        return dict(status="UNTESTED", reason="unsupported_creature_light_scene")
    try:
        bounds = scene["plane_bounds"]
        lo, hi = scene["chunk_x_bounds"]
        floor, zlo, zhi = (bounds[k] for k in ("floor", "z_low", "z_high"))
        top = scene.get("expected_max_eye_y", 19)
        if (any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
                for v in (floor, zlo, zhi, top)) or
                not all(isinstance(v, int) and not isinstance(v, bool) for v in (lo, hi)) or
                lo >= hi or zhi-zlo <= 32 or not floor < top <= 63):
            raise ValueError("domain")
    except (KeyError, TypeError, ValueError):
        return dict(status="FAIL", reason="invalid_creature_light_fixture")
    epoch = None
    paints = set()
    observations = []
    faults = unsupported = skipped = samples = ready = unconstrained = 0
    examples = []
    frames, sections = set(), set()

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
                            if not isinstance(epoch, int) or isinstance(epoch, bool) or epoch < 0:
                                raise ValueError("epoch")
                    except (ValueError, TypeError, KeyError, AttributeError):
                        epoch = None
                        fault("malformed_first_paint")
                elif "[CameraLightSample]" in line:
                    values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                    try:
                        paints.add((int(values["world_epoch"]), int(values["frame_epoch"])))
                    except (ValueError, KeyError):
                        fault("malformed_world_paint")
                elif "[SceneLightSample]" in line:
                    values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                    if values.get("consumer") == "creature":
                        observations.append(values)
    paints = {key for key in paints if key[0] == epoch}
    unjoined = 0
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
            fault("malformed_creature_sample")
            continue
        if key[0] != epoch or not (lo*16 <= cell[0] < (hi+1)*16 and
                                  zlo < cell[2] < zhi and floor < cell[1] <= top):
            skipped += 1
            continue
        samples += 1
        frames.add(key)
        sections.add(coord)
        if key not in paints:
            unjoined += 1
        if not physical:
            unsupported += 1
        elif not available or state != 2 or identity == 0:
            fault("creature_without_current_computed_field")
        elif sky != 15:
            fault("creature_direct_sky_mismatch")
        else:
            interior = lo*16+16 <= cell[0] < (hi+1)*16-16 and zlo+16 <= cell[2] < zhi-16
            if interior and block != 0:
                fault("creature_interior_blocklight_mismatch")
            else:
                ready += 1
                unconstrained += not interior
    coverage = dict(first_world_paint=epoch is not None,
                    paint_frames=bool(paints) and (expected_frames is None or len(paints) == expected_frames),
                    creature_requests=samples >= 3 and len(frames) >= 3,
                    multiple_sections=len(sections) >= 2, joined_world_paints=unjoined == 0,
                    physical_mode=unsupported == 0)
    return dict(schema="engine-creature-light-reference.v1",
                status="FAIL" if faults else "PASS" if all(coverage.values()) else "UNTESTED",
                reason="creature_canonical_field_in_clear_columns",
                samples=samples, ready_samples=ready, frame_epochs=len(frames),
                distinct_sections=len(sections), boundary_block_unconstrained_samples=unconstrained,
                unjoined_samples=unjoined, unsupported_mode_samples=unsupported,
                skipped_unscoped=skipped, faults=faults, examples=examples,
                missing_coverage=[k for k, valid in coverage.items() if not valid],
                scope="creature CPU sample readiness/direct sky15 in authored columns; block0 only in padded interior; "
                      "no actor identity, GPU primitive issue, shader RGB, smoothing, latency or full-screen acceptance")
