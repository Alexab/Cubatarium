#!/usr/bin/env python3
"""Sync Luanti mob textures into CubatariumTextureResearch (TD-CRE-021)."""

from __future__ import annotations

import argparse
import shutil
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

# species -> (destination relative to research, source relative OR download URL)
TEXTURE_ALIASES: dict[str, tuple[str, str]] = {
    "kitten": (
        "mobs_animal/textures/mobs_kitten.png",
        "mobs_animal/textures/mobs_kitten_striped.png",
    ),
    "warthog": (
        "mobs_animal/textures/mobs_warthog.png",
        "mobs_animal/textures/mobs_pumba.png",
    ),
    "mese_monster": (
        "mobs_monster/textures/mobs_mese_monster.png",
        "mobs_monster/textures/mobs_mese_monster_purple.png",
    ),
    "lava_flan": (
        "mobs_monster/textures/mobs_lava_flan.png",
        "mobs_monster/textures/zmobs_lava_flan.png",
    ),
}

DOWNLOADS: dict[str, tuple[str, str]] = {
    "dolphin": (
        "animalworld/textures/texturedolphin.png",
        "https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/1.19.2/assets/minecraft/textures/entity/dolphin.png",
    ),
    "octopus": (
        "animalworld/textures/textureoctopus.png",
        "https://raw.githubusercontent.com/Skandarella/marinaramobs/main/textures/textureoctopus.png",
    ),
    "whale": (
        "animalworld/textures/texturewhale.png",
        "dmobs/textures/dmobs_whale.png",
    ),
    "water_dragon": (
        "animalworld/textures/texturewaterdragon.png",
        "dmobs/textures/dmobs_waterdragon.png",
    ),
}


def copy_local(research: Path, dest_rel: str, src_rel: str) -> None:
    dest = research / dest_rel.replace("/", "\\")
    src = research / src_rel.replace("/", "\\")
    if not src.is_file():
        raise FileNotFoundError(src)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest)


def download_url(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "cubatarium-sync"})
    with urllib.request.urlopen(req, timeout=60) as resp:
        dest.write_bytes(resp.read())


def sync_species(research: Path, species: str) -> None:
    if species in TEXTURE_ALIASES:
        dest_rel, src_rel = TEXTURE_ALIASES[species]
        copy_local(research, dest_rel, src_rel)
        print(f"OK {species}: alias {src_rel} -> {dest_rel}")
        return
    if species in DOWNLOADS:
        dest_rel, source = DOWNLOADS[species]
        dest = research / dest_rel.replace("/", "\\")
        if source.startswith("http"):
            download_url(source, dest)
            print(f"OK {species}: download -> {dest_rel}")
        else:
            copy_local(research, dest_rel, source)
            print(f"OK {species}: copy {source} -> {dest_rel}")
        return
    raise KeyError(f"no sync rule for {species}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    ap.add_argument(
        "--species",
        action="append",
        help="species id (default: TD-CRE-021 blockers)",
    )
    args = ap.parse_args()
    research = args.research.resolve()
    species_list = args.species or list(TEXTURE_ALIASES) + list(DOWNLOADS)
    err = 0
    for species in species_list:
        try:
            sync_species(research, species)
        except Exception as exc:
            print(f"FAIL {species}: {exc}", file=sys.stderr)
            err = 1
    return err


if __name__ == "__main__":
    raise SystemExit(main())
