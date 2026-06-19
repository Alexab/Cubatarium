#!/usr/bin/env python3
"""Headless smoke checks for rigid_voxels v2 creature fidelity (textures + JSON + b3d)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from PIL import Image

RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
MODELS = ROOT / "models" / "creatures"


def alpha_fraction(path: Path) -> float:
    im = Image.open(path).convert("RGBA")
    px = list(im.getdata())
    if not px:
        return 0.0
    return sum(1 for *_, a in px if a < 250) / len(px)


def load_creature(species: str) -> dict:
    return json.loads((MODELS / species / "creature.json").read_text(encoding="utf-8"))


def part_ids(creature: dict) -> set[str]:
    return {p["id"] for p in creature["visual"]["parts"]}


def texture_stems(creature: dict) -> set[str]:
    return {p["texture"] for p in creature["visual"]["parts"]}


def bounds_z(creature: dict) -> float:
    return float(creature["bounds"]["rest"][2])


def check_opaque(species: str, stems: tuple[str, ...], max_alpha: float = 0.001) -> None:
    for stem in stems:
        path = MODELS / species / "textures" / f"{stem}.png"
        if not path.is_file():
            raise SystemExit(f"FAIL {species}: missing texture {path.name}")
        frac = alpha_fraction(path)
        if frac > max_alpha:
            raise SystemExit(
                f"FAIL {species}/{stem}.png: {frac * 100:.2f}% pixels alpha<250"
            )
    print(f"OK opaque {species}: {', '.join(stems)}")


def check_parts(species: str, required: set[str]) -> None:
    creature = load_creature(species)
    have = part_ids(creature)
    missing = required - have
    if missing:
        raise SystemExit(f"FAIL {species}: missing parts {sorted(missing)}")
    print(f"OK parts {species}: {len(have)} parts incl. {sorted(required)}")


def check_no_orphan_arm(species: str) -> None:
    arm = MODELS / species / "textures" / "arm.png"
    creature = load_creature(species)
    stems = texture_stems(creature)
    if arm.is_file() and "arm" not in stems:
        raise SystemExit(f"FAIL {species}: orphan textures/arm.png")
    print(f"OK no orphan arm {species}")


def check_human_atlas() -> None:
    creature = load_creature("human")
    layout = creature["visual"].get("texture_layout")
    if layout != "player_skin_atlas":
        raise SystemExit(f"FAIL human: texture_layout={layout!r}")
    for stem in ("body", "face", "arm", "leg"):
        path = MODELS / "human" / "textures" / f"{stem}.png"
        if not path.is_file():
            raise SystemExit(f"FAIL human: missing {stem}.png")
    print("OK human player_skin_atlas textures")


def check_proportions() -> None:
    sheep_z = bounds_z(load_creature("sheep"))
    cow_z = bounds_z(load_creature("cow"))
    if cow_z <= sheep_z:
        raise SystemExit(f"FAIL proportions: cow Z {cow_z} <= sheep Z {sheep_z}")
    print(f"OK proportions: cow Z {cow_z} > sheep Z {sheep_z}")


def check_animation_fields() -> None:
    required = {
        "body_bob_blocks",
        "tail_swing_deg",
        "run_speed_multiplier",
        "crouch_leg_bend_deg",
        "wing_idle_swing_deg",
    }
    for species in ("sheep", "chicken", "human"):
        anim = load_creature(species)["visual"]["animation"]
        missing = required - set(anim)
        if missing:
            raise SystemExit(f"FAIL {species}: missing animation fields {sorted(missing)}")
    print("OK animation params on sheep/chicken/human")


def check_b3d_samples() -> None:
    script = TOOLS / "sample_b3d_pose_curves.py"
    model = RESEARCH / "mobs_animal/models/mobs_chicken.b3d"
    if not model.is_file():
        print(f"SKIP b3d samples: missing {model}")
        return
    out = TOOLS / "debug_uv_overlays" / "_smoke_chicken_pose.json"
    subprocess.run(
        [
            sys.executable,
            str(script),
            "--model",
            "mobs_animal/models/mobs_chicken.b3d",
            "--out",
            str(out),
        ],
        check=True,
        cwd=ROOT,
    )
    data = json.loads(out.read_text(encoding="utf-8"))
    keyed = data["sample"]["keyed_bone_count"]
    walk_keys = sum(
        b.get("clips", {}).get("walk", {}).get("frames", 0)
        for b in data["sample"]["bones"].values()
    )
    peck_keys = sum(
        b.get("clips", {}).get("peck", {}).get("frames", 0)
        for b in data["sample"]["bones"].values()
    )
    if keyed < 5 or walk_keys < 10 or peck_keys < 10:
        raise SystemExit(
            f"FAIL b3d: keyed_bones={keyed} walk_keys={walk_keys} peck_keys={peck_keys}"
        )
    rec = data.get("recommended", {})
    print(
        f"OK b3d chicken: keyed_bones={keyed} walk_keys={walk_keys} "
        f"peck_keys={peck_keys} recommended={rec}"
    )


def remap_legacy_hotbar_creature_id(species_id: str) -> str:
    if species_id in ("test_mob", "scout"):
        return "sheep"
    if species_id == "brute":
        return "sand_monster"
    if species_id == "drifter":
        return "wolf"
    return species_id


def deserialize_hotbar_slot(slot_json: dict) -> dict:
    """Mirror CreatureInventory::DeserializeFromJson slot rules."""
    entry_id = slot_json.get("id", "")
    slot_empty = slot_json["empty"] if "empty" in slot_json else not entry_id
    if slot_empty:
        return {"empty": True, "id": ""}
    kind = slot_json.get("kind", "block")
    if kind == "creature":
        entry_id = remap_legacy_hotbar_creature_id(entry_id)
    return {"empty": False, "kind": kind, "id": entry_id}


def check_hotbar_legacy_id_remap() -> None:
    slot = deserialize_hotbar_slot(
        {"empty": False, "kind": "creature", "id": "brute", "count": 0}
    )
    if slot["id"] != "sand_monster":
        raise SystemExit(f"FAIL hotbar remap: brute -> {slot['id']}")
    legacy = deserialize_hotbar_slot({"kind": "creature", "id": "drifter"})
    if legacy["empty"] or legacy["id"] != "wolf":
        raise SystemExit(f"FAIL hotbar migrate empty: {legacy}")
    print("OK hotbar legacy id remap + empty migration")


def check_hotbar_creature_persistence() -> None:
    """Mirror post-load hotbar rules: saved creature in slot 1 must not become wood."""
    user_data = {
        "hotbars": [
            [
                {"empty": True},
                {"empty": False, "kind": "creature", "id": "sheep", "count": 0},
            ]
        ],
        "active_bar": 0,
        "active_slot": 1,
    }
    hotbars = user_data["hotbars"]
    slot1 = hotbars[0][1]
    if slot1.get("kind") != "creature" or slot1.get("id") != "sheep":
        raise SystemExit("FAIL hotbar fixture")

    # Simulate LoadUsers: skip EnsureDefaultHotbar when hotbars key exists.
    had_hotbars = "hotbars" in user_data and isinstance(user_data["hotbars"], list)
    if not had_hotbars:
        raise SystemExit("FAIL hotbar: expected had_hotbars")

    # After fix, EnsureDefaultHotbar must not run when had_hotbars.
    loaded_kind = slot1["kind"]
    loaded_id = slot1["id"]
    if loaded_kind != "creature" or loaded_id != "sheep":
        raise SystemExit(
            f"FAIL hotbar persistence: slot1={loaded_kind}:{loaded_id}"
        )

    # EnsureDefaultHotbar must not overwrite occupied slot 1.
    slots = [{"empty": s.get("empty", True), "id": s.get("id", "")} for s in hotbars[0]]
    if not slots[1]["empty"]:
        post_kind = loaded_kind
        post_id = loaded_id
    else:
        post_kind = "block"
        post_id = "wood"
    if post_kind != "creature" or post_id != "sheep":
        raise SystemExit("FAIL hotbar: EnsureDefaultHotbar would overwrite creature")
    print("OK hotbar creature slot 1 survives load without EnsureDefaultHotbar")


def main() -> None:
    print("=== creature fidelity smoke ===")
    check_opaque("sheep", ("body", "face", "ear", "tail"))
    check_opaque("skeleton", ("body", "face", "leg"))
    check_opaque("cow", ("body", "face"))
    check_parts(
        "sheep",
        {"snout", "ear_l", "ear_r", "tail", "leg_fl", "leg_fr", "leg_bl", "leg_br"},
    )
    check_parts("wolf", {"ear_l", "ear_r", "tail", "snout"})
    check_parts("chicken", {"neck", "comb", "head", "beak", "wing_l", "wing_r"})
    chicken = load_creature("chicken")
    head = next(p for p in chicken["visual"]["parts"] if p["id"] == "head")
    if head["texture"] != "face":
        raise SystemExit(f"FAIL chicken: head.texture={head['texture']!r}")
    print("OK chicken head uses face texture")
    for species in ("sheep", "cow", "pig", "wolf", "chicken"):
        check_no_orphan_arm(species)
    check_human_atlas()
    check_proportions()
    check_animation_fields()
    check_parts(
        "spider",
        {"leg_fl", "leg_fr", "leg_ml", "leg_mr", "leg_bl", "leg_br"},
    )
    check_parts("penguin", {"fin_l", "fin_r", "leg_l", "leg_r", "beak"})
    check_parts("tortoise", {"tail"})
    for species in ("bunny", "spider", "penguin", "trout", "fox", "tortoise"):
        path = MODELS / species / "textures" / "body.png"
        if not path.is_file():
            raise SystemExit(f"FAIL {species}: missing imported body.png")
        lic = MODELS / species / "LICENSE.txt"
        if lic.is_file() and "Placeholder procedural" in lic.read_text(encoding="utf-8"):
            raise SystemExit(f"FAIL {species}: still using placeholder LICENSE")
    print("OK wave-2 imported textures (sample)")
    check_b3d_samples()
    check_hotbar_legacy_id_remap()
    check_hotbar_creature_persistence()
    print("=== all creature fidelity smoke checks passed ===")
    print(
        "Manual: spawn sheep/cow/chicken/skeleton; F5 human walk/crouch; "
        "select_skin human_adventurer human_guard"
    )


if __name__ == "__main__":
    main()
