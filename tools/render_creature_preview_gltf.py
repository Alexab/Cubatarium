#!/usr/bin/env python3
"""Validate glTF species assets and emit 4-yaw preview markers (CI smoke)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"
OUT = ROOT / "bin" / "uv_preview"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from validate_gltf_creature import validate_species  # noqa: E402


def main() -> int:
    import argparse

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--species", action="append", dest="species")
    ap.add_argument("positional", nargs="*")
    args = ap.parse_args()
    species_list = args.species or args.positional or []
    if not species_list:
        species_list = sorted(
            p.name
            for p in MODELS.iterdir()
            if (p / "creature.json").is_file()
            and json.loads((p / "creature.json").read_text(encoding="utf-8"))
            .get("visual", {})
            .get("backend")
            == "gltf_skeleton"
        )
    err = 0
    for species in species_list:
        errors = validate_species(species)
        if errors:
            for e in errors:
                print(f"FAIL {species}: {e}", file=sys.stderr)
            err = 1
            continue
        OUT.mkdir(parents=True, exist_ok=True)
        for yaw in (0, 90, 180, 270):
            marker = OUT / species / f"yaw_{yaw}.bmp"
            marker.parent.mkdir(parents=True, exist_ok=True)
            if not marker.is_file():
                marker.write_bytes(b"BMP")
        print(f"OK render_creature_preview_gltf: {species}")
    return err


if __name__ == "__main__":
    raise SystemExit(main())
