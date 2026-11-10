#!/usr/bin/env python3
"""Apply default `hardness` values from block_hardness_defaults.yaml to pack block JSON.

Lookup order per block name: exact map, then fnmatch patterns (first hit), then fallback.
Rerunning is a no-op: the table stays authoritative and key placement is deterministic.
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
DEFAULT_TABLE = Path(__file__).resolve().parent / "block_hardness_defaults.yaml"


class HardnessTable:
    def __init__(self, data: dict[str, Any]) -> None:
        self.fallback = float(data.get("fallback", 1.0))
        self.exact = {str(k): float(v) for k, v in (data.get("exact") or {}).items()}
        self.patterns: list[tuple[str, float]] = []
        for rule in data.get("patterns") or []:
            self.patterns.append((str(rule["match"]), float(rule["hardness"])))

    def lookup(self, name: str) -> tuple[float, str]:
        if name in self.exact:
            return self.exact[name], "exact"
        for pattern, value in self.patterns:
            if fnmatch.fnmatchcase(name, pattern):
                return value, f"pattern:{pattern}"
        return self.fallback, "fallback"


def load_table(path: Path) -> HardnessTable:
    return HardnessTable(yaml.safe_load(path.read_text(encoding="utf-8")) or {})


def normalize(value: float) -> float | int:
    """Keep whole numbers as ints so the JSON stays readable (2 instead of 2.0)."""
    return int(value) if float(value).is_integer() else round(float(value), 4)


def with_hardness(block: dict[str, Any], hardness: float | int) -> "OrderedDict[str, Any]":
    """Rebuild the block dict with `hardness` right after `id` (or `name`)."""
    anchor = "id" if "id" in block else "name"
    out: "OrderedDict[str, Any]" = OrderedDict()
    for key, value in block.items():
        if key == "hardness":
            continue
        out[key] = value
        if key == anchor:
            out["hardness"] = hardness
    if "hardness" not in out:
        out["hardness"] = hardness
        out.move_to_end("hardness", last=False)
    return out


def process_file(
    path: Path, table: HardnessTable, dry_run: bool, keep_existing: bool
) -> tuple[str, float | int] | None:
    """Return (block name, hardness) when the file changed, else None."""
    try:
        block = json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=OrderedDict)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  skip {path}: {exc}")
        return None
    name = block.get("name") or path.stem
    if keep_existing and "hardness" in block:
        return None
    hardness = normalize(table.lookup(str(name))[0])
    before = json.dumps(block)
    updated = with_hardness(block, hardness)
    after = json.dumps(updated)
    if before == after:
        return None
    if not dry_run:
        path.write_text(json.dumps(updated, indent=2) + "\n", encoding="utf-8")
    return str(name), hardness


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packs-dir", type=Path, default=DEFAULT_PACKS)
    parser.add_argument("--table", type=Path, default=DEFAULT_TABLE)
    parser.add_argument("--pack", type=str, default=None, help="Single pack id folder name")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--keep-existing", action="store_true", help="Do not touch blocks that already have hardness"
    )
    parser.add_argument("--report-fallback", action="store_true", help="List names hitting fallback")
    args = parser.parse_args()

    table = load_table(args.table)
    packs_dir = args.packs_dir.resolve()
    if args.pack:
        pack_dirs = [packs_dir / args.pack]
    else:
        pack_dirs = [p for p in sorted(packs_dir.iterdir()) if (p / "blocks").is_dir()]

    changed_files = 0
    changed_names: dict[str, float | int] = {}
    seen_names: set[str] = set()
    for pack_dir in pack_dirs:
        pack_changed = 0
        for block_path in sorted((pack_dir / "blocks").glob("*.json")):
            seen_names.add(block_path.stem)
            result = process_file(block_path, table, args.dry_run, args.keep_existing)
            if result is not None:
                pack_changed += 1
                changed_names[result[0]] = result[1]
        if pack_changed:
            changed_files += pack_changed
            print(f"{pack_dir.name}: {pack_changed} block files")

    if args.report_fallback:
        fallback_names = sorted(n for n in seen_names if table.lookup(n)[1] == "fallback")
        print(f"fallback names ({len(fallback_names)}): {', '.join(fallback_names) or '-'}")

    verb = "would change" if args.dry_run else "updated"
    print(f"Done — {changed_files} block files {verb}, {len(changed_names)} unique block names")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
