#!/usr/bin/env python3
"""Bake ordered primary+secondary resource packs into a single merged pack."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def load_pack_manifest(pack_dir: Path) -> dict:
    return json.loads((pack_dir / "pack.json").read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--primary", nargs="+", required=True)
    parser.add_argument("--secondary", nargs="*", default=[])
    parser.add_argument("--output", required=True, help="Output pack directory")
    parser.add_argument("--pack-id", default="merged_pack")
    args = parser.parse_args()

    out = Path(args.output)
    if out.exists():
        shutil.rmtree(out)
    (out / "blocks").mkdir(parents=True)
    (out / "textures" / "blocks").mkdir(parents=True)

    merged_blocks: dict[str, dict] = {}
    for tier_ids in (args.primary, args.secondary):
        for pack_id in tier_ids:
            src = REPO / "resource_packs" / pack_id
            if not src.is_dir():
                print(f"skip missing pack: {pack_id}")
                continue
            for block_file in (src / "blocks").glob("*.json"):
                merged_blocks[block_file.stem] = json.loads(
                    block_file.read_text(encoding="utf-8")
                )
            tex_dst = out / "textures" / "blocks"
            for png in (src / "textures" / "blocks").glob("*.png"):
                shutil.copy2(png, tex_dst / png.name)

    for name, data in merged_blocks.items():
        (out / "blocks" / f"{name}.json").write_text(
            json.dumps(data, indent=2), encoding="utf-8"
        )

    manifest = {
        "id": args.pack_id,
        "name": f"Baked merge ({args.pack_id})",
        "pack_format": 1,
        "worldgen_role": "primary",
        "merge_mode": "skip_existing",
        "priority": 0,
        "resolution": 16,
        "license": "composite",
    }
    (out / "pack.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"baked {len(merged_blocks)} blocks -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
