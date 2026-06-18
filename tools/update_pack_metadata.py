#!/usr/bin/env python3
"""Ensure pack.json has depends, conflicts, min_game_version, pack_format."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_PACKS = REPO / "resource_packs"


def update_pack_json(path: Path, dry_run: bool) -> bool:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError):
        return False
    changed = False
    if "depends" not in manifest:
        manifest["depends"] = []
        changed = True
    if "conflicts" not in manifest:
        manifest["conflicts"] = []
        changed = True
    if "min_game_version" not in manifest:
        manifest["min_game_version"] = ""
        changed = True
    if manifest.get("pack_format") != 1:
        manifest["pack_format"] = 1
        changed = True
    if changed and not dry_run:
        path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="Normalize pack.json metadata fields")
    parser.add_argument("--packs-dir", type=Path, default=DEFAULT_PACKS)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    count = 0
    for pack_dir in sorted(args.packs_dir.resolve().iterdir()):
        if not pack_dir.is_dir() or pack_dir.name.startswith("_"):
            continue
        pack_json = pack_dir / "pack.json"
        if pack_json.is_file() and update_pack_json(pack_json, args.dry_run):
            print(f"updated {pack_dir.name}/pack.json")
            count += 1
    print(f"Done — {count} pack.json files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
