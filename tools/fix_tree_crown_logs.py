#!/usr/bin/env python3
"""Remove or soften tree_log voxels in tree crown areas of mapgen prefabs.

Central trunk keeps tree_log only below the lowest leaf layer.
Offset tree_log at/above crown height becomes tree_leaves (branches read as foliage).
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PREFABS = ROOT / "prefabs"


def load_blocks(path: Path) -> tuple[dict, list[dict]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    blocks = data.get("blocks", [])
    return data, blocks


def min_leaf_dy(blocks: list[dict]) -> int | None:
    leaf_dys = [b["dy"] for b in blocks if b.get("type") == "tree_leaves"]
    return min(leaf_dys) if leaf_dys else None


def fix_blocks(blocks: list[dict]) -> tuple[list[dict], dict]:
    crown_start = min_leaf_dy(blocks)
    if crown_start is None:
        return blocks, {"skipped": True, "reason": "no_leaves"}

    key_to_type = {
        (b["dx"], b["dy"], b["dz"]): b.get("type") for b in blocks
    }
    removed_center = 0
    converted_branch = 0

    new_blocks: list[dict] = []
    for b in blocks:
        if b.get("type") != "tree_log":
            new_blocks.append(b)
            continue

        dy = b["dy"]
        dx, dz = b["dx"], b["dz"]
        is_center = dx == 0 and dz == 0

        if is_center:
            if dy >= crown_start:
                removed_center += 1
                continue
            new_blocks.append(b)
            continue

        if dy >= crown_start:
            converted_branch += 1
            key = (dx, dy, dz)
            if key_to_type.get(key) == "tree_leaves":
                continue
            new_blocks.append(
                {"dx": dx, "dy": dy, "dz": dz, "type": "tree_leaves"}
            )
            continue

        new_blocks.append(b)

    stats = {
        "crown_start": crown_start,
        "removed_center": removed_center,
        "converted_branch": converted_branch,
        "blocks_before": len(blocks),
        "blocks_after": len(new_blocks),
    }
    return new_blocks, stats


def audit(path: Path) -> dict:
    _, blocks = load_blocks(path)
    crown_start = min_leaf_dy(blocks)
    crown_logs = [
        b
        for b in blocks
        if b.get("type") == "tree_log"
        and crown_start is not None
        and b["dy"] >= crown_start
    ]
    center_crown = [b for b in crown_logs if b["dx"] == 0 and b["dz"] == 0]
    branch_crown = [b for b in crown_logs if b["dx"] != 0 or b["dz"] != 0]
    return {
        "file": path.name,
        "crown_start": crown_start,
        "crown_log_total": len(crown_logs),
        "center_crown_logs": len(center_crown),
        "branch_crown_logs": len(branch_crown),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--glob",
        default="tree_*_mapgen.json",
        help="Prefab glob under prefabs/",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="Apply fixes in place",
    )
    parser.add_argument(
        "--audit-only",
        action="store_true",
        help="Print audit table only",
    )
    args = parser.parse_args()

    paths = sorted(PREFABS.glob(args.glob))
    if not paths:
        print(f"No files matching {args.glob}")
        return 1

    print("=== Crown tree_log audit (dy >= min leaf dy) ===")
    for path in paths:
        row = audit(path)
        print(
            f"{row['file']:36} crown@dy={row['crown_start']} "
            f"logs={row['crown_log_total']:3} "
            f"(center={row['center_crown_logs']}, branch={row['branch_crown_logs']})"
        )

    if args.audit_only:
        return 0

    if not args.write:
        print("\nDry run (use --write to apply):")
    else:
        print("\nApplying fixes:")

    for path in paths:
        data, blocks = load_blocks(path)
        fixed, stats = fix_blocks(blocks)
        if stats.get("skipped"):
            print(f"  {path.name}: skipped ({stats['reason']})")
            continue
        print(
            f"  {path.name}: crown@dy={stats['crown_start']} "
            f"removed_center={stats['removed_center']} "
            f"converted_branch={stats['converted_branch']} "
            f"blocks {stats['blocks_before']} -> {stats['blocks_after']}"
        )
        if args.write:
            data["blocks"] = fixed
            path.write_text(
                json.dumps(data, indent=2, ensure_ascii=False) + "\n",
                encoding="utf-8",
            )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
