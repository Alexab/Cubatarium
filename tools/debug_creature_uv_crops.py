#!/usr/bin/env python3
"""Draw manual/auto UV crop rects on Luanti mob atlases for validation."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml
from PIL import Image, ImageDraw

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

from bake_rigid_creature_textures import (
    compute_stem_rects,
    load_atlas,
    load_yaml,
)
from creature_tier_a import TIER_A_MOBS, TIER_A_SPECIES


def draw_rects(
    atlas: Image.Image,
    rects: dict[str, tuple[float, float, float, float]],
    out_path: Path,
    title: str = "",
) -> None:
    w, h = atlas.size
    overlay = atlas.convert("RGBA").copy()
    draw = ImageDraw.Draw(overlay, "RGBA")
    colors = {
        "body": (0, 200, 80, 120),
        "face": (255, 80, 80, 120),
        "leg": (80, 120, 255, 120),
        "arm": (255, 200, 0, 120),
        "ear": (200, 100, 255, 120),
        "tail": (100, 200, 200, 120),
        "wing": (255, 140, 40, 120),
    }
    for stem, rect in rects.items():
        u0, v0, u1, v1 = rect
        box = (int(u0 * w), int(v0 * h), int(u1 * w), int(v1 * h))
        color = colors.get(stem, (255, 255, 255, 100))
        draw.rectangle(box, outline=color[:3] + (255,), width=2)
        draw.rectangle(box, fill=color)
        draw.text((box[0] + 2, box[1] + 2), stem, fill=(255, 255, 255, 255))
    if title:
        draw.text((4, 4), title, fill=(255, 255, 0, 255))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    overlay.save(out_path)
    print(f"wrote {out_path}")


def write_baseline_manifest(out_dir: Path, species_list: list[str]) -> None:
    rows: list[dict] = []
    for species_id in species_list:
        creature_path = ROOT / "models" / "creatures" / species_id / "creature.json"
        if not creature_path.is_file():
            continue
        creature = json.loads(creature_path.read_text(encoding="utf-8"))
        vis = creature.get("visual", {})
        parts = vis.get("parts", [])
        rows.append(
            {
                "id": species_id,
                "texture_layout": vis.get("texture_layout", "rigid_crop"),
                "part_count": len(parts),
                "stems": sorted({p["texture"] for p in parts}),
            }
        )
    manifest = {"tier": "A", "species": rows}
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / "tier_a_baseline_manifest.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--species", action="append")
    parser.add_argument(
        "--tier-a",
        action="store_true",
        help="Process ship-set Tier A species only",
    )
    parser.add_argument(
        "--auto",
        action="store_true",
        help="Include auto-computed stem rects (not only manual_uv)",
    )
    parser.add_argument(
        "--manifest",
        action="store_true",
        help="Write tier_a_baseline_manifest.json",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=ROOT / "tools" / "debug_uv_overlays",
    )
    args = parser.parse_args()

    sources = load_yaml(TOOLS / "creature_luanti_sources.yaml")
    maps = load_yaml(TOOLS / "creature_rigid_uv_maps.yaml")
    pad = float(maps.get("uv_pad", 0.02))
    match_margin = float(maps.get("uv_match_margin", 0.15))
    leg_margin = float(maps.get("uv_leg_match_margin", 0.35))

    if args.tier_a:
        species_list = list(TIER_A_SPECIES)
    elif args.species:
        species_list = args.species
    else:
        species_list = list(sources["species"].keys())

    research = args.research.resolve()
    models_root = ROOT / "models"

    if args.manifest:
        write_baseline_manifest(args.out_dir, species_list)

    for species_id in species_list:
        if species_id == "human":
            print(f"skip {species_id} (player_skin_atlas)")
            continue
        spec = sources["species"].get(species_id)
        if not spec:
            print(f"skip {species_id} (no luanti source)")
            continue
        manual = {k: tuple(v) for k, v in spec.get("manual_uv", {}).items() if len(v) == 4}
        rects = dict(manual)
        if args.auto:
            try:
                auto = compute_stem_rects(
                    species_id,
                    sources,
                    maps,
                    research,
                    models_root,
                    pad,
                    match_margin,
                    leg_margin,
                )
                for stem, rect in auto.items():
                    rects.setdefault(stem, rect)
            except (FileNotFoundError, ValueError) as exc:
                print(f"auto skip {species_id}: {exc}")
        if not rects:
            print(f"skip {species_id} (no rects)")
            continue
        try:
            atlas = load_atlas(spec, research)
        except FileNotFoundError as exc:
            print(f"skip {species_id}: {exc}")
            continue
        draw_rects(
            atlas,
            rects,
            args.out_dir / f"{species_id}_uv_overlay.png",
            title=species_id,
        )

    if args.tier_a and not args.manifest:
        write_baseline_manifest(args.out_dir, species_list)


if __name__ == "__main__":
    main()
