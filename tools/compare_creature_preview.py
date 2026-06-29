#!/usr/bin/env python3
"""Perceptual hash comparison for creature preview-smoke PNGs."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from creature_tier_a import TIER_A_MOBS
from creature_uv_common import list_all_species, profile_thresholds


def phash(img_path: Path) -> int:
    from PIL import Image

    img = Image.open(img_path).convert("L").resize((32, 32))
    px = list(img.getdata())
    avg = sum(px) / len(px)
    bits = 0
    for i, p in enumerate(px):
        if p >= avg:
            bits |= 1 << i
    return bits


def hamming(a: int, b: int) -> int:
    return (a ^ b).bit_count()


def compare_dir(species_id: str, current: Path, baseline: Path, max_h: int) -> tuple[bool, str]:
    if not baseline.is_dir():
        return True, "no baseline (skip)"
    fails: list[str] = []
    for png in sorted(current.glob("*.png")) + sorted(current.glob("*.bmp")):
        ref = baseline / png.name
        if not ref.is_file():
            fails.append(f"missing baseline {png.name}")
            continue
        d = hamming(phash(png), phash(ref))
        if d > max_h:
            fails.append(f"{png.name} hamming={d}>{max_h}")
    if fails:
        return False, "; ".join(fails)
    return True, "ok"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", nargs="+")
    parser.add_argument("--tier-a", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--current-dir", type=Path, default=ROOT / "bin" / "uv_preview")
    parser.add_argument("--baseline-dir", type=Path, default=TOOLS / "uv_preview_baseline")
    parser.add_argument("--update-baseline", action="store_true")
    parser.add_argument("--fail-on-diff", action="store_true")
    args = parser.parse_args()

    if args.all:
        species = list_all_species()
    elif args.tier_a:
        species = list(TIER_A_MOBS)
    elif args.species:
        species = args.species
    else:
        species = list(TIER_A_MOBS)

    import shutil

    failures = 0
    for sid in species:
        cur = args.current_dir / sid
        base = args.baseline_dir / sid
        if args.update_baseline:
            if cur.is_dir():
                base.parent.mkdir(parents=True, exist_ok=True)
                if base.exists():
                    shutil.rmtree(base)
                shutil.copytree(cur, base)
                print(f"baseline updated {sid}")
            continue
        if not cur.is_dir():
            print(f"SKIP {sid}: no preview dir {cur}")
            continue
        max_h = int(profile_thresholds(sid).get("preview_hamming_max", 14))
        ok, msg = compare_dir(sid, cur, base, max_h)
        print(f"{'PASS' if ok else 'FAIL'} {sid}: {msg}")
        if not ok:
            failures += 1

    if args.update_baseline:
        return 0
    return 1 if failures and args.fail_on_diff else (1 if failures else 0)


if __name__ == "__main__":
    raise SystemExit(main())
