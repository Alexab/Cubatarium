#!/usr/bin/env python3
"""Copy visual.parts from creature_rigid_parts.yaml into creature.json."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
MODELS = ROOT / "models" / "creatures"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", action="append", nargs="+")
    args = parser.parse_args()

    rigid = yaml.safe_load((TOOLS / "creature_rigid_parts.yaml").read_text(encoding="utf-8"))
    flat: list[str] = []
    if args.species:
        for group in args.species:
            flat.extend(group)
    species_list = flat or [k for k in rigid if k != "default" and "parts" in rigid[k]]
    for species_id in species_list:
        if species_id not in rigid or "parts" not in rigid[species_id]:
            print(f"skip {species_id}: no rigid parts")
            continue
        creature_path = MODELS / species_id / "creature.json"
        if not creature_path.is_file():
            print(f"skip {species_id}: missing creature.json")
            continue
        data = json.loads(creature_path.read_text(encoding="utf-8"))
        data.setdefault("visual", {})["parts"] = rigid[species_id]["parts"]
        creature_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        print(f"patched parts {species_id}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
