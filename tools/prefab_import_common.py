#!/usr/bin/env python3
"""Shared helpers for Minetest schematic import."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
NODE_MAP_PATH = REPO / "tools" / "minetest_node_map.yaml"
CANONICAL_PATH = REPO / "tools" / "canonical_blocks.yaml"
PACKS_DIR = REPO / "resource_packs"


def load_node_map(path: Path = NODE_MAP_PATH) -> tuple[set[str], dict[str, str], str]:
    data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    skip = set(data.get("skip_nodes") or [])
    mappings = dict(data.get("mappings") or {})
    unknown = str(data.get("unknown_policy") or "skip")
    return skip, mappings, unknown


def resolve_block(node_name: str, skip: set[str], mappings: dict[str, str], unknown: str) -> str | None:
    if node_name in skip:
        return None
    if node_name in mappings:
        return mappings[node_name]
    bare = node_name.split(":", 1)[-1]
    for key, value in mappings.items():
        if key.split(":", 1)[-1] == bare:
            return value
    if unknown == "stone":
        return "stone"
    return None


def load_known_block_names() -> set[str]:
    names: set[str] = set()
    canonical = yaml.safe_load(CANONICAL_PATH.read_text(encoding="utf-8")) or {}
    names.update((canonical.get("tier_a") or {}).keys())
    names.update((canonical.get("tier_b") or {}).keys())
    if PACKS_DIR.is_dir():
        for pack in PACKS_DIR.iterdir():
            blocks_dir = pack / "blocks"
            if not blocks_dir.is_dir():
                continue
            for path in blocks_dir.glob("*.json"):
                try:
                    data = json.loads(path.read_text(encoding="utf-8-sig"))
                except (OSError, json.JSONDecodeError):
                    continue
                if data.get("name"):
                    names.add(data["name"])
    return names


def load_manifest(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def manifest_entry(manifest: dict[str, Any], prefab_name: str) -> dict[str, Any]:
    return (manifest.get("prefabs") or {}).get(prefab_name) or {}
