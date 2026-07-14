#!/usr/bin/env python3
"""Add missing model: entries to creature_luanti_sources.yaml from research Lua."""

from __future__ import annotations

import sys
from pathlib import Path

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
YAML_PATH = TOOLS / "creature_luanti_sources.yaml"
MODELS = ROOT / "models" / "creatures"

sys.path.insert(0, str(TOOLS))
from luanti_mob_animation import load_mob_properties, resolve_b3d_model_path  # noqa: E402


def main() -> int:
    data = yaml.safe_load(YAML_PATH.read_text(encoding="utf-8")) or {}
    species = data.setdefault("species", {})
    added = 0
    skipped_sprite = 0
    for species_dir in sorted(MODELS.iterdir()):
        cj = species_dir / "creature.json"
        if not cj.is_file():
            continue
        sid = species_dir.name
        entry = species.setdefault(sid, {})
        if entry.get("model"):
            continue
        props, _ = load_mob_properties(sid)
        if props.get("visual") == "sprite":
            skipped_sprite += 1
            continue
        path = resolve_b3d_model_path(sid)
        if path:
            entry["model"] = path
            added += 1
            print(f"  + {sid}: {path}")
        elif props.get("mesh"):
            # Known mesh reuse without file on disk — still record canonical path
            mesh = props["mesh"]
            from luanti_mob_animation import find_species_lua, _mod_root_for_lua

            lua = find_species_lua(sid)
            if lua:
                mod_root = _mod_root_for_lua(lua, Path(r"E:/Work/Home/CubatariumTextureResearch"))
                rel = f"{mod_root.name}/models/{mesh}"
                entry["model"] = rel.replace("\\", "/")
                added += 1
                print(f"  + {sid}: {entry['model']} (lua mesh, file may be absent)")

    YAML_PATH.write_text(
        yaml.safe_dump(data, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(f"sync_creature_yaml_b3d_models: added {added}, skipped sprite {skipped_sprite}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
