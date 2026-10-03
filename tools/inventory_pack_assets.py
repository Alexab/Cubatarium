#!/usr/bin/env python3
"""Scan an extracted CC0 pack for tool/weapon/armor glTF assets.

Usage:
  python tools/inventory_pack_assets.py --pack-root third_party/asset_cache/kenney_survival_kit --source-id kenney_survival_kit
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT_ITEMS = ROOT / "content" / "items"

TOOL_WEAPON = (
    "pickaxe",
    "axe",
    "shovel",
    "sword",
    "spear",
    "knife",
    "dagger",
    "hammer",
    "mallet",
    "bow",
    "crossbow",
    "mace",
    "club",
    "staff",
    "wand",
    "scythe",
    "hoe",
    "fishing",
    "lantern",
    "torch",
    "shield",
    "wrench",
    "saw",
    "chisel",
    "anvil",
    "tongs",
    "scissors",
    "lockpick",
    "blueprint",
    "arrow",
)
ARMOR_KW = (
    "helmet",
    "helm",
    "armor",
    "chest",
    "plate",
    "boot",
    "shoe",
    "glove",
    "gauntlet",
    "leg",
    "pant",
    "bracer",
    "shoulder",
    "pauldron",
)
EXCLUDE = (
    "table",
    "chair",
    "crate",
    "barrel",
    "food",
    "bread",
    "apple",
    "tree",
    "rock",
    "wall",
    "floor",
    "fence",
    "door",
    "window",
    "house",
    "plant",
    "flower",
    "coin",
    "gem",
    "potion",
    "bottle",
    "chest_closed",
    "chest_open",
    "campfire",
    "workbench",
    "tent",
    "bridge",
    "path",
    "grass",
    "dirt",
)


def slugify(stem: str) -> str:
    s = stem.replace("-", "_").replace(" ", "_")
    s = re.sub(r"[^a-zA-Z0-9_]+", "_", s)
    s = re.sub(r"_+", "_", s).strip("_").lower()
    return s or "item"


def classify(stem: str) -> str:
    low = stem.lower()
    for ex in EXCLUDE:
        if ex in low:
            return "skip"
    # shield is combat utility, not armor.slot
    if "shield" in low:
        return "utility"
    for kw in ARMOR_KW:
        if kw in low:
            return "armor"
    weaponish = (
        "sword",
        "spear",
        "bow",
        "dagger",
        "knife",
        "mace",
        "club",
        "staff",
        "wand",
        "crossbow",
        "arrow",
    )
    for kw in weaponish:
        if kw in low:
            return "weapon"
    for kw in TOOL_WEAPON:
        if kw in low:
            return "tool"
    return "skip"


def existing_item_ids() -> set[str]:
    if not CONTENT_ITEMS.is_dir():
        return set()
    return {p.stem for p in CONTENT_ITEMS.glob("*.json")}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pack-root", type=Path, required=True)
    ap.add_argument("--source-id", required=True)
    ap.add_argument(
        "--out",
        type=Path,
        default=None,
        help="Default: tools/_pack_inventory_<source-id>.json",
    )
    args = ap.parse_args()
    root: Path = args.pack_root
    if not root.is_dir():
        raise SystemExit(f"pack root not found: {root}")

    known = existing_item_ids()
    files = []
    for path in sorted(root.rglob("*")):
        if path.suffix.lower() not in {".gltf", ".glb"}:
            continue
        rel = path.relative_to(root).as_posix()
        stem = path.stem
        cls = classify(stem)
        suggested = slugify(stem)
        entry = {
            "rel": rel,
            "stem": stem,
            "class": cls,
            "suggested_id": suggested,
            "maps_to_existing": suggested in known,
        }
        if suggested in known:
            entry["existing_id"] = suggested
        files.append(entry)

    out = args.out or (ROOT / "tools" / f"_pack_inventory_{args.source_id}.json")
    payload = {"source": args.source_id, "pack_root": str(root), "files": files}
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    counts: dict[str, int] = {}
    for f in files:
        counts[f["class"]] = counts.get(f["class"], 0) + 1
    print(f"Wrote {out} ({len(files)} glTF/glb)")
    for k, v in sorted(counts.items()):
        print(f"  {k}: {v}")


if __name__ == "__main__":
    main()
