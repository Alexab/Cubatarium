"""Deterministic scenes prepared in an already isolated smoke-world copy."""
from __future__ import annotations

import json
import math
from pathlib import Path


def prepare_negative_cave(fixture: Path, minimum_chunks: int, *, flooded: bool = False,
                          wide: bool = False) -> dict:
    """Saved starting position is prepared before load; no runtime relocation."""
    fixture = fixture.resolve()
    if fixture.parent.name != "worlds" or not fixture.name.startswith("EngineSmoke_"):
        raise ValueError("scene preparation requires an isolated EngineSmoke_ world")
    if not 1 <= minimum_chunks <= 64:
        raise ValueError("cave fixture coverage must be 1..64 chunks")
    chunks = fixture / "chunks"
    if chunks.is_symlink() or not chunks.is_dir():
        raise ValueError("fixture chunks must be a real directory")
    metadata = json.loads((fixture / "world_data.json").read_text(encoding="utf-8"))
    profile = metadata.get("procedural", {})
    required_min = -64 if wide else -48
    if profile.get("min_height", 0) > required_min or profile.get("max_height", 0) < 63:
        raise ValueError(f"cave fixture requires a world containing Y={required_min}..63")
    users_path = fixture / "users.json"
    users = json.loads(users_path.read_text(encoding="utf-8"))
    if "Username" not in users:
        raise ValueError("fixture requires the flight runner's Username save")
    distance = minimum_chunks * 16 + 48
    floor_y, ceiling_y = (-61, -16) if wide else (-44, -30)
    cave_z_low, cave_z_high = (35, 67) if wide else (45, 57)
    start = [22.0, floor_y + 0.5 + 1.62, 51.0]
    min_cx = math.floor((start[0] - distance - 32) / 16)
    max_cx = math.floor((start[0] + 32) / 16)
    written = 0
    omitted_air = 0
    for cx in range(min_cx, max_cx + 1):
        for cz in range(2, 5):
            for cy in ((-4, -3, -2, -1, 3) if wide else (-3, -2, 3)):
                voxels = []
                for lz in range(16):
                    z = cz * 16 + lz
                    for ly in range(16):
                        y = cy * 16 + ly
                        for lx in range(16):
                            # Wide, level cave, with a solid floor/ceiling and
                            # an ocean separated from the eye by solid rock.
                            inside_cave = floor_y < y < ceiling_y and cave_z_low <= z <= cave_z_high
                            if inside_cave and not flooded:
                                continue
                            kind = "water" if inside_cave or 60 <= y <= 63 else "stone"
                            voxels.append({"lx": lx, "ly": ly, "lz": lz, "type": kind})
                stem = chunks / f"{cx}_{cy}_{cz}"
                binary = stem.with_suffix(".cchunk")
                target = stem.with_suffix(".json")
                if binary.is_symlink() or target.is_symlink():
                    raise ValueError("fixture chunk symlinks are forbidden")
                binary.unlink(missing_ok=True)
                if not voxels:
                    # The real column loader must reconstruct internal air
                    # sections from a sparse saved column with a floor/roof.
                    target.unlink(missing_ok=True)
                    omitted_air += 1
                    continue
                target.write_text(json.dumps({"format_version": 4, "cx": cx,
                    "cy": cy, "cz": cz, "voxels": voxels}, separators=(",", ":")),
                    encoding="utf-8")
                written += 1
    user = users["Username"]
    user.update(position=start, yaw=180.0, pitch=0.0)
    users_path.write_text(json.dumps(users, indent=2) + "\n", encoding="utf-8")
    creatures_path = fixture / "creatures.json"
    if creatures_path.exists():
        creatures = json.loads(creatures_path.read_text(encoding="utf-8"))
        for creature in creatures.get("creatures", []):
            if creature.get("id") == user.get("player_creature_id"):
                creature["body_origin"] = [start[0], floor_y + 0.5, start[2]]
        creatures_path.write_text(json.dumps(creatures, indent=2) + "\n", encoding="utf-8")
    # These summaries describe the source geometry, which was replaced above.
    for name in ("column_light.json", "ownership_snapshot.json", "ownership_snapshot.previous.json"):
        (fixture / name).unlink(missing_ok=True)
    return {"schema": "engine-smoke-fixture.v1",
            "name": ("wet-" if flooded else "dry-") + ("wide-" if wide else "") + "negative-cave",
            "starting_eye": start, "chunk_x_bounds": [min_cx, max_cx],
            "chunks_replaced": written, "omitted_internal_air_slices": omitted_air,
            "cave_bounds": {"floor": floor_y + 0.5, "ceiling": ceiling_y - 0.5,
                            "z_low": cave_z_low - 0.5, "z_high": cave_z_high + 0.5},
            "route_rise": 30 if wide else 6,
            "expected_eye_in_fluid": flooded,
            "expected_max_eye_y": ceiling_y - 0.5, "route_yaw": 180.0}


