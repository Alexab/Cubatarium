#!/usr/bin/env python3
"""Generate tools/too_many_stones_mapping.yaml — partial stone/ore mapping."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
TMS_TEX = RESEARCH / "too_many_stones" / "textures"

sys.path.insert(0, str(REPO / "tools"))
from stem_mapping_common import block_spec, load_manifest_blocks  # noqa: E402

# Cubatarium block name -> TMS texture file (must exist in upstream)
BLOCK_TO_TMS: dict[str, str] = {
    "stone": "tms_andesite_block.png",
    "stoneMoss": "tms_agate_moss.png",
    "stonebricksmooth": "tms_marble_block.png",
    "stonebricksmooth_cracked": "tms_marble_cracked_brick.png",
    "stonebricksmooth_mossy": "tms_agate_moss.png",
    "stonebricksmooth_carved": "tms_marble_brick.png",
    "stonebrick": "tms_basalt_brick.png",
    "sandstone": "tms_sandstone.png",
    "sandstone_carved": "tms_desert_sandstone.png",
    "sandstone_smooth": "tms_silver_sandstone.png",
    "bedrock": "tms_basalt_block.png",
    "gravel": "tms_basalt_cobble.png",
    "obsidian": "tms_basalt.png",
    "clay": "tms_amber.png",
    "hellrock": "tms_basalt_block.png",
    "whiteStone": "tms_pumice_block.png",
    "oreCoal": "tms_galena.png",
    "oreIron": "tms_ilvaite.png",
    "oreGold": "tms_pyrite.png",
    "oreDiamond": "tms_amethyst.png",
    "oreEmerald": "tms_jade.png",
    "oreLapis": "tms_lapis_lazuli.png",
    "oreRedstone": "tms_erythrite.png",
    "blockIron": "tms_ilvaite_block.png",
    "blockGold": "tms_pyrite_block.png",
    "blockDiamond": "tms_amethyst_block.png",
    "blockEmerald": "tms_jade_block.png",
    "blockLapis": "tms_lapis_lazuli_block.png",
    "blockRedstone": "tms_erythrite_block.png",
    "brick": "tms_marble_brick.png",
    "nether_brick": "tms_basalt_brick.png",
    "glowstone": "tms_glow_calcite.png",
    "ice": "tms_amethyst.png",
    "snow": "tms_pumice.png",
    "sand": "tms_desert_sandstone.png",
    "dirt": "tms_amber.png",
    "stone_slab": "tms_andesite_block.png",
    "stone_slab_top": "tms_andesite_block.png",
}


def pick_tms_file(stem: str) -> str | None:
    for path in sorted(TMS_TEX.glob("tms_*.png")):
        if stem.replace(".png", "") in path.name:
            return path.name
    return None


def resolve_tms(name: str) -> str | None:
    if name in BLOCK_TO_TMS:
        candidate = BLOCK_TO_TMS[name]
        if (TMS_TEX / candidate).is_file():
            return candidate
    return pick_tms_file(name)


def main() -> int:
    if not TMS_TEX.is_dir():
        raise SystemExit(f"Missing {TMS_TEX}")

    available = {p.name for p in TMS_TEX.glob("*.png")}

    def resolve_file(tex: str) -> str | None:
        if tex in available:
            return tex
        stem = tex.replace(".png", "")
        for name in sorted(available):
            if stem.replace("tms_", "") in name:
                return name
        return None

    fixed_map: dict[str, str] = {}
    for block, tex in BLOCK_TO_TMS.items():
        hit = resolve_file(tex)
        if hit:
            fixed_map[block] = hit

    blocks_out: dict[str, Any] = {}
    textures_out: dict[str, str] = {}
    manifest_by_name = {b["name"]: b for b in load_manifest_blocks()}

    for block_name, tms_file in sorted(fixed_map.items()):
        entry = manifest_by_name.get(block_name)
        if not entry:
            continue
        spec = block_spec(entry)
        if isinstance(spec, str):
            stem = spec
        else:
            faces = spec.get("faces", [])
            stem = faces[0] if faces else block_name
        blocks_out[block_name] = spec
        if isinstance(spec, dict):
            for face_stem in set(spec.get("faces", [spec.get("uniform", stem)])):
                textures_out[face_stem] = tms_file
        else:
            textures_out[stem] = tms_file

    out = {
        "license": "CC0-1.0",
        "license_text": (
            "Too Many Stones textures (CC0).\n"
            "Source: https://github.com/asuna-mt/Too_Many_Stones\n"
            "Partial mapping to Cubatarium stone/ore blocks.\n"
        ),
        "blocks": blocks_out,
        "textures": textures_out,
    }
    path = REPO / "tools" / "too_many_stones_mapping.yaml"
    path.write_text(
        yaml.dump(out, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(f"Wrote {path}: {len(blocks_out)} blocks, {len(textures_out)} textures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
