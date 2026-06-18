#!/usr/bin/env python3
"""Analyze Cubatarium block textures and downloaded research packs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    import subprocess
    import sys

    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow", "-q"])
    from PIL import Image

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
OUT = RESEARCH / "analysis" / "raw"
CURRENT = REPO / "textures" / "blocks"
EXTERNAL = Path(r"E:/Work/Home/CubatariumTextures/blocks")

PACK_DIRS = {
    "cubatarium_current": CURRENT,
    "kenney_voxel_pack": RESEARCH / "kenney_voxel_pack",
    "kenney_pattern_pixel": RESEARCH / "kenney_pattern_pixel",
    "kenney_pattern_lines": RESEARCH / "kenney_pattern_lines",
    "minetest_default": RESEARCH / "minetest_default",
    "refi_textures": RESEARCH / "refi_textures",
    "programmer_art": RESEARCH / "programmer_art",
    "snez": RESEARCH / "snez",
    "too_many_stones": RESEARCH / "too_many_stones",
    "seamless_pattern_pack": RESEARCH / "seamless_pattern_pack",
    "oga_16x16_blocks": RESEARCH / "oga_16x16_blocks",
    "oga_mc_inspired": RESEARCH / "oga_mc_inspired",
    "sbs_sandbox_terrain": RESEARCH / "sbs_sandbox_terrain",
    "goncalo_pixel_patterns": RESEARCH / "goncalo_pixel_patterns",
    "vexed_block_land": RESEARCH / "vexed_block_land",
}

PACK_META = {
    "cubatarium_current": {"license": "Unknown (likely Minecraft-derived)", "source": "textures/blocks in repo"},
    "kenney_voxel_pack": {"license": "CC0-1.0", "source": "https://kenney.nl/assets/voxel-pack"},
    "kenney_pattern_pixel": {"license": "CC0-1.0", "source": "https://kenney.nl/assets/pattern-pack-pixel"},
    "kenney_pattern_lines": {"license": "CC0-1.0", "source": "https://kenney-assets.itch.io/pattern-pack-2"},
    "minetest_default": {"license": "CC-BY-SA-3.0", "source": "https://github.com/minetest-game/default"},
    "seamless_pattern_pack": {"license": "CC0-1.0", "source": "https://opengameart.org/content/seamless-pattern-pack"},
    "oga_16x16_blocks": {"license": "CC0-1.0", "source": "https://opengameart.org/content/16x16-block-texture-set"},
    "oga_mc_inspired": {"license": "CC0-1.0", "source": "https://opengameart.org/content/cc0-minecraft-inspired-textures"},
    "sbs_sandbox_terrain": {"license": "CC0-1.0", "source": "https://screamingbrainstudios.itch.io/sbst-pack"},
    "goncalo_pixel_patterns": {"license": "CC0-1.0", "source": "https://goncalomcoliveira.itch.io/pixel-patterns"},
    "vexed_block_land": {"license": "CC0-1.0", "source": "https://v3x3d.itch.io/block-land"},
}

# Explicit stem aliases: canonical -> candidate filenames (no ext)
STEM_ALIASES: dict[str, list[str]] = {
    "dirt": ["dirt", "default_dirt", "terrain_dirt", "dirt_16x16"],
    "grass_side": ["grass_side", "default_grass_side", "dirt_grass", "grass_side_overlay"],
    "grass_top": ["grass_top", "default_grass", "grass_top_green", "grass1", "grass2"],
    "grass_top_green": ["grass_top", "default_grass", "grass_top_green", "grass1"],
    "stone": ["stone", "default_stone", "greystone", "stone_16x16", "rocks"],
    "sand": ["sand", "default_sand", "greysand", "sand_16x16"],
    "gravel": ["gravel", "default_gravel", "gravel_stone", "gravel_dirt", "gravel_16x16"],
    "glass": ["glass", "default_glass", "glass_frame"],
    "obsidian": ["obsidian", "default_obsidian"],
    "ice": ["ice", "default_ice"],
    "snow": ["snow", "default_snow", "dirt_snow"],
    "water": ["water", "default_water_source", "default_water_flowing", "water_16x16"],
    "lava": ["lava", "default_lava_source", "default_lava_flowing"],
    "oreCoal": ["oreCoal", "default_stone_with_coal", "coal_ore"],
    "oreIron": ["oreIron", "default_stone_with_iron", "iron_ore"],
    "oreGold": ["oreGold", "default_stone_with_gold", "gold_ore"],
    "oreDiamond": ["oreDiamond", "default_stone_with_diamond", "diamond_ore"],
    "tree_side": ["tree_side", "default_tree", "default_wood", "wood", "log_side"],
    "tree_top": ["tree_top", "default_tree_top", "wood_top", "log_top"],
    "leaves_opaque": ["leaves_opaque", "default_leaves", "leaves"],
    "hellrock": ["hellrock", "default_netherrack", "netherrack"],
    "hellsand": ["hellsand", "default_nether_sand", "soul_sand"],
    "whiteStone": ["whiteStone", "default_stone", "end_stone"],
    "cloth_0": ["cloth_0", "wool_white", "cotton_tan", "wool_colored_white"],
    "grass_block_top": ["grass_top", "default_grass", "grass_block_top"],
    "grass_block_side": ["grass_side", "default_grass_side", "grass_block_side"],
    "planks_oak": ["wood", "default_wood", "planks_oak", "oak_planks"],
    "log_oak": ["tree_side", "default_tree", "log_oak", "oak_log"],
    "log_oak_top": ["tree_top", "default_tree_top", "log_oak_top"],
    "lapis_ore": ["oreLapis", "default_stone_with_lapis", "lapis_ore"],
    "emerald_ore": ["oreEmerald", "emerald_ore"],
    "redstone_ore": ["oreRedstone", "redstone_ore"],
    "netherrack": ["hellrock", "default_netherrack", "netherrack"],
    "soul_sand": ["hellsand", "default_nether_sand", "soul_sand"],
    "end_stone": ["whiteStone", "end_stone"],
    "glowstone": ["lightgem", "glowstone", "default_meselamp"],
    "gold_block": ["blockGold", "default_gold_block", "gold_block"],
    "iron_block": ["blockIron", "default_steel_block", "iron_block"],
    "stone_bricks": ["stonebricksmooth", "default_stone_brick", "stone_bricks"],
    "mossy_stone_bricks": ["stonebricksmooth_mossy", "mossy_stone_bricks"],
    "cracked_stone_bricks": ["stonebricksmooth_cracked", "cracked_stone_bricks"],
    "chiseled_stone_bricks": ["stonebricksmooth_carved", "chiseled_stone_bricks"],
    "chiseled_sandstone": ["sandstone_carved", "chiseled_sandstone"],
    "smooth_sandstone": ["sandstone_smooth", "smooth_sandstone"],
    "coal_ore": ["oreCoal", "coal_ore"],
    "cobblestone": ["stone", "default_cobble", "cobblestone"],
}

CATEGORY_KEYWORDS = {
    "terrain": ["dirt", "grass", "stone", "sand", "gravel", "snow", "clay", "mycel", "farmland"],
    "wood": ["tree", "wood", "log", "plank", "leaves", "sapling"],
    "ore": ["ore", "coal", "iron", "gold", "diamond", "emerald", "lapis", "redstone", "quartz"],
    "fluid": ["water", "lava"],
    "plant": ["flower", "rose", "tallgrass", "fern", "crops", "carrot", "potato", "cactus", "vine", "cocoa"],
    "wool": ["cloth_", "wool"],
    "mechanism": ["piston", "rail", "repeater", "comparator", "redstone", "torch", "lever", "button", "door", "trapdoor"],
    "nether": ["hell", "nether", "glow", "lightgem"],
    "decorative": ["brick", "bookshelf", "wool", "stained", "glass", "wool"],
}


def md5_file(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def png_size_fast(path: Path) -> tuple[int, int] | None:
    try:
        with path.open("rb") as f:
            sig = f.read(8)
            if sig != b"\x89PNG\r\n\x1a\n":
                return None
            f.read(4)  # IHDR length
            chunk_type = f.read(4)
            if chunk_type != b"IHDR":
                return None
            w, h = struct.unpack(">II", f.read(8))
            return w, h
    except OSError:
        return None


def categorize_name(stem: str) -> str:
    low = stem.lower()
    for cat, keys in CATEGORY_KEYWORDS.items():
        if any(k in low for k in keys):
            return cat
    return "other"


def seamless_class(img: Image.Image, threshold_tight: float = 4.0, threshold_loose: float = 12.0) -> str:
    w, h = img.size
    if w != h or w < 4:
        return "n/a"
    px = img.convert("RGBA")
    left = [px.getpixel((0, y)) for y in range(h)]
    right = [px.getpixel((w - 1, y)) for y in range(h)]
    top = [px.getpixel((x, 0)) for x in range(w)]
    bottom = [px.getpixel((x, h - 1)) for x in range(w)]

    def edge_diff(a, b) -> float:
        if len(a) != len(b):
            return 999.0
        total = 0
        for p1, p2 in zip(a, b):
            total += sum(abs(int(p1[i]) - int(p2[i])) for i in range(3))
        return total / (len(a) * 3)

    lr = edge_diff(left, right)
    tb = edge_diff(top, bottom)
    score = max(lr, tb)
    if score <= threshold_tight:
        return "seamless"
    if score <= threshold_loose:
        return "likely-seamless"
    return "not-seamless"


def frame_tile_size(w: int, h: int) -> tuple[int, int, int]:
    if w <= 0:
        return 0, 0, 1
    frames = max(1, h // w) if h >= w else 1
    tile_h = h // frames if frames else h
    return w, tile_h, frames


def analyze_png(path: Path) -> dict:
    rel = path.name
    size = png_size_fast(path)
    info = {
        "path": str(path),
        "stem": path.stem,
        "name": rel,
        "md5": md5_file(path),
        "category": categorize_name(path.stem),
    }
    if not size:
        info.update({"width": 0, "height": 0, "tile_w": 0, "tile_h": 0, "frames": 0, "seamless": "n/a"})
        return info
    w, h = size
    tw, th, frames = frame_tile_size(w, h)
    info.update({"width": w, "height": h, "tile_w": tw, "tile_h": th, "frames": frames})
    try:
        with Image.open(path) as img:
            if tw == th and tw > 0:
                tile = img.crop((0, 0, tw, min(th, img.height)))
                info["seamless"] = seamless_class(tile)
            else:
                info["seamless"] = "n/a"
    except Exception:
        info["seamless"] = "n/a"
    return info


def index_pack(pack_dir: Path) -> dict[str, list[Path]]:
    idx: dict[str, list[Path]] = defaultdict(list)
    if not pack_dir.exists():
        return idx
    for png in pack_dir.rglob("*.png"):
        stem = png.stem.lower()
        idx[stem].append(png)
        # minetest default_dirt -> dirt
        if stem.startswith("default_"):
            idx[stem[8:]].append(png)
    return idx


def candidate_names(stem: str) -> list[str]:
    names = [stem.lower()]
    if stem in STEM_ALIASES:
        names.extend(s.lower() for s in STEM_ALIASES[stem])
    # generic variants
    names.append(stem.lower().replace("_", ""))
    if not stem.startswith("default_"):
        names.append(f"default_{stem.lower()}")
    # dedupe preserve order
    seen = set()
    out = []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def resolve_stem(stem: str, index: dict[str, list[Path]]) -> str | None:
    for name in candidate_names(stem):
        if name in index:
            return name
    # fuzzy: stem contained in filename
    for key in index:
        if stem.lower() in key or key in stem.lower():
            return key
    return None


def load_manifest_blocks() -> tuple[list[dict], set[str]]:
    manifests = [
        REPO / "tools/block_manifest.json",
        REPO / "tools/block_manifest_supplement.json",
        REPO / "tools/block_manifest_animated.json",
    ]
    blocks = []
    stems: set[str] = set()
    for mf in manifests:
        data = json.loads(mf.read_text(encoding="utf-8"))
        for block in data.get("blocks", []):
            blocks.append(block)
            if "uniform" in block:
                stems.add(block["uniform"])
            if "faces" in block:
                stems.update(block["faces"])
    return blocks, stems


def block_coverage(blocks: list[dict], pack_indexes: dict[str, dict[str, list[Path]]]) -> dict:
    matrix = {}
    for block in blocks:
        name = block["name"]
        if "uniform" in block:
            face_stems = [block["uniform"]] * 6
        else:
            faces = block["faces"]
            if len(faces) == 12:
                face_stems = faces[:6]
            else:
                face_stems = faces
        unique_stems = list(dict.fromkeys(face_stems))
        row = {"stems": unique_stems, "packs": {}}
        for pack_id, index in pack_indexes.items():
            resolved = {}
            for s in unique_stems:
                hit = resolve_stem(s, index)
                resolved[s] = hit
            found = sum(1 for s in unique_stems if resolved[s])
            if found == 0:
                status = "missing"
            elif found == len(unique_stems):
                status = "full"
            else:
                status = "partial"
            row["packs"][pack_id] = {"status": status, "resolved": resolved, "found": found, "total": len(unique_stems)}
        matrix[name] = row
    return matrix


def summarize_pack(pack_id: str, textures: list[dict]) -> dict:
    sizes = Counter((t["tile_w"], t["tile_h"]) for t in textures if t["tile_w"])
    seamless = Counter(t["seamless"] for t in textures)
    categories = Counter(t["category"] for t in textures)
    square = sum(1 for t in textures if t["tile_w"] == t["tile_h"] and t["tile_w"] > 0)
    return {
        "id": pack_id,
        "license": PACK_META.get(pack_id, {}).get("license", "?"),
        "source": PACK_META.get(pack_id, {}).get("source", "?"),
        "png_count": len(textures),
        "square_tile_count": square,
        "size_distribution": {f"{w}x{h}": c for (w, h), c in sizes.most_common(15)},
        "dominant_tile_size": sizes.most_common(1)[0][0] if sizes else (0, 0),
        "seamless": dict(seamless),
        "seamless_pct": round(100 * seamless.get("seamless", 0) / max(1, square), 1),
        "categories": dict(categories),
    }


def verify_origin(current_textures: list[dict]) -> dict:
    result = {
        "external_path": str(EXTERNAL),
        "external_exists": EXTERNAL.exists(),
        "repo_png_count": len(current_textures),
        "identical_to_external": 0,
        "same_name_diff_content": 0,
        "only_in_repo": [],
        "only_in_external": [],
        "minecraft_naming_indicators": [],
    }
    if not EXTERNAL.exists():
        return result

    repo_map = {t["stem"]: t for t in current_textures}
    ext_files = {p.stem: p for p in EXTERNAL.glob("*.png")}
    common = set(repo_map) & set(ext_files)
    for stem in common:
        if repo_map[stem]["md5"] == md5_file(ext_files[stem]):
            result["identical_to_external"] += 1
        else:
            result["same_name_diff_content"] += 1
    result["only_in_repo"] = sorted(set(repo_map) - set(ext_files))
    result["only_in_external"] = sorted(set(ext_files) - set(repo_map))[:20]

    mc_indicators = [
        "oreCoal", "hellrock", "whiteStone", "cloth_0", "comparator_lit",
        "tripWireSource", "netherBrick", "lightgem", "musicBlock",
    ]
    result["minecraft_naming_indicators"] = [s for s in mc_indicators if s in repo_map]

    # Optional remote compare (old MC naming)
    try:
        import requests

        remote_samples = {
            "dirt": "https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/1.12.2/assets/minecraft/textures/blocks/dirt.png",
            "stone": "https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/1.12.2/assets/minecraft/textures/blocks/stone.png",
            "grass_side": "https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/1.12.2/assets/minecraft/textures/blocks/grass_side.png",
            "oreCoal": "https://raw.githubusercontent.com/InventivetalentDev/minecraft-assets/1.12.2/assets/minecraft/textures/blocks/coal_ore.png",
        }
        remote_hits = {}
        session = requests.Session()
        for stem, url in remote_samples.items():
            local = CURRENT / f"{stem}.png"
            if stem == "oreCoal":
                local = CURRENT / "oreCoal.png"
            if not local.exists():
                continue
            r = session.get(url, timeout=30)
            if r.status_code == 200:
                remote_md5 = hashlib.md5(r.content).hexdigest()
                remote_hits[stem] = {
                    "local_md5": md5_file(local),
                    "remote_md5": remote_md5,
                    "identical": md5_file(local) == remote_md5,
                    "url": url,
                }
        result["minecraft_assets_1_12_compare"] = remote_hits
    except Exception as exc:
        result["minecraft_assets_1_12_compare_error"] = str(exc)

    return result


def pack_coverage_pct(matrix: dict, pack_id: str) -> dict:
    total = len(matrix)
    full = partial = missing = 0
    for row in matrix.values():
        st = row["packs"].get(pack_id, {}).get("status", "missing")
        if st == "full":
            full += 1
        elif st == "partial":
            partial += 1
        else:
            missing += 1
    return {
        "blocks_total": total,
        "full": full,
        "partial": partial,
        "missing": missing,
        "full_pct": round(100 * full / max(1, total), 1),
        "any_pct": round(100 * (full + partial) / max(1, total), 1),
    }


def stem_coverage(stems: set[str], pack_indexes: dict[str, dict[str, list[Path]]]) -> dict:
    out = {}
    for pack_id, index in pack_indexes.items():
        hit = sum(1 for s in stems if resolve_stem(s, index))
        out[pack_id] = {"hit": hit, "total": len(stems), "pct": round(100 * hit / max(1, len(stems)), 1)}
    return out


def write_csv_coverage(matrix: dict, pack_ids: list[str], path: Path) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["block"] + pack_ids)
        for block, row in sorted(matrix.items()):
            w.writerow([block] + [row["packs"].get(p, {}).get("status", "missing") for p in pack_ids])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--research", type=Path, default=RESEARCH)
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)

    blocks, stems = load_manifest_blocks()
    pack_textures: dict[str, list[dict]] = {}
    pack_indexes: dict[str, dict[str, list[Path]]] = {}

    for pack_id, pack_dir in PACK_DIRS.items():
        textures = [analyze_png(p) for p in sorted(pack_dir.rglob("*.png"))] if pack_dir.exists() else []
        pack_textures[pack_id] = textures
        pack_indexes[pack_id] = index_pack(pack_dir)

    summaries = {pid: summarize_pack(pid, tex) for pid, tex in pack_textures.items()}
    matrix = block_coverage(blocks, pack_indexes)
    stem_cov = stem_coverage(stems, pack_indexes)
    origin = verify_origin(pack_textures.get("cubatarium_current", []))

    pack_ids = list(PACK_DIRS.keys())
    coverage_stats = {pid: pack_coverage_pct(matrix, pid) for pid in pack_ids}

    result = {
        "blocks_count": len(blocks),
        "canonical_stems_count": len(stems),
        "pack_summaries": summaries,
        "stem_coverage": stem_cov,
        "block_coverage_stats": coverage_stats,
        "origin_verification": origin,
    }

    (out / "summary.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    (out / "pack_summaries.json").write_text(json.dumps(summaries, indent=2), encoding="utf-8")
    (out / "block_coverage.json").write_text(json.dumps(matrix, indent=2), encoding="utf-8")
    (out / "block_coverage_stats.json").write_text(json.dumps(coverage_stats, indent=2), encoding="utf-8")
    (out / "origin_verification.json").write_text(json.dumps(origin, indent=2), encoding="utf-8")
    (out / "stem_coverage.json").write_text(json.dumps(stem_cov, indent=2), encoding="utf-8")

    # per-pack texture inventories (compact)
    for pack_id, textures in pack_textures.items():
        compact = [{k: t[k] for k in ("stem", "tile_w", "tile_h", "frames", "seamless", "category")} for t in textures]
        (out / f"textures_{pack_id}.json").write_text(json.dumps(compact, indent=2), encoding="utf-8")

    write_csv_coverage(matrix, pack_ids, out / "block_coverage_matrix.csv")

    print(f"Analyzed {len(pack_ids)} packs, {len(blocks)} blocks, {len(stems)} stems")
    print(f"Output: {out}")
    for pid in pack_ids:
        cs = coverage_stats[pid]
        print(f"  {pid}: stems {stem_cov[pid]['pct']}% | blocks full {cs['full_pct']}%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
