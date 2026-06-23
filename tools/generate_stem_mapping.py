#!/usr/bin/env python3
"""Generate stem mapping YAML from a texture source + Cubatarium block manifests."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")

sys.path.insert(0, str(REPO / "tools"))
from analyze_texture_packs import index_pack  # noqa: E402
from stem_mapping_common import (  # noqa: E402
    block_spec,
    face_stems,
    load_manifest_blocks,
    resolve_stem_ref,
    texture_ref_exists,
)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate resource-pack mapping YAML")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--license", default="CC0-1.0")
    parser.add_argument("--license-text", default="")
    parser.add_argument("--flat", action="store_true", help="Source is flat PNG folder")
    args = parser.parse_args()

    source: Path = args.source
    if not source.is_dir():
        raise SystemExit(f"Missing source: {source}")

    tex_root = source
    index = index_pack(tex_root)
    stem_map: dict[str, Any] = {}

    blocks_out: dict[str, Any] = {}
    textures_out: dict[str, Any] = {}
    skipped: list[str] = []

    for entry in load_manifest_blocks():
        name = entry["name"]
        stems = face_stems(entry)
        resolved = {
            s: resolve_stem_ref(s, index, stem_map, tex_root) for s in set(stems)
        }
        if any(v is None for v in resolved.values()):
            skipped.append(name)
            continue
        for stem, ref in resolved.items():
            if ref is not None and not texture_ref_exists(tex_root, ref):
                resolved[stem] = None
        if any(v is None for v in resolved.values()):
            skipped.append(name)
            continue
        blocks_out[name] = block_spec(entry)
        for stem, ref in resolved.items():
            if stem not in textures_out and ref is not None:
                if isinstance(ref, str) and not args.flat:
                    textures_out[stem] = Path(ref).name
                else:
                    textures_out[stem] = ref

    out = {
        "license": args.license,
        "license_text": args.license_text or f"Source textures from {source}\n",
        "blocks": blocks_out,
        "textures": textures_out,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        yaml.dump(out, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(
        f"Wrote {args.out}: {len(blocks_out)} blocks, {len(textures_out)} textures, "
        f"skipped {len(skipped)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
