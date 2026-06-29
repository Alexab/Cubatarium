#!/usr/bin/env python3
"""Extract rotation keyframe hints from Bedrock animation JSON (calibration aid)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ANIM_DIR = ROOT / "models" / "creatures" / "_sources" / "bedrock_animations"


def summarize_animation(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    out: dict = {"file": path.name, "bones": {}}
    anims = data.get("animations", data)
    for anim_name, anim in anims.items():
        if not isinstance(anim, dict):
            continue
        bones = anim.get("bones", {})
        for bone_name, bone_data in bones.items():
            rot = bone_data.get("rotation", {})
            if isinstance(rot, dict) and "0.0" in rot:
                out["bones"].setdefault(bone_name, []).append(
                    {"anim": anim_name, "t0": rot.get("0.0")}
                )
    return out


def main() -> int:
    if not ANIM_DIR.is_dir():
        print(f"SKIP no animation dir {ANIM_DIR}")
        return 0
    for path in sorted(ANIM_DIR.glob("*.animation.json")):
        summary = summarize_animation(path)
        print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
