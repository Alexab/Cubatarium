#!/usr/bin/env python3
"""One-time repo conversion: prefabs -> objects, merge mapgen, tags, object_features."""
from __future__ import annotations

import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PREFABS = ROOT / "prefabs"
OBJECTS = ROOT / "objects"

MAPGEN_TO_BASE = {
    "tree_acacia_mapgen": "tree_acacia",
    "tree_apple_mapgen": "tree_apple",
    "tree_aspen_mapgen": "tree_aspen",
    "tree_jungle_emergent_mapgen": "tree_jungle_emergent",
    "tree_jungle_mapgen": "tree_jungle",
    "tree_pine_mapgen": "tree_pine",
    "tree_pine_small_mapgen": "tree_pine_small",
    "tree_pine_snowy_mapgen": "tree_pine_snowy",
    "tree_pine_snowy_small_mapgen": "tree_pine_snowy_small",
}


def infer_tags(name: str, category: str) -> list[str]:
    tags: list[str] = []
    if category and category != "misc":
        tags.append(category)
    n = name.lower()
    if n.startswith("tree_"):
        if "trees" not in tags:
            tags.append("trees")
    elif n.startswith("bush_"):
        if "bushes" not in tags:
            tags.append("bushes")
    elif n.startswith("ruin_"):
        if "ruins" not in tags:
            tags.append("ruins")
    elif n.startswith("house_") or n.startswith("shed_") or n.startswith("tower_"):
        if "structures" not in tags:
            tags.append("structures")
    elif n.startswith("path_") or n.startswith("deco_"):
        if "paths" not in tags:
            tags.append("paths")
    elif n.startswith("cactus"):
        if "plants" not in tags:
            tags.append("plants")
    if not tags:
        tags.append("misc")
    # dedupe preserve order
    seen: set[str] = set()
    out: list[str] = []
    for t in tags:
        if t not in seen:
            seen.add(t)
            out.append(t)
    return out


def convert_object_json(path: Path) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    name = data.get("name", path.stem)
    category = data.get("category", "misc")
    if "tags" not in data:
        data["tags"] = infer_tags(name, category)
    if "category" in data:
        del data["category"]
    if "displayName" in data:
        dn = data["displayName"]
        if "(mapgen)" in dn.lower():
            data["displayName"] = re.sub(r"\s*\(mapgen\)\s*", "", dn, flags=re.I)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def move_prefabs_to_objects() -> None:
    if OBJECTS.exists():
        shutil.rmtree(OBJECTS)
    if PREFABS.exists():
        shutil.move(str(PREFABS), str(OBJECTS))
    else:
        OBJECTS.mkdir(parents=True, exist_ok=True)


def remove_mapgen_duplicates() -> None:
    for mapgen_name in MAPGEN_TO_BASE:
        p = OBJECTS / f"{mapgen_name}.json"
        if p.exists():
            p.unlink()


def convert_all_objects() -> None:
    for p in OBJECTS.rglob("*.json"):
        if p.stem.endswith("_mapgen"):
            continue
        convert_object_json(p)


def convert_features(src: Path, dst: Path) -> None:
    data = json.loads(src.read_text(encoding="utf-8"))
    for pool in ("vegetation", "ground_cover", "decoration", "structures"):
        if pool not in data:
            continue
        for rule in data[pool]:
            if "prefab" in rule:
                obj = rule.pop("prefab")
                if obj in MAPGEN_TO_BASE:
                    obj = MAPGEN_TO_BASE[obj]
                rule["object"] = obj
    dst.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> None:
    move_prefabs_to_objects()
    remove_mapgen_duplicates()
    convert_all_objects()
    src_feat = ROOT / "content" / "prefab_features.json"
    dst_feat = ROOT / "content" / "object_features.json"
    if src_feat.exists():
        convert_features(src_feat, dst_feat)
        src_feat.unlink()
    manifest_src = ROOT / "tools" / "prefab_manifest.yaml"
    manifest_dst = ROOT / "tools" / "object_manifest.yaml"
    if manifest_src.exists():
        text = manifest_src.read_text(encoding="utf-8")
        for m, b in MAPGEN_TO_BASE.items():
            text = text.replace(m, b)
        manifest_dst.write_text(text, encoding="utf-8")
        manifest_src.unlink()
    print("Conversion done:", OBJECTS, dst_feat)


if __name__ == "__main__":
    main()
