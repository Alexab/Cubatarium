#!/usr/bin/env python3
"""Generate tools/minetest_mapping.yaml from Minetest textures + block manifests."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
MT_TEX = RESEARCH / "minetest_default" / "textures"

sys.path.insert(0, str(REPO / "tools"))
from analyze_texture_packs import index_pack, resolve_stem  # noqa: E402


def load_manifest_blocks() -> list[dict]:
    blocks: list[dict] = []
    for mf in [
        REPO / "tools/block_manifest.json",
        REPO / "tools/block_manifest_supplement.json",
        REPO / "tools/block_manifest_animated.json",
    ]:
        data = json.loads(mf.read_text(encoding="utf-8"))
        blocks.extend(data.get("blocks", []))
    return blocks


def block_spec(entry: dict) -> dict | str:
    if "uniform" in entry:
        stem = entry["uniform"]
        if entry.get("physics_preset") or entry.get("render_transparent"):
            spec: dict = {
                "faces": [stem] * 6,
                "types": entry.get("types", ["natural"]),
            }
            if entry.get("physics_preset"):
                spec["physics"] = {"preset": entry["physics_preset"]}
            if entry.get("render_transparent"):
                spec["render"] = {"transparent": True, "style": "fluid" if entry["physics_preset"] in ("water", "lava") else None}
                if spec["render"]["style"] is None:
                    del spec["render"]["style"]
            if entry["name"] in ("water", "lava"):
                spec["animation"] = {"frame_count": 16 if entry["name"] == "water" else 8, "frametime": 2}
            return spec
        return stem
    faces = entry.get("faces", [])
    spec = {"faces": faces, "types": entry.get("types", ["natural"])}
    if "physics" in entry:
        spec["physics"] = entry["physics"]
    elif entry.get("physics_preset"):
        spec["physics"] = {"preset": entry["physics_preset"]}
    if "render" in entry:
        spec["render"] = entry["render"]
    elif entry.get("render_transparent"):
        spec["render"] = {"transparent": True}
    if "animation" in entry:
        spec["animation"] = entry["animation"]
    return spec


def face_stems(entry: dict) -> list[str]:
    if "uniform" in entry:
        return [entry["uniform"]] * 6
    faces = entry["faces"]
    return faces[:6] if len(faces) >= 6 else faces


def main() -> int:
    if not MT_TEX.is_dir():
        raise SystemExit(f"Missing {MT_TEX} — run download_texture_packs.py --pack minetest_default")

    index = index_pack(MT_TEX)
    blocks_out: dict = {}
    textures_out: dict[str, str] = {}
    skipped: list[str] = []

    for entry in load_manifest_blocks():
        name = entry["name"]
        stems = face_stems(entry)
        resolved: dict[str, str | None] = {}
        for stem in set(stems):
            hit = resolve_stem(stem, index)
            if hit and index[hit]:
                rel = index[hit][0].relative_to(MT_TEX).as_posix()
                resolved[stem] = rel
            else:
                resolved[stem] = None

        if any(v is None for v in resolved.values()):
            skipped.append(name)
            continue

        blocks_out[name] = block_spec(entry)
        for stem, rel in resolved.items():
            if stem not in textures_out and rel:
                textures_out[stem] = rel

    # Animated fluids use MT vertical strips as-is (frame_count inferred at build time).
    animated = {}
    for stem, mt_name in [
        ("water", "default_water_source_animated.png"),
        ("lava", "default_lava_source_animated.png"),
    ]:
        path = MT_TEX / mt_name
        if path.is_file() and "water" in blocks_out:
            animated[stem] = [mt_name]
        elif path.is_file():
            pass

    out = {
        "license": "CC-BY-SA-3.0",
        "license_text": (
            "Minetest Game default textures (CC BY-SA 3.0).\n"
            "Copyright (c) celeron55, Perttu Ahola et al.\n"
            "Source: https://github.com/minetest-game/default\n"
            "Modifications and repackaging under the same license.\n"
        ),
        "blocks": blocks_out,
        "textures": textures_out,
        "animated": animated,
    }

    path = REPO / "tools" / "minetest_mapping.yaml"
    path.write_text(
        yaml.dump(out, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(f"Wrote {path}: {len(blocks_out)} blocks, {len(textures_out)} textures, skipped {len(skipped)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
