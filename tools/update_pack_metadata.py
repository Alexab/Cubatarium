#!/usr/bin/env python3
"""Ensure pack.json has depends, conflicts, min_game_version, pack_format."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
DEFAULT_PACKS = REPO / "resource_packs"
DEPS_PATH = REPO / "tools" / "pack_dependencies.yaml"


def load_dependencies() -> dict[str, dict]:
    if not DEPS_PATH.is_file():
        return {}
    data = yaml.safe_load(DEPS_PATH.read_text(encoding="utf-8")) or {}
    packs = data.get("packs", {})
    return packs if isinstance(packs, dict) else {}


def apply_pack_policy(manifest: dict, pack_id: str, deps_map: dict) -> bool:
    """Merge depends/conflicts/worldgen_role from pack_dependencies.yaml."""
    changed = False
    policy = deps_map.get(pack_id, {})
    if pack_id in deps_map:
        exp_dep = policy.get("depends", [])
        exp_conf = policy.get("conflicts", [])
        if manifest.get("depends") != exp_dep:
            manifest["depends"] = list(exp_dep)
            changed = True
        if manifest.get("conflicts") != exp_conf:
            manifest["conflicts"] = list(exp_conf)
            changed = True
        role = policy.get("worldgen_role", "secondary")
        if manifest.get("worldgen_role") != role:
            manifest["worldgen_role"] = role
            changed = True
    elif manifest.get("worldgen_role") != "secondary":
        manifest["worldgen_role"] = "secondary"
        changed = True
    return changed


def update_pack_json(path: Path, pack_id: str, deps_map: dict, dry_run: bool) -> bool:
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
    if apply_pack_policy(manifest, pack_id, deps_map):
        changed = True
    if "min_game_version" not in manifest:
        manifest["min_game_version"] = "0.1.0"
        changed = True
    elif manifest.get("min_game_version") == "":
        manifest["min_game_version"] = "0.1.0"
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

    deps_map = load_dependencies()
    count = 0
    for pack_dir in sorted(args.packs_dir.resolve().iterdir()):
        if not pack_dir.is_dir() or pack_dir.name.startswith("_"):
            continue
        pack_json = pack_dir / "pack.json"
        if not pack_json.is_file():
            continue
        try:
            pack_id = json.loads(pack_json.read_text(encoding="utf-8-sig")).get("id", pack_dir.name)
        except json.JSONDecodeError:
            pack_id = pack_dir.name
        if update_pack_json(pack_json, pack_id, deps_map, args.dry_run):
            print(f"updated {pack_dir.name}/pack.json")
            count += 1
    print(f"Done — {count} pack.json files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
