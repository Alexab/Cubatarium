#!/usr/bin/env python3
"""Drop on-disk creature icon cache entries and re-sync bin/models for species."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def clear_icon_cache(species: list[str], all_creatures: bool) -> None:
    cache_dir = ROOT / "bin" / "cache" / "icons"
    manifest_path = cache_dir / "manifest.json"
    if not manifest_path.is_file():
        print(f"skip icons: no {manifest_path}")
        return

    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    targets = set(species)
    removed: list[str] = []
    for key in list(data.keys()):
        if not key.startswith("creature:"):
            continue
        sid = key.split(":", 1)[1]
        if all_creatures or sid in targets:
            entry = data.pop(key)
            png = cache_dir / entry.get("file", "")
            if png.is_file():
                png.unlink()
            removed.append(sid)

    manifest_path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    if removed:
        print(f"cleared icon cache: {', '.join(sorted(set(removed)))}")
    else:
        print("icon cache: nothing to remove")


def sync_bin_models(species: list[str]) -> None:
    bin_root = ROOT / "bin" / "models" / "creatures"
    if not bin_root.parent.is_dir():
        print("skip bin/models sync: bin/models missing")
        return
    for sid in species:
        src = ROOT / "models" / "creatures" / sid
        dst = bin_root / sid
        if not src.is_dir():
            print(f"warn: missing {src}", file=sys.stderr)
            continue
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
        print(f"synced bin/models/creatures/{sid}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--species",
        action="append",
        default=[],
        help="creature id (repeatable)",
    )
    parser.add_argument(
        "--all-creatures",
        action="store_true",
        help="clear every creature:* icon cache entry",
    )
    parser.add_argument("--no-sync", action="store_true", help="skip bin/models copy")
    args = parser.parse_args()

    species = args.species or ["stingray", "seahorse"]
    clear_icon_cache(species, args.all_creatures)
    if not args.no_sync:
        sync_bin_models(species)

    print()
    print("Next steps:")
    print("  1. Fully quit Cubatarium.exe (not just reload world).")
    print("  2. Remove old stingray/seahorse from the world; spawn fresh mobs.")
    print("  3. Bind-pose shape is unchanged; check swim animation (fly clip).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
