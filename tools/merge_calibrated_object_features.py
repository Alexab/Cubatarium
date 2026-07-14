#!/usr/bin/env python3
"""Merge calibrated rules from legacy prefab_features JSON into object_features.json."""

from __future__ import annotations

import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LEGACY = REPO / "content" / "_prefab_features_restore.json"
OUT = REPO / "content" / "object_features.json"


def rule_key(rule: dict) -> tuple:
    name = rule.get("object") or rule.get("prefab") or rule.get("block")
    return (name, tuple(sorted(rule.get("biomes", []))))


def load_object_names() -> set[str]:
    names: set[str] = set()
    objects_dir = REPO / "objects"
    for path in objects_dir.rglob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        names.add(data.get("name") or path.stem)
    return names


def main() -> int:
    if not LEGACY.is_file():
        print(f"missing {LEGACY}")
        return 1
    object_names = load_object_names()
    old = json.loads(LEGACY.read_text(encoding="utf-8-sig"))
    cur = json.loads(OUT.read_text(encoding="utf-8-sig")) if OUT.is_file() else {"schema_version": 1}
    for pool in ("vegetation", "ground_cover", "decoration", "structures"):
        cur.setdefault(pool, [])
        existing = {rule_key(r) for r in cur[pool] if isinstance(r, dict)}
        for rule in old.get(pool, []):
            if not isinstance(rule, dict) or not rule.get("calibrated"):
                continue
            r = dict(rule)
            if "prefab" in r and "object" not in r:
                r["object"] = r.pop("prefab")
            obj = r.get("object")
            if obj and obj not in object_names:
                continue
            key = rule_key(r)
            if key not in existing:
                cur[pool].append(r)
                existing.add(key)
    OUT.write_text(json.dumps(cur, indent=2) + "\n", encoding="utf-8")
    print(f"merged calibrated rules into {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
