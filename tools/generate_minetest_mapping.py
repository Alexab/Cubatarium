#!/usr/bin/env python3
"""Generate tools/minetest_mapping.yaml from Minetest textures + block manifests."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
MT_TEX = RESEARCH / "minetest_default" / "textures"
STEM_MAP_PATH = REPO / "tools" / "minetest_stem_map.yaml"
UPSTREAM_PATH = REPO / "tools" / "minetest_upstream_blocks.yaml"

sys.path.insert(0, str(REPO / "tools"))
from analyze_texture_packs import index_pack  # noqa: E402
from stem_mapping_common import (  # noqa: E402
    MINETEST_STEM_MAP,
    block_spec,
    face_stems,
    load_manifest_blocks,
    load_stem_map,
    resolve_stem_ref,
    texture_ref_exists,
    write_minetest_stem_map_yaml,
)


def ensure_stem_map() -> dict[str, Any]:
    if not STEM_MAP_PATH.is_file():
        write_minetest_stem_map_yaml(STEM_MAP_PATH)
    stem_map = dict(MINETEST_STEM_MAP)
    stem_map.update(load_stem_map(STEM_MAP_PATH))
    return stem_map


def resolve_all_stems(
    stems: list[str],
    index: dict,
    stem_map: dict[str, Any],
) -> dict[str, Any | None]:
    out: dict[str, Any | None] = {}
    for stem in stems:
        ref = resolve_stem_ref(stem, index, stem_map, MT_TEX)
        if ref is not None and texture_ref_exists(MT_TEX, ref):
            out[stem] = ref
        else:
            out[stem] = None
    return out


def load_upstream() -> tuple[dict, dict[str, Any]]:
    if not UPSTREAM_PATH.is_file():
        return {}, {}
    data = yaml.safe_load(UPSTREAM_PATH.read_text(encoding="utf-8")) or {}
    blocks = data.get("blocks", {})
    textures = data.get("textures", {})
    return blocks, textures


def main() -> int:
    if not MT_TEX.is_dir():
        raise SystemExit(f"Missing {MT_TEX} — run download_texture_packs.py --pack minetest_default")

    write_minetest_stem_map_yaml(STEM_MAP_PATH)
    stem_map = ensure_stem_map()
    index = index_pack(MT_TEX)

    blocks_out: dict[str, Any] = {}
    textures_out: dict[str, Any] = {}
    skipped_manifest: list[str] = []

    for entry in load_manifest_blocks():
        name = entry["name"]
        stems = face_stems(entry)
        resolved = resolve_all_stems(list(set(stems)), index, stem_map)
        if any(v is None for v in resolved.values()):
            skipped_manifest.append(name)
            continue
        blocks_out[name] = block_spec(entry)
        for stem, ref in resolved.items():
            if stem not in textures_out and ref is not None:
                textures_out[stem] = ref

    upstream_blocks, upstream_textures = load_upstream()
    added_upstream = 0
    for name, spec in upstream_blocks.items():
        if name in blocks_out:
            continue
        if isinstance(spec, str):
            stems = [spec] * 6
        else:
            stems = spec.get("faces", [spec.get("uniform", "")] * 6)
        stems = stems[:6]
        if not stems or any(not s for s in stems):
            continue
        ok = True
        for stem in set(stems):
            if stem not in upstream_textures:
                ok = False
                break
            ref = upstream_textures[stem]
            if not texture_ref_exists(MT_TEX, ref):
                ok = False
                break
        if not ok:
            continue
        clean = {k: v for k, v in spec.items() if not k.startswith("_")}
        blocks_out[name] = clean
        for stem in set(stems):
            if stem not in textures_out:
                textures_out[stem] = upstream_textures[stem]
        added_upstream += 1

    for extra_stem in ("fire_1",):
        if "fire" in blocks_out and extra_stem not in textures_out:
            ref = resolve_stem_ref(extra_stem, index, stem_map, MT_TEX)
            if ref is not None and texture_ref_exists(MT_TEX, ref):
                textures_out[extra_stem] = ref

    animated: dict[str, list[Any]] = {}
    for stem, mt_name in [
        ("water", "default_water_source_animated.png"),
        ("lava", "default_lava_source_animated.png"),
    ]:
        path = MT_TEX / mt_name
        if path.is_file() and stem in blocks_out:
            animated[stem] = [mt_name]

    out = {
        "license": "CC-BY-SA-3.0",
        "license_text": (
            "Minetest Game default textures (CC BY-SA 3.0).\n"
            "Copyright (c) celeron55, Perttu Ahola et al.\n"
            "Source: https://github.com/minetest-game/default\n"
            "Modifications and repackaging under the same license.\n"
        ),
        "pack_version": 2,
        "blocks": blocks_out,
        "textures": textures_out,
        "animated": animated,
    }

    path = REPO / "tools" / "minetest_mapping.yaml"
    path.write_text(
        yaml.dump(out, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(
        f"Wrote {path}: {len(blocks_out)} blocks "
        f"({len(blocks_out) - added_upstream} manifest + {added_upstream} MTG-only), "
        f"{len(textures_out)} textures, skipped manifest {len(skipped_manifest)}"
    )
    if skipped_manifest:
        print("Skipped:", ", ".join(sorted(skipped_manifest)[:40]), end="")
        if len(skipped_manifest) > 40:
            print(f" ... +{len(skipped_manifest) - 40} more")
        else:
            print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
