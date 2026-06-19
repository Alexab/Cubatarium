#!/usr/bin/env python3
"""Import CC-licensed Luanti mob textures into models/creatures and models/skins."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow", "-q"])
    from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")
IMPORTS_YAML = ROOT / "tools" / "creature_texture_imports.yaml"

# Luanti 64x32 skin regions (Minecraft classic layout).
SKIN64x32 = {
    "head_top": (8, 0, 8, 8),
    "head_front": (8, 8, 8, 8),
    "head_back": (24, 8, 8, 8),
    "head_left": (0, 8, 8, 8),
    "head_right": (16, 8, 8, 8),
    "body_front": (20, 20, 8, 12),
    "body_back": (32, 20, 8, 12),
    "body_left": (16, 20, 4, 12),
    "body_right": (28, 20, 4, 12),
    "arm_right": (44, 20, 4, 12),
    "arm_left": (44, 20, 4, 12),
    "leg_right": (4, 20, 4, 12),
    "leg_left": (4, 20, 4, 12),
}

# 64x64 skin uses same head/body coords; legs/arms at different Y.
SKIN64x64_ARM_RIGHT = (40, 20, 4, 12)
SKIN64x64_ARM_LEFT = (32, 52, 4, 12)
SKIN64x64_LEG_RIGHT = (4, 20, 4, 12)
SKIN64x64_LEG_LEFT = (20, 52, 4, 12)


@dataclass(frozen=True)
class TextureSource:
    path: str
    license: str
    attribution: str


@dataclass(frozen=True)
class MobImport:
    species_id: str
    texture: TextureSource


@dataclass(frozen=True)
class SkinImport:
    skin_id: str
    creature_id: str
    texture: TextureSource
    tint_rgb: tuple[int, int, int] | None = None


DOWNLOAD_REPOS = [
    ("mobs_animal", "https://codeberg.org/tenplus1/mobs_animal.git"),
    ("mobs_monster", "https://codeberg.org/tenplus1/mobs_monster.git"),
    ("dmobs", "https://codeberg.org/tenplus1/dmobs.git"),
    ("animalworld", "https://github.com/Skandarella/animalworld.git"),
    ("minetest_game", "https://github.com/minetest/minetest_game.git"),
]


def load_yaml_imports() -> list[MobImport]:
    if not IMPORTS_YAML.is_file():
        return []
    try:
        import yaml
    except ImportError:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "pyyaml", "-q"])
        import yaml
    data = yaml.safe_load(IMPORTS_YAML.read_text(encoding="utf-8")) or {}
    out: list[MobImport] = []
    for species_id, meta in (data.get("mobs") or {}).items():
        out.append(
            MobImport(
                species_id,
                TextureSource(
                    meta["path"],
                    meta.get("license", "Unknown"),
                    meta.get("attribution", meta["path"]),
                ),
            )
        )
    return out


def mob_sources(research: Path) -> list[MobImport]:
    base = [
        MobImport(
            "human",
            TextureSource(
                "minetest_game/mods/player_api/models/character.png",
                "CC BY-SA 3.0",
                "Minetest / Luanti player_api (celeron55, Perttu Ahola et al.)",
            ),
        ),
        MobImport(
            "sheep",
            TextureSource(
                "mobs_animal/textures/mobs_sheep_wool.png",
                "MIT",
                "mobs_animal — Krupnov Pavel, TenPlus1",
            ),
        ),
        MobImport(
            "wolf",
            TextureSource(
                "animalworld/textures/awolf.png",
                "MIT",
                "animalworld — Skandarella / Liil",
            ),
        ),
        MobImport(
            "pig",
            TextureSource(
                "animalworld/textures/awildboar.png",
                "MIT",
                "animalworld — Skandarella / Liil (wild boar as pig stand-in)",
            ),
        ),
        MobImport(
            "cow",
            TextureSource(
                "mobs_animal/textures/mobs_cow.png",
                "MIT",
                "mobs_animal — sirrobzeroone, TenPlus1",
            ),
        ),
        MobImport(
            "chicken",
            TextureSource(
                "mobs_animal/textures/mobs_chicken.png",
                "MIT",
                "mobs_animal — TenPlus1",
            ),
        ),
        MobImport(
            "oerkki",
            TextureSource(
                "mobs_monster/textures/mobs_oerkki.png",
                "MIT",
                "mobs_monster — TenPlus1",
            ),
        ),
        MobImport(
            "skeleton",
            TextureSource(
                "dmobs/textures/dmobs_skeleton.png",
                "CC BY-SA 3.0",
                "dmobs — D00Med",
            ),
        ),
        MobImport(
            "sand_monster",
            TextureSource(
                "mobs_monster/textures/mobs_sand_monster.png",
                "MIT",
                "mobs_monster — TenPlus1",
            ),
        ),
    ]
    seen = {entry.species_id for entry in base}
    for entry in load_yaml_imports():
        if entry.species_id not in seen:
            base.append(entry)
            seen.add(entry.species_id)
    return base


def skin_sources(research: Path) -> list[SkinImport]:
    return [
        SkinImport(
            "human_adventurer",
            "human",
            TextureSource(
                "minetest_game/mods/player_api/models/character.png",
                "CC BY-SA 3.0",
                "Minetest player_api (tinted adventurer)",
            ),
            tint_rgb=(242, 191, 51),
        ),
        SkinImport(
            "human_guard",
            "human",
            TextureSource(
                "minetest_game/mods/player_api/models/character.png",
                "CC BY-SA 3.0",
                "Minetest player_api (tinted guard)",
            ),
            tint_rgb=(140, 140, 153),
        ),
        SkinImport(
            "sheep_wool_black",
            "sheep",
            TextureSource(
                "mobs_animal/textures/mobs_sheep_wool.png",
                "MIT",
                "mobs_animal — Krupnov Pavel, TenPlus1 (black dye)",
            ),
            tint_rgb=(40, 40, 45),
        ),
        SkinImport(
            "sheep_wool_golden",
            "sheep",
            TextureSource(
                "mobs_animal/textures/mobs_sheep_wool.png",
                "MIT",
                "mobs_animal — Krupnov Pavel, TenPlus1 (golden dye)",
            ),
            tint_rgb=(255, 220, 80),
        ),
        SkinImport(
            "wolf_snow",
            "wolf",
            TextureSource(
                "animalworld/textures/texturewolf3.png",
                "MIT",
                "animalworld — Skandarella / Liil (snow variant)",
            ),
        ),
    ]


def ensure_research(research: Path, download: bool) -> None:
    if research.is_dir() and any(research.iterdir()):
        return
    if not download:
        raise SystemExit(
            f"Research folder missing or empty: {research}\n"
            "Run with --download to clone Luanti texture sources."
        )
    research.mkdir(parents=True, exist_ok=True)
    for name, url in DOWNLOAD_REPOS:
        dest = research / name
        if dest.exists():
            continue
        print(f"cloning {name}...")
        subprocess.run(
            ["git", "clone", "--depth", "1", url, str(dest)],
            check=True,
        )


def crop_skin_region(skin: Image.Image, box: tuple[int, int, int, int]) -> Image.Image:
    x, y, w, h = box
    return skin.crop((x, y, x + w, y + h))


def skin_regions(skin: Image.Image) -> dict[str, Image.Image]:
    w, h = skin.size
    if w == 64 and h == 32:
        return {k: crop_skin_region(skin, v) for k, v in SKIN64x32.items()}
    if w == 64 and h == 64:
        base = dict(SKIN64x32)
        base["arm_right"] = SKIN64x64_ARM_RIGHT
        base["arm_left"] = SKIN64x64_ARM_LEFT
        base["leg_right"] = SKIN64x64_LEG_RIGHT
        base["leg_left"] = SKIN64x64_LEG_LEFT
        return {k: crop_skin_region(skin, v) for k, v in base.items()}
    raise ValueError(f"Unsupported skin size {w}x{h} (expected 64x32 or 64x64)")


def paste_uv(canvas: Image.Image, region: Image.Image, u0: float, v0: float, u1: float, v1: float) -> None:
    size = canvas.size[0]
    x0, y0 = int(u0 * size), int(v0 * size)
    x1, y1 = int(u1 * size), int(v1 * size)
    resized = region.resize((max(1, x1 - x0), max(1, y1 - y0)), Image.NEAREST)
    canvas.paste(resized, (x0, y0))


def average_color(img: Image.Image) -> tuple[int, int, int]:
    px = img.convert("RGBA").resize((1, 1), Image.NEAREST).getpixel((0, 0))
    return px[0], px[1], px[2]


def fill_plain(canvas: Image.Image, color: tuple[int, int, int]) -> None:
    plain = Image.new("RGBA", canvas.size, (*color, 255))
    canvas.paste(plain)


def build_face_atlas(regions: dict[str, Image.Image]) -> Image.Image:
    canvas = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    plain = average_color(regions["head_left"])
    fill_plain(canvas, plain)
    paste_uv(canvas, regions["head_front"], 0.28, 0.22, 0.72, 0.78)
    paste_uv(canvas, regions["head_top"], 0.55, 0.02, 0.78, 0.14)
    return canvas


def build_body_atlas(regions: dict[str, Image.Image]) -> Image.Image:
    canvas = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    plain = average_color(regions["body_left"])
    fill_plain(canvas, plain)
    paste_uv(canvas, regions["body_front"], 0.28, 0.22, 0.72, 0.78)
    paste_uv(canvas, regions["body_back"], 0.02, 0.55, 0.18, 0.82)
    return canvas


def build_limb_atlas(region: Image.Image) -> Image.Image:
    return region.resize((64, 64), Image.NEAREST)


def opaque_fill(img: Image.Image) -> Image.Image:
    out = img.convert("RGBA").copy()
    w, h = out.size
    if w == 0 or h == 0:
        return out
    px = out.load()
    for _ in range(max(w, h)):
        changed = False
        nxt = out.copy()
        npx = nxt.load()
        for y in range(h):
            for x in range(w):
                if px[x, y][3] >= 250:
                    continue
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and px[nx, ny][3] >= 250:
                        npx[x, y] = (px[nx, ny][0], px[nx, ny][1], px[nx, ny][2], 255)
                        changed = True
                        break
        out = nxt
        px = out.load()
        if not changed:
            break
    return out


def tint_rgba(img: Image.Image, rgb: tuple[int, int, int], strength: float = 0.45) -> Image.Image:
    out = img.convert("RGBA")
    tint = Image.new("RGBA", out.size, (*rgb, 255))
    return Image.blend(out, tint, strength)


def export_player_parts(skin_path: Path, out_dir: Path, tint: tuple[int, int, int] | None) -> None:
    skin = Image.open(skin_path).convert("RGBA")
    regions = skin_regions(skin)
    face = build_face_atlas(regions)
    body = build_body_atlas(regions)
    arm = build_limb_atlas(regions["arm_right"])
    leg = build_limb_atlas(regions["leg_right"])
    if tint:
        body = tint_rgba(body, tint, 0.35)
        leg = tint_rgba(leg, tint, 0.4)
        arm = tint_rgba(arm, tint, 0.25)
    out_dir.mkdir(parents=True, exist_ok=True)
    opaque_fill(face).save(out_dir / "face.png")
    opaque_fill(body).save(out_dir / "body.png")
    opaque_fill(arm).save(out_dir / "arm.png")
    opaque_fill(leg).save(out_dir / "leg.png")
    icon = skin.resize((32, 32), Image.NEAREST)
    icon.save(out_dir / "icon.png")


def export_skin_player_parts(
    skin_path: Path, out_dir: Path, tint: tuple[int, int, int] | None
) -> None:
    skin = Image.open(skin_path).convert("RGBA")
    regions = skin_regions(skin)
    face = build_face_atlas(regions)
    torso = build_body_atlas(regions)
    arms = build_limb_atlas(regions["arm_right"])
    legs = build_limb_atlas(regions["leg_right"])
    if tint:
        torso = tint_rgba(torso, tint, 0.35)
        legs = tint_rgba(legs, tint, 0.4)
        arms = tint_rgba(arms, tint, 0.25)
    out_dir.mkdir(parents=True, exist_ok=True)
    opaque_fill(face).save(out_dir / "face.png")
    opaque_fill(torso).save(out_dir / "torso.png")
    opaque_fill(arms).save(out_dir / "arms.png")
    opaque_fill(legs).save(out_dir / "legs.png")
    opaque_fill(torso).save(out_dir / "diffuse.png")


def export_mob_texture(src: Path, out_dir: Path) -> None:
    img = Image.open(src).convert("RGBA")
    out_dir.mkdir(parents=True, exist_ok=True)
    for stem in ("body", "leg", "arm", "face"):
        shutil.copy2(src, out_dir / f"{stem}.png")
    img.resize((32, 32), Image.NEAREST).save(out_dir / "icon.png")


def export_mob_skin_texture(src: Path, out_dir: Path, tint: tuple[int, int, int] | None) -> None:
    img = Image.open(src).convert("RGBA")
    if tint:
        img = tint_rgba(img, tint, 0.55)
    out_dir.mkdir(parents=True, exist_ok=True)
    for stem in ("body", "leg", "face"):
        out = out_dir / f"{stem}.png"
        img.save(out)
    img.save(out_dir / "diffuse.png")


def write_license(path: Path, source: TextureSource, src_file: Path) -> None:
    path.write_text(
        f"Source file: {src_file.name}\n"
        f"Upstream: {source.path}\n"
        f"License: {source.license}\n"
        f"Attribution: {source.attribution}\n",
        encoding="utf-8",
    )


def import_mobs(research: Path, only: set[str] | None) -> None:
    for entry in mob_sources(research):
        if only and entry.species_id not in only:
            continue
        src = research / entry.texture.path
        if not src.is_file():
            print(f"SKIP {entry.species_id}: missing {src}")
            continue
        out = ROOT / "models" / "creatures" / entry.species_id / "textures"
        if entry.species_id == "human":
            export_player_parts(src, out, None)
        else:
            export_mob_texture(src, out)
        write_license(
            ROOT / "models" / "creatures" / entry.species_id / "LICENSE.txt",
            entry.texture,
            src,
        )
        print(f"imported creature {entry.species_id} <- {entry.texture.path}")


def import_skins(research: Path, only: set[str] | None) -> None:
    for entry in skin_sources(research):
        if only and entry.skin_id not in only:
            continue
        src = research / entry.texture.path
        if not src.is_file():
            print(f"SKIP skin {entry.skin_id}: missing {src}")
            continue
        out = ROOT / "models" / "skins" / entry.skin_id / "textures"
        if entry.creature_id == "human":
            export_skin_player_parts(src, out, entry.tint_rgb)
        else:
            export_mob_skin_texture(src, out, entry.tint_rgb)
        license_path = ROOT / "models" / "skins" / entry.skin_id / "LICENSE.txt"
        write_license(license_path, entry.texture, src)
        print(f"imported skin {entry.skin_id} <- {entry.texture.path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--research",
        type=Path,
        default=RESEARCH_DEFAULT,
        help="Folder with cloned Luanti mod sources",
    )
    parser.add_argument("--download", action="store_true", help="Clone missing mod repos")
    parser.add_argument("--creature", action="append", help="Import only this species id")
    parser.add_argument("--skin", action="append", help="Import only this skin id")
    args = parser.parse_args()

    ensure_research(args.research.resolve(), args.download)
    only_creatures = set(args.creature) if args.creature else None
    only_skins = set(args.skin) if args.skin else None
    import_mobs(args.research.resolve(), only_creatures)
    import_skins(args.research.resolve(), only_skins)
    print("done")


if __name__ == "__main__":
    main()
