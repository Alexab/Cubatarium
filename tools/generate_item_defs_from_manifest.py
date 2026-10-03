#!/usr/bin/env python3
"""Generate content/items stubs (+ optional parts_v1) from item_model_manifest.json.

Usage:
  python tools/generate_item_defs_from_manifest.py
  python tools/generate_item_defs_from_manifest.py --dry-run
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tools" / "item_model_manifest.json"
CONTENT = ROOT / "content" / "items"
MODELS = ROOT / "models" / "items"

MATERIAL_TIER = {
    "wood": 0,
    "wooden": 0,
    "leather": 0,
    "stone": 1,
    "copper": 1,
    "iron": 2,
    "steel": 2,
    "gold": 1,
    "silver": 1,
}


def detect_material(item_id: str) -> str:
    for m in ("leather", "iron", "stone", "copper", "gold", "silver", "steel", "wood", "wooden"):
        if item_id.startswith(m + "_") or f"_{m}_" in item_id or item_id.endswith("_" + m):
            return "wood" if m == "wooden" else m
    return "iron"


def titleize(item_id: str) -> str:
    return " ".join(p.capitalize() for p in item_id.split("_"))


def groupcaps_for(kind: str, tier: int) -> dict:
    uses = [40, 80, 120][min(tier, 2)]
    times = {
        0: {"1": 2.0, "2": 1.4, "3": 1.0},
        1: {"1": 1.6, "2": 1.0, "3": 0.7},
        2: {"1": 1.4, "2": 0.8, "3": 0.5},
    }[min(tier, 2)]
    maxlevel = min(tier + 1, 3)
    return {kind: {"maxlevel": maxlevel, "uses": uses, "times": times}}


def damage_for(tier: int) -> dict:
    d = [2, 3, 5][min(tier, 2)]
    return {"melee": d, "fleshy": d}


def armor_groups_for(mat: str, slot: str) -> dict:
    base = {"leather": 40, "iron": 100, "gold": 60, "copper": 70, "steel": 110}.get(mat, 50)
    mult = {"head": 0.8, "chest": 1.0, "arms": 0.6, "hands": 0.4, "legs": 0.7, "feet": 0.5}.get(
        slot, 0.7
    )
    return {"fleshy": int(base * mult)}


def repair_for(mat: str) -> dict:
    materials = {
        "wood": ["oak_log", "wood"],
        "stone": ["cobble", "stone"],
        "copper": ["copper_ingot", "copper"],
        "iron": ["iron_ingot", "iron"],
        "gold": ["gold_ingot", "gold"],
        "leather": ["leather"],
        "steel": ["iron_ingot", "iron"],
        "silver": ["iron_ingot"],
    }.get(mat, ["iron_ingot"])
    return {"materials": materials, "amount": 0.25}


def classify_id(item_id: str, forced: str | None) -> tuple[str, dict]:
    """Return (class_key, def_body_fragment without id/model)."""
    low = item_id.lower()
    mat = detect_material(low)
    tier = MATERIAL_TIER.get(mat, 1)

    if forced == "armor" or any(
        low.endswith("_" + s) or f"_{s}" in low
        for s in ("head", "chest", "arms", "hands", "legs", "feet")
    ) and ("leather" in low or "iron" in low or "armor" in low or "helmet" in low or "boot" in low):
        slot = None
        for s in ("head", "chest", "arms", "hands", "legs", "feet"):
            if low.endswith("_" + s) or low.endswith(s):
                slot = s
                break
        if "helmet" in low or "helm" in low:
            slot = "head"
        if "boot" in low or "shoe" in low:
            slot = "feet"
        if "glove" in low or "gauntlet" in low:
            slot = "hands"
        if "chest" in low or "plate" in low:
            slot = slot or "chest"
        if "leg" in low or "pant" in low:
            slot = "legs"
        if "bracer" in low or "shoulder" in low or "arm" in low:
            slot = slot or "arms"
        slot = slot or "chest"
        return "armor", {
            "types": ["armor", slot],
            "stack_max": 1,
            "wear_end": "destroy",
            "armor": {"slots": [slot], "armor_groups": armor_groups_for(mat, slot)},
        }

    if "pickaxe" in low:
        return "tool", {
            "types": ["mining"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 1.0,
                "damage": damage_for(tier),
                "groupcaps": groupcaps_for("cracky", tier),
                "punch_attack_uses": [40, 80, 120][min(tier, 2)],
            },
        }
    if "axe" in low and "pickaxe" not in low:
        return "tool", {
            "types": ["cutting"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 1.0,
                "damage": damage_for(tier),
                "groupcaps": groupcaps_for("choppy", tier),
                "punch_attack_uses": [40, 80, 120][min(tier, 2)],
            },
        }
    if "shovel" in low:
        return "tool", {
            "types": ["digging"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 1.0,
                "damage": damage_for(max(tier - 1, 0)),
                "groupcaps": groupcaps_for("crumbly", tier),
                "punch_attack_uses": [40, 80, 120][min(tier, 2)],
            },
        }
    if any(k in low for k in ("sword", "spear", "knife", "dagger", "mace", "club")):
        return "weapon", {
            "types": ["combat"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 0.9 if "knife" in low else 1.0,
                "damage": damage_for(tier if "sword" in low or "spear" in low else max(tier, 1)),
                "groupcaps": groupcaps_for("snappy", max(tier - 1, 0)) if "knife" in low else {},
                "punch_attack_uses": [50, 90, 140][min(tier, 2)],
            },
        }
    if "bow" in low or "crossbow" in low:
        return "weapon", {
            "types": ["combat"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 1.2,
                "damage": {"melee": 1, "fleshy": 1},
                "punch_attack_uses": 80,
            },
        }
    if "hammer" in low or "mallet" in low:
        return "tool", {
            "types": ["utility", "mining"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 1.1,
                "damage": damage_for(tier),
                "groupcaps": groupcaps_for("cracky", max(tier - 1, 0)),
                "punch_attack_uses": 100,
            },
        }
    if "shield" in low:
        return "utility", {
            "types": ["combat", "utility"],
            "stack_max": 1,
            "wear_end": "destroy",
            "repair": repair_for(mat),
            "tool": {
                "full_punch_interval": 1.5,
                "damage": {"melee": 0, "fleshy": 0},
                "punch_attack_uses": 200,
            },
        }
    # utility default
    return "utility", {
        "types": ["utility"],
        "stack_max": 1,
        "wear_end": "destroy",
        "repair": repair_for(mat),
        "tool": {
            "full_punch_interval": 1.0,
            "damage": {"melee": 1, "fleshy": 1},
            "punch_attack_uses": 60,
        },
    }


def default_parts(item_id: str, cls: str) -> list[dict]:
    """Minimal parts_v1 for icons/FP fallback."""
    if cls == "armor":
        if item_id.endswith("_head") or "helmet" in item_id:
            return [{"texture": "white", "offset": [0, 0.12, 0], "size": [0.28, 0.22, 0.28]}]
        if item_id.endswith("_chest"):
            return [{"texture": "white", "offset": [0, 0.05, 0], "size": [0.36, 0.4, 0.2]}]
        return [{"texture": "white", "offset": [0, 0, 0], "size": [0.2, 0.2, 0.2]}]
    if "sword" in item_id or "spear" in item_id:
        return [
            {"texture": "white", "offset": [0, 0.25, 0], "size": [0.06, 0.5, 0.06]},
            {"texture": "white", "offset": [0, 0.02, 0], "size": [0.14, 0.06, 0.06]},
        ]
    if "bow" in item_id:
        return [{"texture": "white", "offset": [0, 0.15, 0], "size": [0.08, 0.4, 0.08]}]
    if "shield" in item_id:
        return [{"texture": "white", "offset": [0, 0.1, 0], "size": [0.28, 0.35, 0.06]}]
    # tool default rod + head
    return [
        {"texture": "white", "offset": [0, 0.18, 0], "size": [0.06, 0.36, 0.06]},
        {"texture": "white", "offset": [0, 0.4, 0], "size": [0.18, 0.1, 0.08]},
    ]


def write_wear_sidecar(item_id: str, slots: list[str]) -> None:
    if not slots:
        return
    slot = slots[0]
    bone_map = {
        "head": ["hat"],
        "chest": ["body"],
        "arms": ["leftArm", "rightArm"],
        "hands": ["leftItem", "rightItem"],
        "legs": ["leftLeg", "rightLeg"],
        "feet": ["leftLeg", "rightLeg"],
    }
    wear = {
        "bones": bone_map.get(slot, ["body"]),
        "offset": [0, -0.15, 0] if slot == "feet" else [0, 0, 0],
        "euler_deg": [0, 0, 0],
        "scale": 1.0,
    }
    dest = MODELS / item_id
    dest.mkdir(parents=True, exist_ok=True)
    (dest / "wear.json").write_text(json.dumps(wear, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--force", action="store_true", help="Overwrite existing content defs")
    args = ap.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    created = 0
    for item_id, spec in sorted(manifest.get("items", {}).items()):
        if spec.get("role") != "new":
            continue
        content_path = CONTENT / f"{item_id}.json"
        if content_path.exists() and not args.force:
            print(f"EXISTS {item_id}")
            continue
        forced = spec.get("class")
        cls, body = classify_id(item_id, forced)
        doc = {
            "id": item_id,
            "displayName": titleize(item_id),
            **body,
            "model": f"models/items/{item_id}/model.gltf",
        }
        # strip empty groupcaps
        if "tool" in doc and not doc["tool"].get("groupcaps"):
            doc["tool"].pop("groupcaps", None)
        parts_doc = {
            "id": item_id,
            "format": "parts_v1",
            "license": "CC0-1.0",
            "attribution": "Cubatarium educational",
            "parts": default_parts(item_id, cls),
        }
        print(f"{'DRY ' if args.dry_run else ''}CREATE {item_id} class={cls}")
        if not args.dry_run:
            content_path.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
            parts_path = MODELS / f"{item_id}.json"
            if not parts_path.exists() or args.force:
                parts_path.write_text(json.dumps(parts_doc, indent=2) + "\n", encoding="utf-8")
            if cls == "armor" and "armor" in doc:
                write_wear_sidecar(item_id, doc["armor"].get("slots", []))
        created += 1
    print(f"Done. created={created}")


if __name__ == "__main__":
    main()
