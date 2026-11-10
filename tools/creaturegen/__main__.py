"""CreatureGen CLI."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw

from creaturegen.anatomy import build_model
from creaturegen.hashutil import file_hash, pixel_hash
from creaturegen.pipeline import (
    CATALOG,
    READS,
    STAGING,
    build_species,
    creature_ids,
    emit_recipes,
    isolation_ok,
    load_recipe,
    publish_skins,
    publish_species,
    validate_staging,
    write_asset_manifest,
    _bounds,
    _read_json,
    _target_px,
)
from creaturegen.pipeline import MODELS


def _sheet(species_id: str, out_dir: Path) -> None:
    recipe = load_recipe(species_id)
    definition = _read_json(MODELS / species_id / "creature.json")
    bounds = _bounds(definition)
    target = _target_px(definition)
    canvas = Image.new("RGB", (640, 240), (236, 232, 220))
    for index, concept in enumerate(recipe["concepts"][:3]):
        scale = tuple(concept.get("body_scale") or [1, 1, 1])
        model = build_model(recipe, target, scale, bounds)
        tile = Image.new("RGB", (200, 220), (248, 246, 238))
        draw = ImageDraw.Draw(tile)
        draw.rectangle((8, 8, 96, 200), fill=(20, 18, 16))
        draw.rectangle((104, 8, 192, 200), fill=(20, 18, 16))
        cubes = model.cubes()
        if cubes:
            max_axis = max(max(cube.origin[i] + cube.size[i] for cube in cubes) for i in range(3))
            max_axis = max(1, max_axis)

            def map_point(x, y, box):
                return (box[0] + int(x / max_axis * 80), box[1] + 170 - int(y / max_axis * 150))

            def rect(a, b, c, d):
                x0, x1 = sorted((a, c))
                y0, y1 = sorted((b, d))
                if x1 == x0:
                    x1 += 1
                if y1 == y0:
                    y1 += 1
                return (x0, y0, x1, y1)

            for cube in cubes:
                x, y, z = cube.origin
                sx, sy, sz = cube.size
                side = rect(*map_point(z, y, (8, 8)), *map_point(z + sz, y + sy, (8, 8)))
                three = rect(*map_point(x + z, y, (104, 8)), *map_point(x + sx + z + sz, y + sy, (104, 8)))
                draw.rectangle(side, fill=(20, 18, 16))
                draw.rectangle(three, fill=(186, 92, 42) if index == 0 else (72, 96, 70))
        canvas.paste(tile, (index * 210 + 10, 10))
    out_dir.mkdir(parents=True, exist_ok=True)
    canvas.save(out_dir / f"{species_id}_concepts.png")


def _compare(species_id: str) -> bool:
    left = STAGING / "_cmp_a" / species_id
    right = STAGING / "_cmp_b" / species_id
    build_species(species_id, left)
    build_species(species_id, right)
    same = True
    for path in sorted(p for p in left.rglob("*") if p.is_file()):
        other = right / path.relative_to(left)
        if path.suffix == ".png":
            with Image.open(path) as a, Image.open(other) as b:
                same = same and pixel_hash(a) == pixel_hash(b)
        else:
            same = same and file_hash(path) == file_hash(other)
    return same


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Build original Cubatarium creatures")
    parser.add_argument("command", choices=[
        "inventory", "emit", "build", "validate", "compare", "publish", "audit", "silhouettes", "frames", "previews", "measure-familiar",
    ])
    parser.add_argument("--species", default="")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--staging", action="store_true")
    parser.add_argument("--effective", action="store_true")
    parser.add_argument("--dir", default="")
    parser.add_argument("--textured", action="store_true", help="sample atlas texels onto each face")
    args = parser.parse_args(argv)
    selected = [part for part in args.species.split(",") if part] if args.species else []
    if args.all or not selected:
        if args.command in ("build", "validate", "publish") and (args.all or not selected):
            selected = creature_ids()
    if args.command == "frames":
        folder = Path(args.dir) if args.dir else Path("docs/research/c0-frames")
        pngs = sorted(folder.glob("*.png"))
        if not pngs:
            print(f"FAIL no png in {folder}")
            return 1
        for path in pngs:
            raw = path.read_bytes()
            if raw.startswith(b"BMP") or not raw.startswith(b"\x89PNG"):
                print(f"FAIL marker or non-png {path}")
                return 1
            with Image.open(path) as image:
                image.load()
            print("OK", path.name, raw[:8])
        return 0
    if args.command == "previews":
        from creaturegen.preview import OUT, OUT_TEXTURED, render_all, render_species, write_index
        out = OUT_TEXTURED if args.textured else OUT
        ids = selected or None
        if ids:
            for species_id in ids:
                render_species(species_id, out, args.textured)
            paths = sorted(out.glob("*.png"))
            write_index(paths, out, args.textured)
        else:
            paths = render_all(out, args.textured)
        print(f"wrote {len(paths)} sheets in {out}")
        return 0
    if args.command == "measure-familiar":
        from creaturegen.measure_familiar import main as measure_main
        measure_main()
        return 0
    if args.command == "inventory":
        print(json.dumps({"species": len(creature_ids()), "ids": creature_ids()}, indent=2))
        return 0
    if args.command == "emit":
        emit_recipes()
        print(f"wrote {CATALOG}")
        return 0
    if args.command == "audit":
        from research import audit_creature_originality
        audit_creature_originality.main()
        return 0
    if args.command == "silhouettes":
        ids = selected or ["cow", "badger", "rigid_demo_walker"]
        out = STAGING / "concepts"
        for species_id in ids:
            _sheet(species_id, out)
            print(species_id)
        return 0
    if args.command == "compare":
        ids = selected or ["cow", "badger", "rigid_demo_walker"]
        failed = [species_id for species_id in ids if not _compare(species_id)]
        print("isolation", isolation_ok(), "reads", len(READS))
        return 1 if failed else 0
    if args.command == "build":
        emit_recipes()
        for species_id in selected:
            out = build_species(species_id)
            errors = validate_staging(species_id, out)
            print(species_id, "OK" if not errors else errors)
            if errors:
                return 1
        return 0
    if args.command == "validate":
        failed = False
        for species_id in selected:
            errors = validate_staging(species_id)
            if errors:
                failed = True
                print(species_id, errors)
            else:
                print("OK", species_id)
        return 1 if failed else 0
    if args.command == "publish":
        emit_recipes()
        for species_id in selected:
            publish_species(species_id)
            print("published", species_id)
        publish_skins()
        write_asset_manifest()
        print("skins and manifest published")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