def prepare_open_sky_plane(fixture: Path, minimum_chunks: int) -> dict:
    """Keep only a low floor: AIR lies above the highest saved geometry."""
    import hashlib
    import re
    scene = prepare_negative_cave(fixture, minimum_chunks, wide=True)
    metadata = json.loads((fixture / "world_data.json").read_text(encoding="utf-8"))
    packs = metadata.get("resource_packs", {})
    material_path = fixture.parent.parent / "resource_packs/cubatarium_worldgen_cc0/blocks/stone.json"
    payload = material_path.read_bytes()
    material = json.loads(payload)
    if (packs.get("primary") != ["cubatarium_worldgen_cc0"] or packs.get("secondary", []) or
            material.get("name") != "stone" or material.get("id") != 560 or
            material.get("lighting", {}).get("emission", 0) != 0 or
            material.get("render", {}).get("style", "cube") != "cube" or
            material.get("render", {}).get("transparent", False) or "physics" in material):
        raise ValueError("open-sky reference requires the unmodified opaque stone catalog")
    floor_y = -61
    floor_cy = floor_y // 16
    lo, hi = scene["chunk_x_bounds"]
    removed = 0
    for path in (fixture / "chunks").iterdir():
        match = re.fullmatch(r"(-?\d+)_(-?\d+)_(-?\d+)\.(json|cchunk)", path.name)
        if match:
            cx, cy, cz = map(int, match.groups()[:3])
            if lo <= cx <= hi and 2 <= cz <= 4 and cy > floor_cy:
                if path.is_symlink():
                    raise ValueError("fixture chunk symlinks are forbidden")
                path.unlink()
                removed += 1
    for cx in range(lo, hi + 1):
        for cz in range(2, 5):
            path = fixture / "chunks" / f"{cx}_{floor_cy}_{cz}.json"
            if path.is_symlink():
                raise ValueError("fixture chunk symlinks are forbidden")
            voxels = [dict(lx=x, ly=y, lz=z, type="stone")
                      for x in range(16) for y in range(floor_y % 16 + 1) for z in range(16)]
            path.write_text(json.dumps(dict(format_version=4, cx=cx, cy=floor_cy,
                cz=cz, voxels=voxels), separators=(",", ":")), encoding="utf-8")
    scene.update(name="open-sky-plane", plane_bounds=dict(floor=floor_y + .5,
        z_low=31.5, z_high=79.5), route_rise=20, expected_max_eye_y=floor_y + 80,
        chunks_replaced=(hi-lo+1)*3, chunks_removed=removed,
        omitted_internal_air_slices=0, required_air_light_service=True,
        lighting_fixture=dict(schema="engine-direct-skylight-fixture.v1", sky_level=15,
            material_id=560, material_name="stone", material_sha256=hashlib.sha256(payload).hexdigest()))
    scene.pop("cave_bounds")
    return scene


