"""Frame-bound canonical AIR light oracle for one declared floor-source edit.

Expected light comes from declared geometry/emission and the verified action,
never observed light. This does not certify GPU/shader or publication latency.
"""
from __future__ import annotations
from collections import Counter
import json
from pathlib import Path
import re


def integer(value):
    return type(value) is int


def evaluate_records(scene, lines, action, plan, material, *, expected_frames,
                     initial_extra_emitters=()):
    scope = ("Declared roofed AIR, sky0 and15-Manhattan canonical CPU values before/after "
             "one verified source edit; no GPU/shader, lifetime or publication-latency acceptance")
    def result(status, reason, **data):
        return dict(schema="engine-source-camera-reference.v1", status=status,
                    reason=reason, scope=scope, **data)
    if not isinstance(material, dict):
        return result("FAIL", "invalid_material_evidence")
    if material.get("status") != "PASS":
        return result("UNTESTED", "missing_verified_material_action")
    try:
        bounds = scene["cave_bounds"]
        lo, hi = scene["chunk_x_bounds"]
        fixture = scene["lighting_fixture"]
        if (scene.get("air_border_fixture", {}).get("schema") != "engine-positive-air-border-fixture.v1" or
                scene.get("expected_eye_in_fluid") is not False or bounds["floor"] != -48.5 or
                bounds["ceiling"] < -32.5 or bounds["z_low"] > 47.5 or bounds["z_high"] < 63.5 or
                not integer(lo) or not integer(hi) or lo >= hi or
                fixture.get("schema") != "engine-positive-blocklight-fixture.v1" or
                fixture.get("material_id") != 376 or fixture.get("emission") != 15):
            raise ValueError("unsupported declared geometry")
        def position(value):
            if (not isinstance(value, (list, tuple)) or len(value) != 3 or
                    any(not integer(v) or not -(1 << 31) <= v < (1 << 31) for v in value)):
                raise ValueError("invalid coordinate")
            return tuple(value)
        before = [position(e) for e in fixture["emitters"]] + [position(e) for e in initial_extra_emitters]
        if not before or len(before) != len(set(before)) or any(e[1:] != (-49, 51) for e in before):
            raise ValueError("invalid source declaration")
        before = set(before)
        if plan.get("schema") != "engine-native-world-actions.v1" or len(plan["entries"]) != 1:
            raise ValueError("one action required")
        entry = plan["entries"][0]
        cell = position(entry["cell"])
        ids = {"air": 0, "stone": 560, "glowstone": 376}
        expected, target = ids[entry["expected"]], ids[entry.get("replacement", entry["expected"])]
        mutating = "replacement" in entry
        if (cell[1:] != (-49, 51) or not lo*16 <= cell[0] < (hi+1)*16 or
                (cell in before) != (expected == 376) or
                (mutating and (expected == target or 376 not in (expected, target)))):
            raise ValueError("action does not match source model")
        if (action.get("schema") != "engine-world-actions-observation.v1" or
                action.get("api") != "UWorld::ReplaceBlockAt" or action.get("started") is not True or
                action.get("failed") is not False or action.get("storage_matches_isolated_world") is not True):
            raise ValueError("invalid native action")
        rows = action["observations"]
        initial, final = ([r for r in rows if r.get("phase") == phase and r.get("cell") == entry["cell"]]
                          for phase in ("initial", "final"))
        if len(rows) != 2 or len(initial) != 1 or len(final) != 1:
            raise ValueError("ambiguous action")
        initial, final = initial[0], final[0]
        if any(not integer(row.get(key)) for row in (initial, final)
               for key in ("expected_id", "target_id", "before_id", "after_id")):
            raise ValueError("untyped material identity")
        pivot = initial.get("frame_epoch")
        if not integer(pivot) or pivot < 1:
            return result("UNTESTED", "missing_action_frame_identity")
        if not integer(expected_frames) or not 1 <= expected_frames <= 262144 or pivot >= expected_frames:
            return result("UNTESTED", "unsupported_paint_frame_count")
        if (initial.get("success") is not True or final.get("success") is not True or
                initial.get("before_id") != expected or initial.get("after_id") != target or
                any(row["expected_id"] != expected or row["target_id"] != target for row in (initial, final)) or
                final.get("before_id") != target or final.get("after_id") != target or
                initial.get("api_called") is not mutating or initial.get("api_success") is not mutating or
                final.get("api_called") is not False or final.get("api_success") is not False or
                not integer(final.get("frame_epoch")) or final["frame_epoch"] <= pivot):
            raise ValueError("native action mismatch")
        evidence = material["cells"]
        if (material.get("schema") != "engine-world-actions-evidence.v1" or
                len(evidence) != 1 or evidence[0]["cell"] != entry["cell"] or
                evidence[0]["before_id"] != expected or evidence[0]["after_id"] != target):
            raise ValueError("material evidence from a different action")
        after = before - {cell}
        if target == 376:
            after.add(cell)
    except (KeyError, ValueError, TypeError, AttributeError):
        return result("FAIL", "invalid_source_camera_contract")

    counts = Counter()
    frames, values = set(), [dict(), dict()]
    epoch, pending, examples, last_frame = None, [], [], 0
    model_levels = {}
    def observe(line):
        nonlocal last_frame
        pairs = re.findall(r"\b(\w+)=([^\s]+)", line)
        f = dict(pairs)
        if len(f) != len(pairs):
            raise ValueError("duplicate observation key")
        e, frame = int(f["world_epoch"]), int(f["frame_epoch"])
        if not 0 < e < 1 << 64:
            raise ValueError("invalid world epoch")
        if e != epoch:
            counts["foreign_world_samples"] += 1
            return
        if frame <= last_frame or not 1 <= frame <= expected_frames:
            raise ValueError("duplicate or out-of-range paint")
        last_frame = frame
        frames.add(frame)
        def triple(key):
            m = re.fullmatch(r"\((-?\d+),(-?\d+),(-?\d+)\)", f[key])
            if m is None:
                raise ValueError("invalid observed coordinate")
            return position(tuple(map(int, m.groups())))
        sample, coord = triple("cell"), triple("coord")
        physical, resident, ready = (int(f[k]) for k in ("physical", "resident", "ready"))
        sky, block, incarnation = (int(f[k]) for k in ("sky", "block_light", "field_incarnation"))
        if (any(v not in (0, 1) for v in (physical, resident, ready)) or
                not 0 <= sky <= 15 or not 0 <= block <= 15 or not 0 <= incarnation < 1 << 64 or
                coord != tuple(v//16 for v in sample)):
            raise ValueError("malformed field sample")
        if mutating and frame == pivot:
            counts["unordered_action_frame"] += 1
            return
        phase = int(not mutating or frame > pivot)
        name = "after" if phase else "before"
        counts[name+"_samples"] += 1
        if physical != 1 or resident != 0 or coord[1:] != (-3, 3) or not lo*16+16 <= sample[0] < (hi+1)*16-16:
            raise ValueError("unsupported canonical AIR consumer")
        if not ready:
            counts[name+"_pending"] += 1
            return
        if sample not in model_levels:
            model_levels[sample] = tuple(max((max(0, 15-sum(abs(a-b) for a,b in zip(sample, source)))
                                             for source in sources), default=0)
                                         for sources in (before, after))
        expected = model_levels[sample][phase]
        counts[name+"_ready"] += 1
        if sky != 0 or block != expected or incarnation == 0:
            counts["faults"] += 1
            if len(examples) < 8:
                examples.append(dict(frame=frame, cell=sample, expected=expected, actual=block, sky=sky))
        else:
            values[phase].setdefault(sample, set()).add(block)
            if model_levels[sample][0] != model_levels[sample][1]:
                counts[name+"_source_effect_samples"] += 1
    try:
        for line in lines:
            if "[CameraLightSample]" in line:
                if epoch is None:
                    if len(pending) >= 256:
                        return result("UNTESTED", "missing_first_paint_marker")
                    pending.append(line)
                else:
                    observe(line)
            elif "[FirstWorldFrameWitness]" in line:
                marker = json.loads(line[line.index("{"):])
                if epoch is not None or marker.get("phase") != "first_world_paint" or marker.get("ready") is not True or not integer(marker.get("world_epoch")) or marker["world_epoch"] <= 0:
                    raise ValueError("invalid first-paint marker")
                epoch = marker["world_epoch"]
                for sample in pending:
                    observe(sample)
                pending.clear()
    except (KeyError, ValueError, TypeError, AttributeError):
        return result("FAIL", "malformed_source_camera_trace")
    transitions = [dict(cell=cell, before=sorted(values[0][cell]), after=sorted(values[1][cell]))
                   for cell in sorted(values[0].keys() & values[1].keys()) if values[0][cell] != values[1][cell]]
    coverage = dict(first_paint=epoch is not None, all_paints=len(frames) == expected_frames,
                    single_world_stream=counts["foreign_world_samples"] == 0,
                    no_pending_fields=counts["before_pending"]+counts["after_pending"] == 0,
                    after_samples=counts["after_ready"] >= 8,
                    before_samples=not mutating or counts["before_ready"] >= 8,
                    source_contribution_before=not mutating or counts["before_source_effect_samples"] >= 8,
                    source_contribution_after=not mutating or counts["after_source_effect_samples"] >= 8)
    return result("FAIL" if counts["faults"] else "PASS" if all(coverage.values()) else "UNTESTED",
                  "frame_bound_declared_source_transport", action_frame=pivot, counts=dict(counts),
                  faults=counts["faults"], expected_paints=expected_frames, world_epoch=epoch,
                  observed_paints=len(frames), transitions=transitions, examples=examples,
                  missing_coverage=[k for k,v in coverage.items() if not v])


def evaluate(scene, artifacts: Path, *, initial_extra_emitters=()):
    """Recompute actual material/return proof; cached PASS is not sufficient."""
    from engine_world_actions import evaluate as material_evaluate
    from engine_round_trip_evidence import evaluate as return_evaluate
    try:
        read = lambda name: json.loads((artifacts/name).read_text(encoding="utf-8"))
        report, route, action = read("flight_sim_report.json"), read("bot-route.json"), read("world-actions.json")
        plan = route["world_actions"]
        mutating = any("replacement" in e for e in plan["entries"])
        returned = return_evaluate(artifacts, report, route) if mutating else None
        material = material_evaluate(artifacts, Path(action["world_folder"]), plan, returned)
        def lines():
            for path in sorted((artifacts/"logs").glob("*.log.INFO.*")):
                with path.open(encoding="utf-8", errors="replace") as stream:
                    yield from stream
        return evaluate_records(scene, lines(), action, plan, material,
                                expected_frames=report["ingame_frames"], initial_extra_emitters=initial_extra_emitters)
    except FileNotFoundError:
        return dict(status="UNTESTED", reason="missing_source_camera_artifacts")
    except (KeyError, ValueError, TypeError, AttributeError, OSError):
        return dict(status="FAIL", reason="malformed_source_camera_artifacts")
