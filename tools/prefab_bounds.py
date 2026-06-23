#!/usr/bin/env python3
"""Compute prefab bounding boxes and block-type stats from blocks[]."""

from __future__ import annotations

from typing import Any


def prefab_bounds(blocks: list[dict[str, Any]]) -> dict[str, Any]:
    if not blocks:
        return {
            "min_dx": 0,
            "max_dx": 0,
            "min_dy": 0,
            "max_dy": 0,
            "min_dz": 0,
            "max_dz": 0,
            "size_x": 0,
            "size_y": 0,
            "size_z": 0,
            "block_count": 0,
            "block_types": set(),
            "has_log": False,
            "has_cactus": False,
        }

    dxs = [int(b["dx"]) for b in blocks]
    dys = [int(b["dy"]) for b in blocks]
    dzs = [int(b["dz"]) for b in blocks]
    types = {str(b["type"]) for b in blocks if b.get("type")}

    min_dx, max_dx = min(dxs), max(dxs)
    min_dy, max_dy = min(dys), max(dys)
    min_dz, max_dz = min(dzs), max(dzs)

    return {
        "min_dx": min_dx,
        "max_dx": max_dx,
        "min_dy": min_dy,
        "max_dy": max_dy,
        "min_dz": min_dz,
        "max_dz": max_dz,
        "size_x": max_dx - min_dx + 1,
        "size_y": max_dy - min_dy + 1,
        "size_z": max_dz - min_dz + 1,
        "block_count": len(blocks),
        "block_types": types,
        "has_log": "tree_log" in types,
        "has_cactus": any(t == "cactus" or t.startswith("cactus") for t in types),
    }


def format_size(bounds: dict[str, Any]) -> str:
    return f"{bounds['size_x']}x{bounds['size_y']}x{bounds['size_z']}"
