#!/usr/bin/env python3
"""Parse minetest-game/default Lua sources into upstream block catalog YAML."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

import yaml

from stem_mapping_common import RESEARCH, mt_tiles_to_cubatarium_faces, parse_mtg_tile_ref

REPO = Path(__file__).resolve().parents[1]
MT_ROOT = RESEARCH / "minetest_default"
OUT = REPO / "tools" / "minetest_upstream_blocks.yaml"

LUA_FILES = [
    "nodes.lua",
    "torch.lua",
    "trees.lua",
    "chests.lua",
    "tools.lua",
    "furnace.lua",
    "mapgen.lua",
    "craftitems.lua",
]


def strip_lua_comments(text: str) -> str:
    text = re.sub(r"--\[\[.*?\]\]", "", text, flags=re.DOTALL)
    text = re.sub(r"--.*$", "", text, flags=re.MULTILINE)
    return text


def extract_tiles(block_body: str) -> list[str] | None:
    m = re.search(r"tiles\s*=\s*\{", block_body)
    if not m:
        single = re.search(r'tiles\s*=\s*"([^"]+)"', block_body)
        if single:
            return [single.group(1)] * 6
        return None
    start = m.end()
    depth = 1
    i = start
    while i < len(block_body) and depth:
        if block_body[i] == "{":
            depth += 1
        elif block_body[i] == "}":
            depth -= 1
        i += 1
    inner = block_body[start : i - 1]
    tiles: list[str] = []
    for part in re.split(r",(?=(?:[^\"']*[\"'][^\"']*[\"'])*[^\"']*$)", inner):
        part = part.strip()
        if not part:
            continue
        sm = re.search(r'"([^"]+)"', part)
        if sm:
            tiles.append(sm.group(1))
    return tiles or None


def parse_register_nodes(lua_text: str) -> list[tuple[str, list[str]]]:
    lua_text = strip_lua_comments(lua_text)
    results: list[tuple[str, list[str]]] = []
    for m in re.finditer(
        r'minetest\.register_node\s*\(\s*"([^"]+)"\s*,\s*\{',
        lua_text,
    ):
        node_id = m.group(1)
        if not node_id.startswith("default:"):
            continue
        start = m.end() - 1
        depth = 0
        i = start
        while i < len(lua_text):
            ch = lua_text[i]
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    body = lua_text[start + 1 : i]
                    tiles = extract_tiles(body)
                    if tiles:
                        results.append((node_id, tiles))
                    break
            i += 1
    return results


def ref_to_stem(block_name: str, ref: Any, cache: dict[str, str]) -> str:
    key = yaml.dump(ref, default_flow_style=True).strip()
    if key in cache:
        return cache[key]
    if isinstance(ref, str):
        stem = f"{block_name}_{Path(ref).stem}"
    else:
        stem = f"{block_name}_composite_{len(cache)}"
    cache[key] = stem
    return stem


def tiles_to_block_spec(block_name: str, tiles: list[str], textures: dict[str, Any]) -> dict | None:
    cuba_tiles = mt_tiles_to_cubatarium_faces(list(tiles))
    cache: dict[str, str] = {}
    face_stems: list[str] = []
    for tile in cuba_tiles:
        ref = parse_mtg_tile_ref(tile)
        stem = ref_to_stem(block_name, ref, cache)
        face_stems.append(stem)
        textures.setdefault(stem, ref)
    if len(set(face_stems)) == 1:
        return {"uniform": face_stems[0], "types": ["building"]}
    return {"faces": face_stems, "types": ["building"]}


def main() -> int:
    from stem_mapping_common import load_manifest_blocks

    existing = {b["name"] for b in load_manifest_blocks()}
    all_nodes: dict[str, dict] = {}
    texture_refs: dict[str, Any] = {}

    for lua_name in LUA_FILES:
        path = MT_ROOT / lua_name
        if not path.is_file():
            print(f"skip missing {path}")
            continue
        nodes = parse_register_nodes(path.read_text(encoding="utf-8", errors="replace"))
        print(f"{lua_name}: {len(nodes)} nodes")
        for node_id, tiles in nodes:
            short = node_id.split(":", 1)[-1]
            block_name = f"mtg_{short}"
            if block_name in existing or block_name in all_nodes:
                continue
            spec = tiles_to_block_spec(block_name, tiles, texture_refs)
            if spec:
                all_nodes[block_name] = spec

    out = {
        "blocks": all_nodes,
        "textures": texture_refs,
        "node_count": len(all_nodes),
    }
    OUT.write_text(
        yaml.dump(out, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(f"Wrote {OUT}: {len(all_nodes)} blocks, {len(texture_refs)} texture refs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