def prepare_visible_items_plane(fixture: Path, minimum_chunks: int) -> dict:
    """Two ordinary saved loose bodies, prepared only in the disposable clone."""
    import copy
    import hashlib
    scene = prepare_open_sky_plane(fixture, minimum_chunks)
    path = fixture / "creatures.json"
    if path.is_symlink():
        raise ValueError("fixture creature symlinks are forbidden")
    creatures = json.loads(path.read_text(encoding="utf-8"))
    users = json.loads((fixture / "users.json").read_text(encoding="utf-8"))
    rows = creatures.get("world_physical_v2", [])
    if not isinstance(rows, list) or any(not isinstance(row, dict) for row in rows):
        raise ValueError("invalid saved physical bodies")
    template = next((row for row in rows if row.get("item") == "raw_meat" and
                     row.get("kind") == 4 and not row.get("carrier") and
                     not row.get("creature")), None)
    if template is None:
        raise ValueError("visible-item fixture requires a saved loose raw_meat body")
    size = template.get("size", [])
    if (len(size) != 3 or any(type(v) not in (int, float) or not math.isfinite(v) or
                             not .02 <= v <= 5 for v in size)):
        raise ValueError("invalid saved visible-item physical size")
    asset = fixture.parent.parent / "models/items/raw_meat/model.glb"
    if asset.is_symlink():
        raise ValueError("fixture item asset symlinks are forbidden")
    payload = asset.read_bytes()
    if (asset.is_symlink() or len(payload) < 20 or payload[:4] != b"glTF" or
            int.from_bytes(payload[4:8], "little") != 2 or
            int.from_bytes(payload[8:12], "little") != len(payload)):
        raise ValueError("visible-item fixture requires a valid raw_meat GLB asset")
    instances = set()
    def collect(value):
        if isinstance(value, dict):
            for key, child in value.items():
                if key in ("instance", "instance_id") and type(child) is int and child > 0:
                    instances.add(child)
                collect(child)
        elif isinstance(value, list):
            for child in value:
                collect(child)
    collect(creatures)
    collect(users)
    next_instance = max(instances, default=0) + 1
    if next_instance + 1 > 2**64-1:
        raise ValueError("visible-item fixture cannot allocate unique inventory identities")
    next_id = max((row.get("id", 0) for row in rows), default=0) + 1
    start = scene["starting_eye"]
    distance = minimum_chunks * 16 + 48
    positions = [[start[0]+4-offset, scene["plane_bounds"]["floor"]+size[1]/2+.01,
                  start[2]] for offset in (0, distance)]
    for index, position in enumerate(positions):
        row = copy.deepcopy(template)
        row.update(id=next_id+index, instance=next_instance+index, count=1,
                   position=position, velocity=[0, 0, 0], age=0, flying=False,
                   arrow=False, carrier=0, creature=0, thrower=0, last_contact=0,
                   contact_cooldown=0, held_time=0, charge=0, damage=0,
                   pickup_separated=True, motion="resting")
        rows.append(row)
    path.write_text(json.dumps(creatures, indent=2)+"\n", encoding="utf-8")
    scene["visible_world_items"] = dict(schema="engine-world-item-field-fixture.v1",
        item="raw_meat", positions=positions, instances=[next_instance, next_instance+1],
        available_glb_sha256=hashlib.sha256(payload).hexdigest(),
        minimum_frame_epochs=3,
        scope="normal saved loose bodies; CPU sample and actual primitive issue, no final RGB")
    return scene


