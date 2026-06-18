#!/usr/bin/env python3
"""Copy a tier A block JSON + texture stems from a donor pack into a target pack."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PACKS = REPO / "resource_packs"


def restore_block(donor: Path, target: Path, block_name: str) -> bool:
    src_json = donor / "blocks" / f"{block_name}.json"
    dst_json = target / "blocks" / f"{block_name}.json"
    if not src_json.is_file():
        print(f"  missing donor block: {block_name}")
        return False
    block = json.loads(src_json.read_text(encoding="utf-8-sig"))
    dst_json.parent.mkdir(parents=True, exist_ok=True)
    dst_json.write_text(json.dumps(block, indent=2) + "\n", encoding="utf-8")
    stems = block.get("textures", [])
    if isinstance(stems, list):
        for stem in set(s for s in stems if isinstance(s, str)):
            src_png = donor / "textures" / "blocks" / f"{stem}.png"
            dst_png = target / "textures" / "blocks" / f"{stem}.png"
            if src_png.is_file():
                dst_png.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src_png, dst_png)
    print(f"  restored {block_name}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", required=True, help="Target pack id")
    parser.add_argument("--donor", default="kenney_voxel_16")
    parser.add_argument("--blocks", nargs="+", required=True)
    args = parser.parse_args()
    donor = PACKS / args.donor
    target = PACKS / args.target
    if not donor.is_dir() or not target.is_dir():
        print("ERROR: donor or target pack not found")
        return 1
    ok = 0
    for name in args.blocks:
        if restore_block(donor, target, name):
            ok += 1
    print(f"Restored {ok}/{len(args.blocks)} blocks into {args.target}")
    return 0 if ok == len(args.blocks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
