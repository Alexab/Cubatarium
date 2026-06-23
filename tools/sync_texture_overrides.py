#!/usr/bin/env python3
"""Convert texture_overrides.yaml to texture_overrides.json in resource packs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
DEFAULT_PACKS = REPO / "resource_packs"


def check_pack(pack_dir: Path) -> str | None:
    """Return error message if yaml/json out of sync, else None."""
    yaml_path = pack_dir / "texture_overrides.yaml"
    if not yaml_path.is_file():
        return None
    data = yaml.safe_load(yaml_path.read_text(encoding="utf-8")) or {}
    json_path = pack_dir / "texture_overrides.json"
    payload = json.dumps(data, indent=2) + "\n"
    if not json_path.is_file():
        return f"{pack_dir.name}: missing texture_overrides.json (run sync_texture_overrides.py)"
    if json_path.read_text(encoding="utf-8") != payload:
        return f"{pack_dir.name}: texture_overrides.yaml/json out of sync"
    return None


def sync_pack(pack_dir: Path, dry_run: bool) -> bool:
    yaml_path = pack_dir / "texture_overrides.yaml"
    if not yaml_path.is_file():
        return False
    data = yaml.safe_load(yaml_path.read_text(encoding="utf-8")) or {}
    json_path = pack_dir / "texture_overrides.json"
    payload = json.dumps(data, indent=2) + "\n"
    if json_path.is_file() and json_path.read_text(encoding="utf-8") == payload:
        return False
    if not dry_run:
        json_path.write_text(payload, encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync texture_overrides.yaml → .json")
    parser.add_argument("--packs-dir", type=Path, default=DEFAULT_PACKS)
    parser.add_argument("--pack", type=str, default=None)
    parser.add_argument("--check", action="store_true", help="Verify yaml/json sync only")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    packs_dir = args.packs_dir.resolve()
    targets = [packs_dir / args.pack] if args.pack else sorted(packs_dir.iterdir())
    if args.check:
        errors = []
        for pack_dir in targets:
            if not pack_dir.is_dir():
                continue
            err = check_pack(pack_dir)
            if err:
                errors.append(err)
        if errors:
            for e in errors:
                print(f"ERROR: {e}")
            return 1
        print("texture_overrides sync OK")
        return 0
    count = 0
    for pack_dir in targets:
        if not pack_dir.is_dir():
            continue
        if sync_pack(pack_dir, args.dry_run):
            print(f"synced {pack_dir.name}")
            count += 1
    print(f"Done — {count} packs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
