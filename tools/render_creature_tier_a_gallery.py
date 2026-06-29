#!/usr/bin/env python3
"""Generate Tier A creature visual baseline artifacts (manifest + UV overlays)."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent

from creature_tier_a import TIER_A_SPECIES


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=ROOT / "tools" / "debug_uv_overlays" / "tier_a_gallery",
    )
    parser.add_argument("--research", type=Path, default=Path(r"E:/Work/Home/CubatariumTextureResearch"))
    args = parser.parse_args()

    debug = TOOLS / "debug_creature_uv_crops.py"
    cmd = [
        sys.executable,
        str(debug),
        "--tier-a",
        "--auto",
        "--manifest",
        "--out-dir",
        str(args.out_dir),
        "--research",
        str(args.research),
    ]
    print("running:", " ".join(cmd))
    subprocess.run(cmd, check=True, cwd=ROOT)

    readme = args.out_dir / "README.txt"
    readme.write_text(
        "Tier A visual baseline\n"
        f"Species: {', '.join(TIER_A_SPECIES)}\n"
        "Files:\n"
        "  tier_a_baseline_manifest.json — part/stem summary per species\n"
        "  <species>_uv_overlay.png — Luanti atlas stem crop rects\n"
        "Regenerate after geometry/bake changes.\n",
        encoding="utf-8",
    )
    print(f"wrote {readme}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
