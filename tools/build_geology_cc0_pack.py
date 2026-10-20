#!/usr/bin/env python3
"""Build the self-contained CC0 geology pack from Kenney Voxel Pack tiles."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = Path(
    r"E:/Work/Home/CubatariumTextureResearch/kenney_voxel_pack/PNG/Tiles"
)
PACK_ID = "cubatarium_geology_cc0"
PACK_OUT = REPO / "resource_packs" / PACK_ID


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    args = parser.parse_args()
    source = args.source.resolve()
    if not source.is_dir():
        raise SystemExit(
            f"Kenney Voxel Pack tiles were not found at {source}; "
            "download the CC0 source and pass its PNG/Tiles directory"
        )

    sys.path.insert(0, str(REPO / "tools"))
    from build_resource_pack import build_pack
    from validate_resource_pack import validate_pack

    mapping_path = REPO / "tools" / "geology_cc0_mapping.yaml"
    mapping = yaml.safe_load(mapping_path.read_text(encoding="utf-8")) or {}
    build_pack(mapping, source, PACK_OUT, PACK_ID, priority=10, resolution=16)

    pack_json = PACK_OUT / "pack.json"
    manifest = json.loads(pack_json.read_text(encoding="utf-8"))
    manifest.update(
        {
            "name": "Cubatarium Geology CC0",
            "version": 1,
            "pack_format": 1,
            "worldgen_role": "secondary",
            "merge_mode": "skip_existing",
            "depends": [],
            "conflicts": [],
            "min_game_version": "0.1.0",
        }
    )
    pack_json.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    errors = validate_pack(PACK_OUT)
    if errors:
        return errors
    print(f"Built {PACK_OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
