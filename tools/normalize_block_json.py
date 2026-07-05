#!/usr/bin/env python3
"""Normalize block JSON under resource_packs (strip UTF-8 BOM, optional reformat)."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BLOCKS_GLOB = "resource_packs/**/blocks/*.json"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def normalize_file(path: Path, write: bool, write_all: bool) -> bool:
    raw = path.read_bytes()
    had_bom = raw.startswith(b"\xef\xbb\xbf")
    try:
        data = json.loads(raw.decode("utf-8-sig"))
    except json.JSONDecodeError as exc:
        print(f"SKIP invalid JSON {path}: {exc}")
        return False
    if not had_bom and not write_all:
        return False
    if write:
        path.write_text(
            json.dumps(data, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--strip-bom",
        action="store_true",
        help="Rewrite files that had UTF-8 BOM (or all with --write-all)",
    )
    parser.add_argument(
        "--write-all",
        action="store_true",
        help="Reformat every block JSON (use with care)",
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not args.strip_bom and not args.write_all:
        parser.error("pass --strip-bom and/or --write-all")

    write = not args.dry_run
    changed = 0
    for path in sorted(REPO_ROOT.glob(BLOCKS_GLOB)):
        if normalize_file(path, write=write, write_all=args.write_all):
            changed += 1
            action = "would update" if args.dry_run else "updated"
            print(f"{action}: {path.relative_to(REPO_ROOT)}")
    print(f"done ({changed} file(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
