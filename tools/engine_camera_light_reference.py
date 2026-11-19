"""Validate canonical camera-field observations at actual world paints.

The open plane has unobstructed sky and no emitter within the scoped footprint.
The roofed AIR-border scene has opaque floor emitters one cell below a whole
absent geometry section; its direct unobstructed transport is15-Manhattan.
Missing/transient fields after first paint fail even when their raw bytes look
bright. This is a CPU-resource readiness/value oracle, not a shader/RGB oracle.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path


def evaluate(scene: dict, logs: list[Path], *, expected_frames: int | None = None) -> dict:
    positive_air = scene.get("air_border_fixture",{}).get("schema") == "engine-positive-air-border-fixture.v1"
    if scene.get("name") != "open-sky-plane" and not positive_air:
        return dict(status="UNTESTED", reason="unsupported_camera_light_scene")
    try:
        bounds = scene["cave_bounds"] if positive_air else scene["plane_bounds"]
        lo, hi = scene["chunk_x_bounds"]
        if (any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v)
                for v in (bounds["floor"], bounds["z_low"], bounds["z_high"])) or
                not all(isinstance(v, int) and not isinstance(v, bool) for v in (lo, hi)) or
                lo >= hi or bounds["z_high"] - bounds["z_low"] <= 32):
            raise ValueError("invalid reference domain")
        if positive_air:
            light=scene["lighting_fixture"]
            floor_y=round(bounds["floor"]-.5)
            border=scene["air_border_fixture"]
            if (scene.get("expected_eye_in_fluid") is not False or floor_y!=-49 or
                    border.get("source_section_y")!=-4 or border.get("target_section_y")!=-3 or
                    bounds["z_low"]>47.5 or bounds["z_high"]<63.5 or bounds["ceiling"]<-32.5 or
                    light.get("schema")!="engine-positive-blocklight-fixture.v1" or light.get("emission")!=15 or
                    light.get("material_id")!=376 or not light.get("emitters") or
                    any(not isinstance(e,list) or len(e)!=3 or any(type(v) is not int for v in e) or e[1]!=floor_y
                        for e in light["emitters"])):
                raise ValueError("invalid AIR/source boundary contract")
    except (KeyError, TypeError, ValueError):
        return dict(status="FAIL", reason="invalid_camera_light_fixture")
    marker_epoch = None
    first_sample = False
    pending = None
    samples = ready_samples = air_samples = unsupported = skipped = faults = 0
    epochs = set()
    air_sections = set()
    seen = set()
    examples = []
    levels=set()

    def fault(reason):
        nonlocal faults
        faults += 1
        if len(examples) < 8:
            examples.append(reason)

    def observe(line):
        nonlocal samples, ready_samples, air_samples, unsupported, skipped
        values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
        try:
            epoch, frame = int(values["world_epoch"]), int(values["frame_epoch"])
            def triple(key):
                value = re.fullmatch(r"\((-?\d+),(-?\d+),(-?\d+)\)", values[key])
                if value is None:
                    raise ValueError("invalid coordinate")
                return tuple(map(int, value.groups()))
            cell, coord = triple("cell"), triple("coord")
            physical, resident, ready = (int(values[k]) for k in ("physical", "resident", "ready"))
            sky, block, identity = (int(values[k]) for k in ("sky", "block_light", "field_incarnation"))
            if (epoch < 0 or frame < 0 or any(v not in (0, 1) for v in (physical, resident, ready)) or
                    not 0 <= sky <= 15 or not 0 <= block <= 15 or identity < 0 or
                    coord != tuple(v // 16 for v in cell)):
                raise ValueError("invalid field observation")
        except (KeyError, TypeError, ValueError):
            fault("malformed_camera_light_observation")
            return
        if epoch != marker_epoch:
            skipped += 1
            return
        bounds = scene["cave_bounds"] if positive_air else scene["plane_bounds"]
        lo, hi = scene["chunk_x_bounds"]
        z_margin=0 if positive_air else 16
        if not (lo*16+16 <= cell[0] < (hi+1)*16-16 and
                bounds["z_low"]+z_margin <= cell[2] < bounds["z_high"]-z_margin and
                cell[1] > round(bounds["floor"]-.5)):
            skipped += 1
            return
        key = epoch, frame, cell
        if key in seen:
            return
        seen.add(key)
        if not physical:
            unsupported += 1
            return
        samples += 1
        epochs.add(frame)
        expected_sky=0 if positive_air else 15
        expected_block=max(0,max(15-sum(abs(a-b) for a,b in zip(cell,e))
                              for e in scene["lighting_fixture"]["emitters"])) if positive_air else 0
        levels.add(expected_block)
        if not ready:
            fault(f"unready_camera_field:{coord}:{frame}")
        elif identity == 0:
            fault(f"invalid_ready_field_binding:{coord}:{frame}")
        elif positive_air and (resident or coord[1]!=-3 or coord[2]!=3):
            fault(f"air_target_has_geometry_or_wrong_band:{coord}:{frame}")
        elif sky != expected_sky or block != expected_block:
            fault(f"camera_light_mismatch:{coord}:{frame}:sky{sky}:block{block}")
        else:
            ready_samples += 1
            if not resident:
                air_samples += 1
                air_sections.add(coord)
            return True

    for path in sorted(logs):
        with path.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if "[CameraLightSample]" in line:
                    if marker_epoch is None:
                        pending = line
                    else:
                        observe(line)
                elif "[FirstWorldFrameWitness]" in line and marker_epoch is None:
                    try:
                        marker = json.loads(line[line.index("{"):])
                        if not isinstance(marker, dict):
                            raise ValueError("invalid first-paint witness")
                        if marker.get("phase") != "first_world_paint":
                            continue
                        epoch = marker["world_epoch"]
                        if not isinstance(epoch, int) or isinstance(epoch, bool) or epoch < 0:
                            raise ValueError("invalid first-paint epoch")
                        marker_epoch = epoch
                        # The camera observation precedes this frame's post-paint
                        # witness. Keep that first actual world frame in scope.
                        if pending:
                            first_sample = bool(observe(pending))
                    except (ValueError, KeyError, TypeError):
                        fault("malformed_first_world_paint")
    coverage = dict(first_paint=marker_epoch is not None, first_camera_sample=first_sample,
                    samples=ready_samples >= 25,
                    frame_epochs=len(epochs) >= 3, absent_geometry_sections=len(air_sections) >= 8,
                    physical_mode=unsupported == 0,
                    **({"positive_levels":len(levels)>=3 and min(levels)>0} if positive_air else {}),
                    paint_frame_coverage=(isinstance(expected_frames, int) and
                        not isinstance(expected_frames, bool) and expected_frames > 0 and
                        len(seen) == len(epochs) == expected_frames and skipped == 0))
    status = "FAIL" if faults else "PASS" if all(coverage.values()) else "UNTESTED"
    return dict(schema="engine-camera-light-reference.v1", status=status,
                reason="camera_canonical_field_at_paint", samples=samples,
                ready_samples=ready_samples, air_samples=air_samples,
                frame_epochs=len(epochs), distinct_air_sections=len(air_sections),
                expected_game_frames=expected_frames, observed_paint_frames=len(seen),
                unsupported_mode_samples=unsupported, skipped_unscoped=skipped,
                faults=faults, examples=examples,
                missing_coverage=[k for k, valid in coverage.items() if not valid],
                expected_block_levels=sorted(levels),
                scope=("roofed AIR section, sky0 and independent emitter Manhattan blocklight at every paint; " if positive_air else
                       "observed canonical camera CPU field readiness/sky15/block0 at world paints; ")+
                      "no creature/item, GPU page, shader RGB, latency or full-screen acceptance")
