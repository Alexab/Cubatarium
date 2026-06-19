#!/usr/bin/env python3
"""Patch block JSON in resource packs using canonical_blocks.yaml metadata."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from stem_mapping_common import apply_canonical_meta_to_block_json, load_canonical_block_specs

REPO = Path(__file__).resolve().parents[1]
DEFAULT_PACKS = REPO / "resource_packs"


def patch_pack(pack_dir: Path, dry_run: bool) -> int:
    blocks_dir = pack_dir / "blocks"
    if not blocks_dir.is_dir():
        return 0
    changed = 0
    for block_path in sorted(blocks_dir.glob("*.json")):
        try:
            block = json.loads(block_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        name = block.get("name")
        if not name:
            continue
        before = json.dumps(block, sort_keys=True)
        updated = apply_canonical_meta_to_block_json(block)
        after = json.dumps(updated, sort_keys=True)
        if before != after:
            changed += 1
            if not dry_run:
                block_path.write_text(json.dumps(updated, indent=2) + "\n", encoding="utf-8")
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply canonical types/physics to pack block JSON")
    parser.add_argument("--packs-dir", type=Path, default=DEFAULT_PACKS)
    parser.add_argument("--pack", type=str, default=None, help="Single pack id folder name")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    packs_dir = args.packs_dir.resolve()
    targets: list[Path]
    if args.pack:
        targets = [packs_dir / args.pack]
    else:
        targets = [p for p in sorted(packs_dir.iterdir()) if p.is_dir() and not p.name.startswith("_")]

    total = 0
    for pack_dir in targets:
        n = patch_pack(pack_dir, args.dry_run)
        if n:
            print(f"{pack_dir.name}: updated {n} blocks")
            total += n
    print(f"Done — {total} block files {'would change' if args.dry_run else 'updated'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
