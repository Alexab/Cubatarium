#!/usr/bin/env python3
"""Set texture_layout: box_uv for all non-human creatures."""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"


def main() -> int:
    changed = 0
    for path in sorted(MODELS.glob("*/creature.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if data.get("id") == "human":
            continue
        vis = data.setdefault("visual", {})
        if vis.get("texture_layout") == "box_uv":
            continue
        vis["texture_layout"] = "box_uv"
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"box_uv {data['id']}")
        changed += 1
    print(f"migrated {changed} species")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
