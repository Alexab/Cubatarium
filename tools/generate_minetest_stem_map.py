#!/usr/bin/env python3
"""Write tools/minetest_stem_map.yaml from built-in MINETEST_STEM_MAP."""

from __future__ import annotations

from pathlib import Path

from stem_mapping_common import write_minetest_stem_map_yaml

REPO = Path(__file__).resolve().parents[1]


def main() -> int:
    out = REPO / "tools" / "minetest_stem_map.yaml"
    write_minetest_stem_map_yaml(out)
    print(f"Wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
