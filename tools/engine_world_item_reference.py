"""Validate prepared loose-body identity, CPU field and actual primitive issue.

The open plane has direct sky15/block0. Issued primitives do not prove raster
visibility, shader uniforms or final color; those remain separate acceptance.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path


def evaluate(scene: dict, logs: list[Path]) -> dict:
    fixture = scene.get("visible_world_items")
    if scene.get("name") != "open-sky-plane" or not isinstance(fixture, dict):
        return dict(status="UNTESTED", reason="unsupported_world_item_scene")
    try:
        instances, positions = fixture["instances"], fixture["positions"]
        if (fixture.get("schema") != "engine-world-item-field-fixture.v1" or
                fixture.get("item") != "raw_meat" or len(instances) != 2 or
                len(set(instances)) != 2 or len(positions) != 2 or
                any(type(v) is not int or not 0 < v < 2**64 for v in instances) or
                fixture.get("minimum_frame_epochs") != 3):
            raise ValueError("invalid item fixture")
        bounds = scene["plane_bounds"]
        lo, hi = scene["chunk_x_bounds"]
        cells = {}
        for identity, position in zip(instances, positions):
            if (len(position) != 3 or any(type(v) not in (int, float) or
                    not math.isfinite(v) for v in position) or
                    not lo*16+16 <= position[0] < (hi+1)*16-16 or
                    not bounds["z_low"]+16 <= position[2] < bounds["z_high"]-16 or
                    not bounds["floor"] < position[1] < bounds["floor"]+5):
                raise ValueError("unscoped item target")
            cells[identity] = tuple(math.floor(v+.5) for v in position)
    except (ValueError, TypeError, KeyError):
        return dict(status="FAIL", reason="invalid_world_item_fixture")
    marker_epoch = None
    paints, queries = set(), set()
    draws = []
    faults = 0
    examples = []

    def fault(reason):
        nonlocal faults
        faults += 1
        if len(examples) < 8:
            examples.append(reason)

    def cell(values):
        match = re.fullmatch(r"\((-?\d+),(-?\d+),(-?\d+)\)", values["cell"])
        if match is None:
            raise ValueError("malformed cell")
        return tuple(map(int, match.groups()))

    for path in sorted(logs):
        with path.open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if "[FirstWorldFrameWitness]" in line and marker_epoch is None:
                    try:
                        marker = json.loads(line[line.index("{"):])
                        if marker.get("phase") == "first_world_paint":
                            marker_epoch = marker["world_epoch"]
                            if type(marker_epoch) is not int or marker_epoch < 0:
                                raise ValueError("invalid epoch")
                    except (ValueError, TypeError, KeyError, AttributeError):
                        fault("malformed_first_paint")
                        marker_epoch = None
                elif "[WorldItemLightDraw]" in line:
                    draws.append(dict(re.findall(r"\b(\w+)=([^\s]+)", line)))
                elif "[CameraLightSample]" in line or "[SceneLightSample]" in line:
                    values = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                    try:
                        key = int(values["world_epoch"]), int(values["frame_epoch"])
                        if "[CameraLightSample]" in line:
                            paints.add(key)
                        elif values.get("consumer") == "item" and values.get("physical") == "1":
                            queries.add((*key, cell(values), int(values["field_incarnation"]),
                                         int(values["field_revision"]), int(values["state"]),
                                         int(values["ready"]), int(values["sky"]),
                                         int(values["block_light"])))
                    except (ValueError, KeyError, TypeError):
                        fault("malformed_item_query_or_paint")

    seen = set()
    accepted = {identity: set() for identity in instances}
    issued = skipped = unjoined = unsupported = 0
    for values in draws:
        try:
            identity = int(values["instance"])
            if identity not in cells:
                skipped += 1
                continue
            epoch, frame = int(values["world_epoch"]), int(values["frame_epoch"])
            point = cell(values)
            physical = int(values["physical"])
            field, revision, state, ready, sky, block, primitives = (int(values[k]) for k in
                ("field_incarnation", "field_revision", "state", "ready", "sky",
                 "block_light", "drawn_primitives"))
            if (min(epoch, frame, field, revision, primitives) < 0 or
                    max(field, revision, primitives) >= 2**64 or
                    physical not in (0, 1) or ready not in (0, 1) or
                    state not in range(5) or not 0 <= sky <= 15 or not 0 <= block <= 15):
                raise ValueError("negative draw witness")
        except (KeyError, TypeError, ValueError):
            fault("malformed_item_draw")
            continue
        if epoch != marker_epoch:
            skipped += 1
            continue
        key = epoch, frame, identity
        if key in seen:
            fault("duplicate_item_draw")
        seen.add(key)
        if not physical:
            unsupported += 1
        elif values.get("item") != fixture["item"]:
            fault("wrong_item_asset")
        elif any(abs(a-b) > 1 for a, b in zip(point, cells[identity])):
            fault("fixture_item_position_changed")
        elif not ready or state != 2 or field == 0:
            fault("fixture_item_without_current_computed_field")
        elif sky != 15 or block != 0:
            fault("fixture_item_light_mismatch")
        elif primitives == 0:
            fault("fixture_item_without_primitive_issue")
        elif ((epoch, frame) not in paints or
              (epoch, frame, point, field, revision, state, ready, sky, block) not in queries):
            unjoined += 1
        else:
            accepted[identity].add(frame)
            issued += primitives
    epochs = set().union(*accepted.values())
    coverage = dict(first_paint=marker_epoch is not None,
                    declared_instances=all(len(v) >= 2 for v in accepted.values()),
                    frame_epochs=len(epochs) >= 3, joined=unjoined == 0,
                    physical_mode=unsupported == 0)
    return dict(schema="engine-world-item-reference.v1",
                status="FAIL" if faults else "PASS" if all(coverage.values()) else "UNTESTED",
                reason="saved_world_item_field_and_primitive_issue", checked_draws=sum(map(len, accepted.values())),
                frame_epochs=len(epochs), issued_primitives=issued,
                instance_frame_counts={str(k): len(v) for k, v in accepted.items()},
                skipped_other_instance_or_world=skipped, unjoined_draws=unjoined,
                unsupported_mode_draws=unsupported,
                faults=faults, examples=examples,
                missing_coverage=[k for k, valid in coverage.items() if not valid],
                scope="declared saved loose-item identity, canonical CPU field and actual nonempty "
                      "primitive draw issue; no raster visibility, shader RGB/uniform readback, "
                      "transparency, smoothing or performance acceptance")
