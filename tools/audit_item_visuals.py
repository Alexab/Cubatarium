#!/usr/bin/env python3
"""Audit content/items visuals: model/materials/textures + critical flag."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "content" / "items"
MODELS = ROOT / "models" / "items"
OUT_DIR = ROOT / "bin" / "iter_reports"

# Critical for gameplay: dig caps, armor, eat/drink, combat progression, hand.
CRITICAL_IDS = {
    "apple",
    "arrow",
    "bread",
    "copper_axe",
    "copper_chest",
    "copper_feet",
    "copper_head",
    "copper_hoe",
    "copper_legs",
    "copper_pickaxe",
    "copper_shovel",
    "copper_sword",
    "gold_axe",
    "gold_chest",
    "gold_feet",
    "gold_head",
    "gold_hoe",
    "gold_legs",
    "gold_pickaxe",
    "gold_shovel",
    "gold_sword",
    "hand",
    "iron_axe",
    "iron_chest",
    "iron_feet",
    "iron_head",
    "iron_hoe",
    "iron_legs",
    "iron_pickaxe",
    "iron_shield",
    "iron_shovel",
    "iron_sword",
    "leather",
    "leather_chest",
    "leather_feet",
    "leather_head",
    "leather_legs",
    "raw_meat",
    "stone_axe",
    "stone_hoe",
    "stone_pickaxe",
    "stone_shovel",
    "stone_sword",
    "waterskin",
    "wood_axe",
    "wood_bow",
    "wood_hoe",
    "wood_pickaxe",
    "wood_shield",
    "wood_shovel",
    "wood_sword",
    "wood_torch",
}

HIDEABLE_IDS = set()


def is_critical(data: dict, item_id: str) -> bool:
    if item_id in CRITICAL_IDS:
        return True
    if item_id in HIDEABLE_IDS:
        return False
    tool = data.get("tool") or {}
    if tool.get("groupcaps"):
        return True
    armor = data.get("armor") or {}
    if armor.get("slots"):
        return True
    use = data.get("use") or {}
    action = use.get("action") or data.get("use_action") or ""
    if action in ("eat", "drink"):
        return True
    return False


def inspect_gltf(gltf_path: Path) -> dict:
    info = {
        "gltf": str(gltf_path.relative_to(ROOT)).replace("\\", "/"),
        "has_materials": False,
        "has_images": False,
        "has_external_png": False,
        "has_embedded_image": False,
        "base_color_factor_only": False,
        "material_names": [],
    }
    try:
        gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        info["error"] = str(exc)
        return info

    mats = gltf.get("materials") or []
    images = gltf.get("images") or []
    info["has_materials"] = bool(mats)
    info["has_images"] = bool(images)
    info["material_names"] = [m.get("name") or "" for m in mats]

    factor_only = False
    for mat in mats:
        pbr = mat.get("pbrMetallicRoughness") or {}
        has_tex = "baseColorTexture" in pbr
        has_factor = "baseColorFactor" in pbr
        if has_factor and not has_tex and not images:
            factor_only = True
    info["base_color_factor_only"] = factor_only and not images

    for img in images:
        uri = img.get("uri") or ""
        if uri:
            tex = (gltf_path.parent / uri).resolve()
            if tex.is_file() and tex.suffix.lower() in {".png", ".jpg", ".jpeg"}:
                info["has_external_png"] = True
        if "bufferView" in img:
            info["has_embedded_image"] = True

    # also scan for png siblings
    if any(gltf_path.parent.rglob("*.png")):
        info["has_external_png"] = True or info["has_external_png"]

    return info


# Hard blockers for critical accept (missing model/materials). Soft notes remain
# in issues for armor factor/embedded (runtime cache handles them).
HARD_ISSUES = {"no_model", "gltf_no_materials"}
SOFT_ISSUES = {"factor_only", "embedded_image_only"}


def audit_item(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    item_id = data.get("id") or path.stem
    model = data.get("model") or ""
    parts = MODELS / f"{item_id}.json"
    gltf = MODELS / item_id / "model.gltf"
    glb = MODELS / item_id / "model.glb"

    entry = {
        "id": item_id,
        "critical": is_critical(data, item_id),
        "hideable": item_id in HIDEABLE_IDS,
        "hidden": bool(data.get("hidden")),
        "model_field": model,
        "has_parts_json": parts.is_file(),
        "has_gltf": gltf.is_file(),
        "has_glb": glb.is_file(),
        "gltf_info": None,
        "issues": [],
    }

    if not (model or parts.is_file() or gltf.is_file() or glb.is_file()):
        entry["issues"].append("no_model")
    if gltf.is_file():
        info = inspect_gltf(gltf)
        entry["gltf_info"] = info
        if not info.get("has_materials") and not info.get("has_images"):
            entry["issues"].append("gltf_no_materials")
        if info.get("has_embedded_image") and not info.get("has_external_png"):
            entry["issues"].append("embedded_image_only")
        if info.get("base_color_factor_only"):
            entry["issues"].append("factor_only")
    elif not parts.is_file() and not glb.is_file() and not model:
        pass

    return entry


def main() -> int:
    items = [audit_item(p) for p in sorted(CONTENT.glob("*.json"))]
    summary = {
        "count": len(items),
        "critical": sum(1 for i in items if i["critical"]),
        "with_issues": sum(1 for i in items if i["issues"]),
        "critical_with_issues": [
            {"id": i["id"], "issues": i["issues"]}
            for i in items
            if i["critical"] and i["issues"]
        ],
        "critical_hard_issues": [
            {
                "id": i["id"],
                "issues": [x for x in i["issues"] if x in HARD_ISSUES],
            }
            for i in items
            if i["critical"] and any(x in HARD_ISSUES for x in i["issues"])
        ],
        "hideable": [i["id"] for i in items if i["hideable"]],
        "items": items,
    }

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_path = OUT_DIR / "item_visual_audit.json"
    out_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    print(f"audit_item_visuals: {summary['count']} items, "
          f"{summary['critical']} critical, "
          f"{summary['with_issues']} with issues")
    if summary["critical_hard_issues"]:
        print("critical hard issues (missing model/materials):")
        for row in summary["critical_hard_issues"]:
            print(f"  {row['id']}: {', '.join(row['issues'])}")
    soft = [
        {"id": i["id"], "issues": [x for x in i["issues"] if x in SOFT_ISSUES]}
        for i in items
        if i["critical"] and any(x in SOFT_ISSUES for x in i["issues"])
    ]
    if soft:
        print(f"critical soft notes ({len(soft)}): factor_only/embedded ok via cache")
    print(f"wrote {out_path.relative_to(ROOT)}")
    return 1 if summary["critical_hard_issues"] else 0


if __name__ == "__main__":
    sys.exit(main())
