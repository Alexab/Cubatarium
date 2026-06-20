#!/usr/bin/env python3
"""Generate content/prefab_features.json from tools/prefab_manifest.yaml."""

from __future__ import annotations

import json
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
MANIFEST = REPO / "tools" / "prefab_manifest.yaml"
OUT_JSON = REPO / "content" / "prefab_features.json"
OUT_YAML = REPO / "content" / "prefab_features.yaml"


def main() -> int:
    manifest = yaml.safe_load(MANIFEST.read_text(encoding="utf-8")) or {}
    prefabs = manifest.get("prefabs") or {}

    pools: dict[str, list] = {
        "vegetation": [],
        "decoration": [],
        "structures": [],
    }

    for name, meta in sorted(prefabs.items()):
        if meta.get("import_status") not in (None, "ready"):
            continue
        wg = meta.get("worldgen")
        if not wg:
            continue
        pool = wg.get("pool")
        if pool not in pools:
            continue
        entry = {
            "prefab": name,
            "biomes": wg.get("biomes", []),
            "weight": wg.get("weight", 1),
            "seed_offset": wg.get("seed_offset", 0),
        }
        if wg.get("spacing"):
            entry["spacing"] = wg["spacing"]
        if wg.get("chance_per_column"):
            entry["chance_per_column"] = wg["chance_per_column"]
        placement = meta.get("placement") or {}
        if placement.get("y_offset") is not None:
            entry["placement_y_offset"] = placement["y_offset"]
        pools[pool].append(entry)

    doc = {"schema_version": 1, **pools}
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    OUT_YAML.write_text(
        yaml.safe_dump(doc, sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )
    total = sum(len(v) for v in pools.values())
    print(f"Wrote {OUT_JSON} ({total} rules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