def prepare_lit_negative_cave(fixture: Path, minimum_chunks: int) -> dict:
    """Positive blocklight scene. Runtime catalog/draw acceptance is separate."""
    import hashlib
    metadata = json.loads((fixture / "world_data.json").read_text(encoding="utf-8"))
    packs = metadata.get("resource_packs", {})
    if packs.get("primary") != ["cubatarium_worldgen_cc0"] or packs.get("secondary", []):
        raise ValueError("lit fixture requires the unmodified worldgen primary pack selection")
    material_path = fixture.parent.parent / "resource_packs/cubatarium_worldgen_cc0/blocks/glowstone.json"
    payload = material_path.read_bytes()
    material = json.loads(payload)
    if (material.get("name") != "glowstone" or material.get("id") != 376 or
            material.get("lighting", {}).get("emission") != 15 or
            material.get("render", {}).get("style", "cube") != "cube" or
            "physics" in material):
        raise ValueError("lit fixture glowstone definition is incompatible with the reference scene")
    scene = prepare_negative_cave(fixture, minimum_chunks)
    floor = round(scene["cave_bounds"]["floor"] - 0.5)
    emitters = []
    for cx in range(scene["chunk_x_bounds"][0], scene["chunk_x_bounds"][1] + 1):
        path = fixture / "chunks" / f"{cx}_{floor // 16}_3.json"
        chunk = json.loads(path.read_text(encoding="utf-8"))
        for voxel in chunk["voxels"]:
            if voxel["ly"] == floor % 16 and voxel["lz"] == 3 and voxel["lx"] in (0, 8):
                if voxel["type"] != "stone":
                    raise ValueError("lit fixture emitter must replace an opaque floor voxel")
                voxel["type"] = "glowstone"
                emitters.append([cx * 16 + voxel["lx"], floor, 51])
        path.write_text(json.dumps(chunk, separators=(",", ":")), encoding="utf-8")
    scene["lighting_fixture"] = {
        "schema": "engine-positive-blocklight-fixture.v1", "emitters": emitters,
        "emission": 15, "material_name": "glowstone", "material_id": 376,
        "material_sha256": hashlib.sha256(payload).hexdigest(),
        "source_material_path": str(material_path.resolve()),
        "scope": "prepared opaque floor emitters; current runtime catalog, physical transport and drawn light are not yet verified"}
    return scene


def prepare_dry_negative_cave(fixture: Path, minimum_chunks: int) -> dict:
    return prepare_negative_cave(fixture, minimum_chunks)


def evaluate_fixture_trace(scene: dict, trace: Path) -> dict:
    """Check analytic scene expectations independently of RGB brightness."""
    if not trace.is_file():
        return {"status": "UNTESTED", "reason": "missing_fixture_trace"}
    samples = 0
    missing = 0
    try:
        for line in trace.read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            pos = row.get("position", [])
            if (row.get("schema") != "diagnostic-bot-trace.v1" or len(pos) != 3 or
                    not all(isinstance(x, (int, float)) and not isinstance(x, bool)
                            and math.isfinite(x) for x in pos)):
                return {"status": "FAIL", "reason": "malformed_fixture_trace"}
            if samples == 0 and math.dist(pos, scene["starting_eye"]) > 2:
                return {"status": "FAIL", "reason": "fixture_start_position_changed"}
            if pos[1] > scene["expected_max_eye_y"]:
                return {"status": "FAIL", "reason": "camera_left_negative_cave"}
            if scene.get("name") == "open-sky-plane" and pos[1] <= scene["plane_bounds"]["floor"]:
                return {"status": "FAIL", "reason": "camera_below_open_sky_floor"}
            inside = row.get("eye_in_fluid")
            if not isinstance(inside, bool):
                missing += 1
            elif inside != scene["expected_eye_in_fluid"]:
                return {"status": "FAIL", "reason":
                        "wet_cave_classified_as_air" if scene["expected_eye_in_fluid"]
                        else "dry_cave_classified_as_fluid"}
            samples += 1
    except (ValueError, TypeError, AttributeError):
        return {"status": "FAIL", "reason": "malformed_fixture_trace"}
    if samples < 2 or missing:
        return {"status": "UNTESTED", "reason": "missing_fixture_observations",
                "samples": samples, "missing_fluid_samples": missing}
    if scene.get("name") == "open-sky-plane":
        return {"status": "PASS", "reason": "open_sky_plane_observed", "samples": samples}
    return {"status": "PASS", "reason":
            "negative_wet_cave_observed" if scene["expected_eye_in_fluid"]
            else "negative_dry_cave_observed", "samples": samples}
