"""Select a retained lighting fault and author ordinary input exploration."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(selection):
    if (selection.get("schema") != "engine-debt-target.v1" or
            selection.get("kind") != "direct_sky_floor_draw_source" or
            not isinstance(selection.get("cell"), list) or len(selection["cell"]) != 3 or
            any(type(v) is not int for v in selection["cell"])):
        raise ValueError("invalid concrete debt cell")
    for name in ("fixture_sha256", "source_world_sha256", "verdict_sha256"):
        value = selection.get(name)
        if not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value):
            raise ValueError("missing debt provenance hash")


def select(artifacts: Path):
    verdict = json.loads((artifacts / "smoke-verdict.json").read_text(encoding="utf-8"))
    scene = json.loads((artifacts / "fixture.json").read_text(encoding="utf-8"))
    manifest = json.loads((artifacts / "route.json").read_text(encoding="utf-8"))
    fault = verdict["components"]["fixture_floor_draw_source_light"]
    if fault.get("status") != "FAIL" or scene.get("name") != "open-sky-plane":
        raise ValueError("selection requires an observed failed direct-sky floor fixture")
    candidates = []
    for example in fault.get("examples", []):
        cell = example.get("floor_cell")
        if (not isinstance(cell, list) or len(cell) != 3 or any(type(v) is not int for v in cell) or
                example.get("expected_sky_level") != 15 or example.get("source_sky_light") != 0 or
                example.get("source_material_id") != scene["lighting_fixture"]["material_id"]):
            continue
        x, y, z = cell
        lo, hi = scene["chunk_x_bounds"]
        plane = scene["plane_bounds"]
        if (lo*16 <= x < (hi+1)*16 and y + .5 == plane["floor"] and
                plane["z_low"] < z < plane["z_high"]):
            candidates.append(cell)
    if not candidates:
        raise ValueError("no supported concrete fault cell")
    cell = min(candidates, key=lambda p: (math.dist(p, scene["starting_eye"]), p))
    selection = dict(schema="engine-debt-target.v1", kind="direct_sky_floor_draw_source",
                     cell=cell, expected_sky_level=15, observed_source_sky_level=0,
                     fixture_sha256=digest(artifacts / "fixture.json"),
                     source_world_sha256=manifest["source_world_sha256"],
                     verdict_sha256=digest(artifacts / "smoke-verdict.json"),
                     source_artifacts=str(artifacts))
    validate(selection)
    return selection


def author(route, selection, scene, *, fixture_hash, source_hash, minimum_chunks):
    validate(selection)
    if select(Path(selection["source_artifacts"])) != selection:
        raise ValueError("original observed debt evidence changed")
    if (fixture_hash != selection["fixture_sha256"] or source_hash != selection["source_world_sha256"] or
            scene.get("name") != "open-sky-plane" or route.get("movement_mode") != "walk" or
            route.get("relative") is not True or len(route.get("waypoints", [])) != 2):
        raise ValueError("debt route requires the same source/fixture and a two-point relative walk")
    start = scene["starting_eye"]
    x, y, z = selection["cell"]
    if math.hypot(x-start[0], z-start[2]) < minimum_chunks * 16:
        raise ValueError("selected debt does not cover the requested excursion")
    point = route["waypoints"][1]
    point["position"] = [x-start[0], 0, z-start[2]]
    point["inspection_targets"] = [[x-start[0], y+.5-start[1], z-start[2]]]
    point["inspect"] = True
    route["debt_target"] = selection
    return route


def evaluate(artifacts: Path, selection, report):
    def result(status, reason, **values):
        return dict(schema="engine-debt-inspection-evidence.v1", status=status, reason=reason,
                    scope="observed pose, directed capture and analytic floor-cell ray; not a light fix or framebuffer reference", **values)
    try:
        validate(selection)
        if select(Path(selection["source_artifacts"])) != selection:
            raise ValueError("original observed debt evidence changed")
        route = json.loads((artifacts / "bot-route.json").read_text(encoding="utf-8"))
        if route.get("debt_target") != selection:
            raise ValueError("selected debt differs from authored route")
        from engine_smoke_run import inspection_evidence, capture_receipt_matches
        inspection = inspection_evidence(artifacts, report)
        if (inspection.get("join_strength") != "request_readback_frame_pose_png" or
                inspection.get("requested", 0) <= 0 or inspection.get("verified") != inspection.get("requested")):
            return result("FAIL", "debt_inspection_capture_incomplete")
        capture_id = inspection["requested"] - 1
        rows = [json.loads(line) for line in (artifacts / "diagnostic_bot.jsonl").read_text(encoding="utf-8").splitlines()]
        pose = next(row for row in rows if row.get("capture_request") and row.get("capture_id") == capture_id)
        png = artifacts / "captures" / f"inspection_{capture_id:04}.png"
        frame, _ = capture_receipt_matches(png, pose, capture_id, -1, -1)
        pitch, yaw = math.radians(pose["pitch"]), math.radians(pose["yaw"])
        direction = [math.cos(pitch)*math.cos(yaw), math.sin(pitch), math.cos(pitch)*math.sin(yaw)]
        x, y, z = selection["cell"]
        if direction[1] >= -.001:
            return result("FAIL", "debt_capture_not_aimed_at_floor")
        distance = (y+.5-pose["position"][1]) / direction[1]
        hit = [a+distance*b for a, b in zip(pose["position"], direction)]
        if distance <= 0 or abs(hit[0]-x) > .5 or abs(hit[2]-z) > .5:
            return result("FAIL", "debt_capture_missed_selected_cell")
        if report.get("diagnostic_bot", {}).get("state") != "complete":
            return result("FAIL", "debt_route_not_complete")
        return result("PASS", "selected_debt_cell_inspected", cell=selection["cell"],
                      capture_id=capture_id, frame_sequence=frame, ray_hit=hit,
                      original_verdict_sha256=selection["verdict_sha256"], png_sha256=digest(png))
    except (OSError, ValueError, KeyError, TypeError, AttributeError, StopIteration) as error:
        return result("FAIL", "invalid_debt_inspection_evidence", diagnostic=str(error))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-artifacts", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    selection = select(args.source_artifacts)
    with args.output.open("x", encoding="utf-8") as output:
        output.write(json.dumps(selection, indent=2)+"\n")
    print(json.dumps(selection))


if __name__ == "__main__":
    main()
