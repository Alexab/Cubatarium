"""Static saved-player air-pocket fixture and strict native observation gate."""
import json
import math
from pathlib import Path

START = [22.0, -41.88, 51.0]
BOX = [[20, 24], [-43, -38], [49, 53]]


def prepare(fixture: Path, minimum_chunks: int) -> dict:
    from engine_smoke_fixtures import prepare_negative_cave
    scene = prepare_negative_cave(fixture, minimum_chunks, flooded=True)
    frame_path = fixture / "chunks/1_-3_3.json"
    frame = json.loads(frame_path.read_text(encoding="utf-8"))
    def inside(v):
        p = [16 + v["lx"], -48 + v["ly"], 48 + v["lz"]]
        return all(lo <= value <= hi for value, (lo, hi) in zip(p, BOX))
    carved = [v for v in frame["voxels"] if inside(v)]
    if len(carved) != 150 or any(v["type"] != "water" for v in carved):
        raise ValueError("air-pocket fixture requires exactly150 original wet cells")
    frame["voxels"] = [v for v in frame["voxels"] if not inside(v)]
    frame_path.write_text(json.dumps(frame, separators=(",", ":")), encoding="utf-8")
    metadata_path = fixture / "world_data.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    metadata["procedural"]["fill_water"] = False
    metadata_path.write_text(json.dumps(metadata, indent=2)+"\n", encoding="utf-8")
    scene.update(name="air-pocket-negative-cave", expected_eye_in_fluid=False,
                 air_pocket_box=BOX, carved_water_cells=150,
                 static_classification_only=True)
    return scene


def static_config(payload: bytes) -> bytes:
    config = json.loads(payload)
    config.setdefault("physics", {})["enable_fluids"] = False
    return (json.dumps(config, indent=2)+"\n").encode("utf-8")


def evaluate(artifacts: Path, report: dict, scene: dict) -> dict:
    def result(status, reason, **values):
        return dict(schema="air-pocket-load-evidence.v1", status=status, reason=reason,
                    scope="Static saved-player pose and local dry eye with actual wet neighbors; no fluid evolution, framebuffer or performance proof", **values)
    try:
        if (scene.get("name") != "air-pocket-negative-cave" or
                scene.get("starting_eye") != START or scene.get("air_pocket_box") != BOX or
                scene.get("carved_water_cells") != 150):
            raise ValueError("unexpected air-pocket fixture")
        if (report.get("exit_code") != 0 or report.get("teleport_cruise") is not False or
                type(report.get("diagnostic_relocations_after_start")) is not int or
                report["diagnostic_relocations_after_start"] != 0):
            return result("FAIL", "runtime_or_relocation_audit_failed")
        config = json.loads((artifacts/"config.air-pocket.json").read_text(encoding="utf-8"))
        if config.get("physics", {}).get("enable_fluids") is not False:
            raise ValueError("missing static fluid policy")
        rows = [json.loads(line) for line in (artifacts/"diagnostic_bot.jsonl").read_text(encoding="utf-8").splitlines()]
        if len(rows) < 10:
            return result("UNTESTED", "insufficient_air_pocket_observation")
        times = []
        for row in rows:
            pos = row.get("position")
            if (row.get("schema") != "diagnostic-bot-trace.v1" or not isinstance(pos, list) or
                    len(pos) != 3 or any(type(v) not in (int, float) or not math.isfinite(v) for v in pos)):
                raise ValueError("malformed air-pocket pose")
            if math.dist(pos, START) > .25 or any(not lo-.5 <= v < hi+.5 for v, (lo, hi) in zip(pos, BOX)):
                return result("FAIL", "safe_saved_pocket_pose_not_retained")
            if (row.get("eye_in_fluid") is not False or
                    row.get("air_pocket_above_in_fluid") is not True or
                    row.get("air_pocket_side_in_fluid") is not True):
                return result("FAIL", "pocket_or_surrounding_fluid_classification_wrong")
            t = row.get("time")
            if type(t) not in (int, float) or not math.isfinite(t):
                raise ValueError("missing observation time")
            times.append(t)
        if any(b <= a for a, b in zip(times, times[1:])) or times[-1]-times[0] < 5:
            return result("FAIL", "observation_not_distinct_or_too_short")
        return result("PASS", "saved_air_pocket_pose_retained_with_wet_neighbors",
                      samples=len(rows), observed_seconds=times[-1]-times[0], saved_eye=START)
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        return result("FAIL", "invalid_air_pocket_evidence", diagnostic=str(error))
