#!/usr/bin/env python3
"""Batch bind-pose / skinning smoke for all skinned glTF creatures (TD-CRE-027)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent

sys.path.insert(0, str(TOOLS))
from validate_gltf_creature import skinned_species, validate_species  # noqa: E402


def main() -> int:
    species = skinned_species()
    if not species:
        print("SKIP test_gltf_skinned_bind_pose: no skinned species")
        return 0

    err = 0
    for sp in species:
        errors = validate_species(sp)
        if errors:
            err += 1
            for e in errors:
                print(f"FAIL {sp}: {e}", file=sys.stderr)
        else:
            gltf = json.loads(
                (ROOT / "models" / "creatures" / sp / "model.gltf").read_text(
                    encoding="utf-8"
                )
            )
            joints = len(gltf.get("skins", [{}])[0].get("joints", []))
            anims = [a.get("name") for a in gltf.get("animations", [])]
            print(f"OK {sp}: joints={joints} anims={anims}")

    print(f"test_gltf_skinned_bind_pose: {len(species) - err}/{len(species)} passed")
    return 1 if err else 0


if __name__ == "__main__":
    raise SystemExit(main())
