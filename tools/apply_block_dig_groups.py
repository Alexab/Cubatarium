#!/usr/bin/env python3
"""Apply default dig.groups / dig.level from block_dig_groups_defaults.yaml to pack block JSON.

Lookup order per block name: exact map, then fnmatch patterns (first hit), then
hardness-based fallback mirroring InferDigGroups. Rerunning is a no-op when the
dig object already matches the table (unless --force).
"""

from __future__ import annotations

import argparse
import fnmatch
import json
from collections import OrderedDict
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
DEFAULT_PACKS = REPO / "resource_packs"
DEFAULT_TABLE = Path(__file__).resolve().parent / "block_dig_groups_defaults.yaml"


def _normalize_groups(groups: dict[str, Any]) -> "OrderedDict[str, int]":
    out: "OrderedDict[str, int]" = OrderedDict()
    for key in sorted(groups.keys()):
        out[str(key)] = int(groups[key])
    return out


def _dig_entry(level: int, groups: dict[str, Any]) -> "OrderedDict[str, Any]":
    entry: "OrderedDict[str, Any]" = OrderedDict()
    entry["level"] = int(level)
    entry["groups"] = _normalize_groups(groups)
    return entry


class DigGroupsTable:
    def __init__(self, data: dict[str, Any]) -> None:
        fb = data.get("fallback") or {}
        self.fallback = _dig_entry(int(fb.get("level", 0)), fb.get("groups") or {"cracky": 2})
        self.exact: dict[str, OrderedDict[str, Any]] = {}
        for name, raw in (data.get("exact") or {}).items():
            self.exact[str(name)] = _dig_entry(int(raw.get("level", 0)), raw.get("groups") or {})
        self.patterns: list[tuple[str, OrderedDict[str, Any]]] = []
        for rule in data.get("patterns") or []:
            dig = rule.get("dig") or {}
            self.patterns.append(
                (str(rule["match"]), _dig_entry(int(dig.get("level", 0)), dig.get("groups") or {}))
            )

    def lookup(self, name: str, hardness: float | None) -> tuple[OrderedDict[str, Any], str]:
        if name in self.exact:
            return self.exact[name], "exact"
        for pattern, value in self.patterns:
            if fnmatch.fnmatchcase(name, pattern):
                return value, f"pattern:{pattern}"
        # Hardness-based fallback (same buckets as InferDigGroups).
        if hardness is not None:
            if hardness <= 0.4:
                return _dig_entry(0, {"crumbly": 3, "oddly_breakable_by_hand": 3}), "hardness:soft"
            if hardness <= 1.2:
                return _dig_entry(0, {"cracky": 3}), "hardness:med_soft"
            if hardness <= 2.5:
                return _dig_entry(0, {"cracky": 2}), "hardness:med"
            return _dig_entry(0, {"cracky": 1}), "hardness:hard"
        return self.fallback, "fallback"


def load_table(path: Path) -> DigGroupsTable:
    return DigGroupsTable(yaml.safe_load(path.read_text(encoding="utf-8")) or {})


def with_dig(block: dict[str, Any], dig: OrderedDict[str, Any]) -> "OrderedDict[str, Any]":
    """Rebuild block dict with `dig` after `hardness` (or after id/name)."""
    out: "OrderedDict[str, Any]" = OrderedDict()
    placed = False
    for key, value in block.items():
        if key == "dig":
            continue
        out[key] = value
        if key == "hardness":
            out["dig"] = dig
            placed = True
    if not placed:
        anchor = "id" if "id" in out else ("name" if "name" in out else None)
        if anchor is None:
            out["dig"] = dig
        else:
            rebuilt: "OrderedDict[str, Any]" = OrderedDict()
            for key, value in out.items():
                rebuilt[key] = value
                if key == anchor:
                    rebuilt["dig"] = dig
            out = rebuilt
    return out


def process_file(
    path: Path, table: DigGroupsTable, dry_run: bool, keep_existing: bool, force: bool
) -> tuple[str, OrderedDict[str, Any]] | None:
    try:
        block = json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=OrderedDict)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  skip {path}: {exc}")
        return None
    name = str(block.get("name") or path.stem)
    if keep_existing and "dig" in block and not force:
        return None
    hardness = block.get("hardness")
    hardness_f = float(hardness) if isinstance(hardness, (int, float)) else None
    dig, _src = table.lookup(name, hardness_f)
    before = json.dumps(block)
    updated = with_dig(block, dig)
    after = json.dumps(updated)
    if before == after and not force:
        return None
    if not dry_run:
        path.write_text(json.dumps(updated, indent=2) + "\n", encoding="utf-8")
    return name, dig


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packs-dir", type=Path, default=DEFAULT_PACKS)
    parser.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    parser.add_argument("--pack", type=str, default=None, help="Single pack id folder name")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--keep-existing", action="store_true", help="Do not touch blocks that already have dig"
    )
    parser.add_argument("--force", action="store_true", help="Rewrite dig even when present")
    args = parser.parse_args()

    table = load_table(args.table)
    packs_dir = args.packs_dir.resolve()
    if args.pack:
        pack_dirs = [packs_dir / args.pack]
    else:
        pack_dirs = [p for p in sorted(packs_dir.iterdir()) if (p / "blocks").is_dir()]

    changed_files = 0
    changed_names: dict[str, OrderedDict[str, Any]] = {}
    for pack_dir in pack_dirs:
        pack_changed = 0
        for block_path in sorted((pack_dir / "blocks").glob("*.json")):
            result = process_file(
                block_path, table, args.dry_run, args.keep_existing, args.force
            )
            if result is not None:
                pack_changed += 1
                changed_names[result[0]] = result[1]
        if pack_changed:
            changed_files += pack_changed
            print(f"{pack_dir.name}: {pack_changed} block files")

    verb = "would change" if args.dry_run else "updated"
    print(f"Done — {changed_files} block files {verb}, {len(changed_names)} unique block names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
