#!/usr/bin/env python3
"""Generate Luanti-style rigid_voxels creature catalog (JSON + PNG textures)."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent

try:
    from extra_creature_species import EXTRA_SPECIES
except ImportError:
    EXTRA_SPECIES = {}

HABITAT_MAP_PATH = TOOLS / "creature_habitat_map.json"

# Luanti-inspired palette (CC-style placeholders; replace with imported PNGs when available).
SPECIES: dict[str, dict] = {
    "human": {
        "display": "Human",
        "role": "controlled_default",
        "spawnable": False,
        "tags": ["humanoid"],
        "sort": 0,
        "archetype": "terrestrial_biped",
        "color": (90, 140, 200),
        "bounds": {"rest": [0.6, 1.8, 0.6], "max": [0.6, 1.8, 0.6], "min": [0.6, 1.5, 0.6]},
        "eye": 1.62,
        "walk": 3.0,
        "behavior": "none",
        "template": "biped_player",
        "icon": [0.35, 0.55, 0.85, 1.0],
    },
    "sheep": {
        "display": "Sheep",
        "tags": ["mobs", "passive"],
        "sort": 10,
        "archetype": "terrestrial_quadruped",
        "color": (230, 230, 235),
        "bounds": {"rest": [0.9, 1.1, 1.4], "max": [0.9, 1.1, 1.4], "min": [0.9, 0.7, 1.4]},
        "eye": 0.95,
        "walk": 2.0,
        "icon": [0.95, 0.95, 1.0, 1.0],
    },
    "wolf": {
        "display": "Wolf",
        "tags": ["mobs", "hostile"],
        "sort": 20,
        "archetype": "terrestrial_quadruped",
        "color": (120, 120, 125),
        "bounds": {"rest": [0.85, 1.0, 1.5], "max": [0.85, 1.0, 1.5], "min": [0.85, 0.65, 1.5]},
        "eye": 0.9,
        "walk": 2.8,
        "icon": [0.5, 0.5, 0.55, 1.0],
    },
    "pig": {
        "display": "Pig",
        "tags": ["mobs", "passive"],
        "sort": 15,
        "archetype": "terrestrial_quadruped",
        "color": (255, 180, 180),
        "bounds": {"rest": [0.95, 0.95, 1.5], "max": [0.95, 0.95, 1.5], "min": [0.95, 0.7, 1.5]},
        "eye": 0.85,
        "walk": 2.2,
        "icon": [1.0, 0.7, 0.7, 1.0],
    },
    "cow": {
        "display": "Cow",
        "tags": ["mobs", "passive"],
        "sort": 16,
        "archetype": "terrestrial_quadruped",
        "color": (140, 90, 60),
        "bounds": {"rest": [1.1, 1.3, 1.8], "max": [1.1, 1.3, 1.8], "min": [1.1, 0.9, 1.8]},
        "eye": 1.15,
        "walk": 1.8,
        "icon": [0.55, 0.35, 0.25, 1.0],
    },
    "chicken": {
        "display": "Chicken",
        "tags": ["mobs", "passive"],
        "sort": 17,
        "archetype": "aerial",
        "habitat": "terrestrial",
        "color": (255, 240, 200),
        "bounds": {"rest": [0.5, 0.7, 0.5], "max": [0.5, 0.7, 0.5], "min": [0.5, 0.5, 0.5]},
        "eye": 0.55,
        "walk": 1.5,
        "icon": [1.0, 0.95, 0.7, 1.0],
    },
    "oerkki": {
        "display": "Oerkki",
        "tags": ["mobs", "hostile"],
        "sort": 30,
        "archetype": "terrestrial_biped",
        "color": (60, 140, 80),
        "bounds": {"rest": [0.65, 1.7, 0.65], "max": [0.65, 1.7, 0.65], "min": [0.65, 1.3, 0.65]},
        "eye": 1.55,
        "walk": 2.5,
        "icon": [0.2, 0.55, 0.3, 1.0],
    },
    "skeleton": {
        "display": "Skeleton",
        "tags": ["mobs", "hostile"],
        "sort": 31,
        "archetype": "terrestrial_biped",
        "color": (210, 210, 200),
        "bounds": {"rest": [0.6, 1.85, 0.6], "max": [0.6, 1.85, 0.6], "min": [0.6, 1.4, 0.6]},
        "eye": 1.6,
        "walk": 2.4,
        "icon": [0.85, 0.85, 0.8, 1.0],
    },
    "sand_monster": {
        "display": "Sand Monster",
        "tags": ["mobs", "hostile"],
        "sort": 32,
        "archetype": "terrestrial_biped",
        "color": (200, 170, 90),
        "bounds": {"rest": [0.75, 1.9, 0.75], "max": [0.75, 1.9, 0.75], "min": [0.75, 1.4, 0.75]},
        "eye": 1.65,
        "walk": 2.2,
        "icon": [0.8, 0.65, 0.3, 1.0],
    },
}

SPECIES.update(EXTRA_SPECIES)

for _sid, _meta in SPECIES.items():
    if _sid != "human" and "habitat" not in _meta:
        _meta["habitat"] = "terrestrial"

SKINS = [
    {
        "id": "human_adventurer",
        "display": "Adventurer",
        "creature_id": "human",
        "color": (242, 191, 51),
        "wire": [0.95, 0.75, 0.2, 1.0],
        "map": {"body": "torso", "face": "face", "leg": "legs", "arm": "arms"},
        "stems": ["torso", "face", "legs", "arms"],
    },
    {
        "id": "human_guard",
        "display": "Guard",
        "creature_id": "human",
        "color": (140, 140, 153),
        "wire": [0.55, 0.55, 0.6, 1.0],
        "map": {"body": "torso", "face": "face", "leg": "legs", "arm": "arms"},
        "stems": ["torso", "face", "legs", "arms"],
    },
    {
        "id": "sheep_wool_black",
        "display": "Black Wool",
        "creature_id": "sheep",
        "color": (40, 40, 45),
        "wire": [0.2, 0.2, 0.25, 1.0],
        "map": {"body": "body", "face": "face", "leg": "leg", "ear": "ear", "tail": "tail"},
        "stems": ["body", "face", "leg", "ear", "tail"],
    },
    {
        "id": "sheep_wool_golden",
        "display": "Golden Wool",
        "creature_id": "sheep",
        "color": (255, 220, 80),
        "wire": [1.0, 0.9, 0.3, 1.0],
        "map": {"body": "body", "face": "face", "leg": "leg", "ear": "ear", "tail": "tail"},
        "stems": ["body", "face", "leg", "ear", "tail"],
    },
    {
        "id": "wolf_snow",
        "display": "Snow Wolf",
        "creature_id": "wolf",
        "color": (200, 210, 220),
        "wire": [0.8, 0.85, 0.9, 1.0],
        "map": {"body": "body", "face": "face", "leg": "leg"},
        "stems": ["body", "face", "leg"],
    },
]

REMOVE_SPECIES = ("scout", "brute", "drifter")
REMOVE_SKINS = ("scout_golden", "brute_rust", "drifter_ice")


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_solid_png(path: Path, rgb: tuple[int, int, int], size: int = 64) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    raw = bytearray()
    for _ in range(size):
        raw.append(0)
        for _ in range(size):
            raw.extend((*rgb, 255))
    compressed = zlib.compress(bytes(raw), 9)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", compressed) + png_chunk(b"IEND", b"")
    path.write_bytes(png)


def biped_parts(pivot: bool) -> list[dict]:
    parts = [
        {"id": "torso", "offset": [0.0, 0.95, 0.0], "size": [0.5, 1.0, 0.32], "texture": "body"},
        {"id": "arm_l", "offset": [-0.34, 1.02, 0.0], "size": [0.16, 0.52, 0.16], "texture": "arm"},
        {"id": "arm_r", "offset": [0.34, 1.02, 0.0], "size": [0.16, 0.52, 0.16], "texture": "arm"},
        {"id": "head", "offset": [0.0, 1.58, 0.0], "size": [0.38, 0.38, 0.38], "texture": "face"},
        {"id": "leg_l", "offset": [-0.14, 0.32, 0.0], "size": [0.2, 0.65, 0.2], "texture": "leg"},
        {"id": "leg_r", "offset": [0.14, 0.32, 0.0], "size": [0.2, 0.65, 0.2], "texture": "leg"},
    ]
    if pivot:
        parts[1]["pivot"] = [-0.34, 1.28, 0.0]
        parts[1]["limb"] = "arm"
        parts[2]["pivot"] = [0.34, 1.28, 0.0]
        parts[2]["limb"] = "arm"
        parts[4]["pivot"] = [-0.14, 0.65, 0.0]
        parts[4]["limb"] = "leg"
        parts[5]["pivot"] = [0.14, 0.65, 0.0]
        parts[5]["limb"] = "leg"
    return parts


def quadruped_parts() -> list[dict]:
    return [
        {"id": "torso", "offset": [0.0, 0.75, 0.0], "size": [0.7, 0.55, 1.1], "texture": "body"},
        {"id": "head", "offset": [0.0, 0.95, 0.65], "size": [0.45, 0.45, 0.45], "texture": "face"},
        {"id": "neck", "offset": [0.0, 0.88, 0.35], "size": [0.25, 0.3, 0.25], "texture": "body"},
        {
            "id": "leg_fl",
            "offset": [-0.28, 0.32, 0.35],
            "size": [0.18, 0.55, 0.18],
            "texture": "leg",
            "pivot": [-0.28, 0.6, 0.35],
            "limb": "leg",
        },
        {
            "id": "leg_fr",
            "offset": [0.28, 0.32, 0.35],
            "size": [0.18, 0.55, 0.18],
            "texture": "leg",
            "pivot": [0.28, 0.6, 0.35],
            "limb": "leg",
        },
        {
            "id": "leg_bl",
            "offset": [-0.28, 0.32, -0.35],
            "size": [0.18, 0.55, 0.18],
            "texture": "leg",
            "pivot": [-0.28, 0.6, -0.35],
            "limb": "leg",
        },
        {
            "id": "leg_br",
            "offset": [0.28, 0.32, -0.35],
            "size": [0.18, 0.55, 0.18],
            "texture": "leg",
            "pivot": [0.28, 0.6, -0.35],
            "limb": "leg",
        },
    ]


def aerial_parts() -> list[dict]:
    return [
        {"id": "torso", "offset": [0.0, 0.45, 0.0], "size": [0.4, 0.45, 0.55], "texture": "body"},
        {"id": "head", "offset": [0.0, 0.62, 0.2], "size": [0.22, 0.22, 0.22], "texture": "face"},
        {
            "id": "wing_l",
            "offset": [-0.35, 0.5, 0.0],
            "size": [0.15, 0.08, 0.45],
            "texture": "body",
            "pivot": [-0.2, 0.5, 0.0],
            "limb": "arm",
        },
        {
            "id": "wing_r",
            "offset": [0.35, 0.5, 0.0],
            "size": [0.15, 0.08, 0.45],
            "texture": "body",
            "pivot": [0.2, 0.5, 0.0],
            "limb": "arm",
        },
        {
            "id": "leg_l",
            "offset": [-0.1, 0.18, 0.05],
            "size": [0.08, 0.22, 0.08],
            "texture": "leg",
            "pivot": [-0.1, 0.3, 0.05],
            "limb": "leg",
        },
        {
            "id": "leg_r",
            "offset": [0.1, 0.18, 0.05],
            "size": [0.08, 0.22, 0.08],
            "texture": "leg",
            "pivot": [0.1, 0.3, 0.05],
            "limb": "leg",
        },
    ]


def load_rigid_parts_yaml() -> dict:
    path = TOOLS / "creature_rigid_parts.yaml"
    if not path.is_file():
        return {}
    if yaml is None:
        raise SystemExit("PyYAML required: pip install pyyaml")
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def aquatic_parts() -> list[dict]:
    return [
        {"id": "torso", "offset": [0.0, 0.25, 0.0], "size": [0.55, 0.35, 1.0], "texture": "body"},
        {"id": "head", "offset": [0.0, 0.3, 0.65], "size": [0.35, 0.3, 0.35], "texture": "face"},
        {
            "id": "tail",
            "offset": [0.0, 0.28, -0.55],
            "size": [0.2, 0.15, 0.45],
            "texture": "body",
            "pivot": [0.0, 0.28, -0.35],
            "limb": "arm",
        },
        {
            "id": "fin_l",
            "offset": [-0.35, 0.2, 0.0],
            "size": [0.12, 0.08, 0.35],
            "texture": "leg",
            "pivot": [-0.28, 0.2, 0.0],
            "limb": "arm",
        },
        {
            "id": "fin_r",
            "offset": [0.35, 0.2, 0.0],
            "size": [0.12, 0.08, 0.35],
            "texture": "leg",
            "pivot": [0.28, 0.2, 0.0],
            "limb": "arm",
        },
    ]


def serpentine_parts() -> list[dict]:
    return [
        {"id": "torso", "offset": [0.0, 0.32, 0.0], "size": [0.7, 0.28, 0.85], "texture": "body"},
        {"id": "head", "offset": [0.0, 0.34, 0.52], "size": [0.28, 0.22, 0.28], "texture": "face"},
        {
            "id": "tail",
            "offset": [0.0, 0.3, -0.52],
            "size": [0.22, 0.18, 0.48],
            "texture": "body",
            "pivot": [0.0, 0.3, -0.32],
            "limb": "arm",
        },
    ]


def load_habitat_map() -> dict[str, str]:
    if not HABITAT_MAP_PATH.is_file():
        return {}
    return json.loads(HABITAT_MAP_PATH.read_text(encoding="utf-8"))


def species_parts(species_id: str, meta: dict, rigid_parts: dict) -> list[dict]:
    if species_id in rigid_parts and "parts" in rigid_parts[species_id]:
        return rigid_parts[species_id]["parts"]
    archetype = meta["archetype"]
    if archetype == "terrestrial_quadruped":
        return quadruped_parts()
    if archetype == "aerial":
        return aerial_parts()
    if archetype == "aquatic":
        return aquatic_parts()
    if archetype == "serpentine":
        return serpentine_parts()
    return biped_parts(pivot=(species_id == "human" or True))


def build_creature_json(species_id: str, meta: dict, rigid_parts: dict) -> dict:
    parts = species_parts(species_id, meta, rigid_parts)
    role = meta.get("role", "mob")
    archetype = meta["archetype"]
    habitat = meta.get("habitat", "terrestrial")
    behavior = meta.get("behavior", "wander" if role == "mob" else "none")
    can_fly = species_id == "human" or habitat in (
        "aquatic",
        "aerial",
        "amphibious",
        "lava",
    )
    return {
        "id": species_id,
        "display_name": meta["display"],
        "catalog": {
            "tags": meta["tags"],
            "spawnable": meta.get("spawnable", role == "mob"),
            "sort_order": meta["sort"],
        },
        "role": role,
        "bounds": meta["bounds"],
        "eye_height": meta["eye"],
        "habitat": habitat,
        "locomotion_archetype": archetype,
        "locomotion": {
            "can_fly": can_fly,
            "can_crouch": species_id == "human",
            "can_jump": habitat in ("terrestrial", "amphibious"),
            "jump_height": 1.0 if species_id != "human" else 1.25,
            "walk_speed": meta["walk"],
        },
        "behavior": behavior,
        "behavior_params": {
            "move_speed": meta["walk"],
            "wander_interval_min": 2.0,
            "wander_interval_max": 4.0,
        },
        "visual": {
            "backend": "rigid_voxels",
            "texture_layout": "player_skin_atlas"
            if species_id == "human"
            else "rigid_crop",
            "animation": {
                "walk_cycle_hz": 2.0,
                "leg_swing_deg": 25,
                "arm_swing_deg": 15,
                "fly_body_pitch_deg": 10,
                "body_bob_blocks": 0.025,
                "tail_swing_deg": 12,
                "run_speed_multiplier": 1.3,
                "crouch_leg_bend_deg": 25,
                "wing_idle_swing_deg": 5,
            },
            "default_texture": "body",
            "parts": parts,
            "icon": {"mode": "parts_preview", "color": meta["icon"]},
        },
    }


def textures_are_imported(species_id: str) -> bool:
    license_path = ROOT / "models" / "creatures" / species_id / "LICENSE.txt"
    if not license_path.is_file():
        return False
    text = license_path.read_text(encoding="utf-8")
    return "Placeholder procedural" not in text


def write_textures(species_id: str, color: tuple[int, int, int]) -> None:
    if textures_are_imported(species_id):
        print(f"skip textures {species_id} (imported LICENSE present)")
        return
    tex = ROOT / "models" / "creatures" / species_id / "textures"
    for stem in ("body", "leg", "arm", "face"):
        write_solid_png(tex / f"{stem}.png", color)
    write_solid_png(tex / "icon.png", color, 32)


def write_license(species_id: str) -> None:
    path = ROOT / "models" / "creatures" / species_id / "LICENSE.txt"
    if textures_are_imported(species_id):
        return
    path.write_text(
        "Placeholder procedural textures for Cubatarium.\n"
        "Replace with CC-licensed Luanti assets when imported.\n",
        encoding="utf-8",
    )


def skin_textures_are_imported(skin_id: str) -> bool:
    license_path = ROOT / "models" / "skins" / skin_id / "LICENSE.txt"
    if not license_path.is_file():
        return False
    return "Placeholder" not in license_path.read_text(encoding="utf-8")


def write_skin(skin: dict) -> None:
    base = ROOT / "models" / "skins" / skin["id"]
    tex = base / "textures"
    if not skin_textures_are_imported(skin["id"]):
        for stem in skin["stems"]:
            write_solid_png(tex / f"{stem}.png", skin["color"])
    data = {
        "id": skin["id"],
        "display_name": skin["display"],
        "creature_id": skin["creature_id"],
        "catalog": {"tags": ["outfits"], "equippable": True, "sort_order": 0},
        "visual": {
            "texture": skin["stems"][0],
            "texture_map": skin["map"],
            "wireframe_color": skin["wire"],
            "icon": {"mode": "skin_texture", "fallback_color": skin["wire"]},
        },
    }
    (base / "skin.json").write_text(
        json.dumps(data, indent=2) + "\n", encoding="utf-8"
    )


def remove_legacy() -> None:
    for sid in REMOVE_SPECIES:
        p = ROOT / "models" / "creatures" / sid
        if p.exists():
            import shutil

            shutil.rmtree(p)
            print(f"removed species {sid}")
    for sid in REMOVE_SKINS:
        p = ROOT / "models" / "skins" / sid
        if p.exists():
            import shutil

            shutil.rmtree(p)
            print(f"removed skin {sid}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--preserve-parts",
        action="store_true",
        help="Keep visual.parts from existing creature.json when present",
    )
    args = parser.parse_args()

    rigid_parts = load_rigid_parts_yaml()
    habitat_map = load_habitat_map()
    for species_id, meta in SPECIES.items():
        if species_id in habitat_map and "habitat" not in meta:
            meta["habitat"] = habitat_map[species_id]
    remove_legacy()
    for species_id, meta in SPECIES.items():
        folder = ROOT / "models" / "creatures" / species_id
        folder.mkdir(parents=True, exist_ok=True)
        creature_path = folder / "creature.json"
        existing_parts = None
        if args.preserve_parts and creature_path.is_file():
            existing = json.loads(creature_path.read_text(encoding="utf-8"))
            existing_parts = existing.get("visual", {}).get("parts")
        data = build_creature_json(species_id, meta, rigid_parts)
        if existing_parts:
            data["visual"]["parts"] = existing_parts
        creature_path.write_text(
            json.dumps(data, indent=2) + "\n", encoding="utf-8"
        )
        write_textures(species_id, meta["color"])
        write_license(species_id)
        print(f"wrote creature {species_id}")
    for skin in SKINS:
        write_skin(skin)
        print(f"wrote skin {skin['id']}")


if __name__ == "__main__":
    main()
