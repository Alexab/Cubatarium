#!/usr/bin/env python3
"""Import Bedrock geo + diffuse texture and patch creature.json for bedrock_geo."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import urllib.request
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
SOURCES = ROOT / "models" / "creatures" / "_sources" / "bedrock_geo"
CATALOG_PATH = TOOLS / "bedrock_geo_catalog.yaml"

try:
    import yaml
except ImportError:
    yaml = None


def load_catalog() -> tuple[dict, dict]:
    if yaml is None:
        raise SystemExit("PyYAML required")
    data = yaml.safe_load(CATALOG_PATH.read_text(encoding="utf-8"))
    return data.get("defaults", {}), data.get("species", {})


def download_texture(url: str, dest: Path, texture_size: list[int] | None) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            dest.write_bytes(resp.read())
    except Exception as exc:
        print(f"  WARN texture download failed: {exc}")
        return False

    if texture_size and len(texture_size) >= 2:
        try:
            from PIL import Image

            tw, th = int(texture_size[0]), int(texture_size[1])
            with Image.open(dest) as img:
                if img.size != (tw, th):
                    if img.size == (64, 64) and (tw, th) == (64, 32):
                        img = img.crop((0, 0, 64, 32))
                        img.save(dest)
                        print(f"  crop texture {img.size[0]}x{img.size[1]} for bedrock box UV")
                    elif img.size != (tw, th):
                        img = img.resize((tw, th), Image.NEAREST)
                        img.save(dest)
                        print(f"  resize texture -> {tw}x{th}")
        except ImportError:
            pass
    return True


def patch_creature_json(path: Path, entry: dict, defaults: dict) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    visual = data.setdefault("visual", {})
    visual["backend"] = "bedrock_geo"
    visual["geometry"] = entry["geometry"]
    visual["geometry_file"] = entry.get("geometry_file", defaults.get("geometry_file", "geometry.geo.json"))
    visual["texture"] = entry.get("texture_stem", defaults.get("texture_stem", "diffuse"))
    visual["texture_size"] = entry.get("texture_size", defaults.get("texture_size", [64, 32]))
    visual["animation_profile"] = entry.get("animation_profile", "quadruped")
    anim = visual.setdefault("animation", {})
    anim.setdefault("walk_cycle_hz", 2.0)
    anim.setdefault("leg_swing_deg", 45)
    anim.setdefault("arm_swing_deg", 40)
    anim.setdefault("body_bob_blocks", 0.03)
    anim.setdefault("look_at_deg", 30)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def import_species(species_id: str, entry: dict, defaults: dict) -> None:
    species_dir = ROOT / "models" / "creatures" / species_id
    creature_json = species_dir / "creature.json"
    if not creature_json.is_file():
        raise SystemExit(f"Missing {creature_json}")

    src_geo = SOURCES / entry.get("geometry_source", f"{species_id}.geo.json")
    if not src_geo.is_file():
        raise SystemExit(f"Missing geo source {src_geo} — run setup_bedrock_creature_sources.py")

    dst_geo = species_dir / entry.get("geometry_file", defaults.get("geometry_file", "geometry.geo.json"))
    shutil.copy2(src_geo, dst_geo)

    tex_dest = species_dir / "textures" / f"{entry.get('texture_stem', 'diffuse')}.png"
    texture_size = entry.get("texture_size", defaults.get("texture_size", [64, 32]))
    if entry.get("texture_url"):
        print(f"  download texture -> {tex_dest.name}")
        download_texture(entry["texture_url"], tex_dest, texture_size)

    patch_creature_json(creature_json, entry, defaults)
    print(f"OK import {species_id}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--species", action="append", required=True)
    args = parser.parse_args()

    defaults, catalog = load_catalog()
    for species_id in args.species:
        if species_id not in catalog:
            raise SystemExit(f"Unknown species in catalog: {species_id}")
        import_species(species_id, catalog[species_id], defaults)
    return 0


if __name__ == "__main__":
    sys.exit(main())
