#!/usr/bin/env python3
"""CI smoke checks for Cubatarium resource packs (merge, worldgen refs, audit)."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
PACKS = REPO / "resource_packs"
OBJECTS = REPO / "objects"
CANONICAL = REPO / "tools" / "canonical_blocks.yaml"
WORLDGEN_REFS = REPO / "content" / "worldgen_refs.json"
CONFIG_EXAMPLE = REPO / "config.json.example"

MERGE_SCENARIOS = [
    {"primary": ["kenney_voxel_16"], "secondary": ["cubatarium_cc0_base"]},
    {"primary": ["minetest_default_16"], "secondary": []},
]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=REPO, check=True)


def check_worldgen_refs_drift() -> None:
    run([sys.executable, "tools/generate_worldgen_refs.py"])
    # generate overwrites; if git clean, file matches. Re-read and validate slots.
    data = json.loads(WORLDGEN_REFS.read_text(encoding="utf-8"))
    slots = data.get("slots", {})
    canonical = yaml.safe_load(CANONICAL.read_text(encoding="utf-8")) or {}
    tier_a = canonical.get("tier_a", {})
    for name in tier_a:
        if name not in slots:
            raise SystemExit(f"worldgen_refs missing slot: {name}")
    print(f"worldgen_refs OK ({len(slots)} slots)")


def check_primary_audit() -> None:
    run([sys.executable, "tools/audit_resource_packs.py", "--primary-only"])


def check_validate_primary() -> None:
    for pack_dir in sorted(PACKS.iterdir()):
        if not pack_dir.is_dir():
            continue
        manifest_path = pack_dir / "pack.json"
        if not manifest_path.is_file():
            continue
        manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
        if manifest.get("worldgen_role") != "primary":
            continue
        run([sys.executable, "tools/validate_resource_pack.py", str(pack_dir.relative_to(REPO))])


def load_blocks(pack_id: str) -> dict[str, dict]:
    pack_dir = PACKS / pack_id
    blocks: dict[str, dict] = {}
    blocks_dir = pack_dir / "blocks"
    if not blocks_dir.is_dir():
        return blocks
    for path in blocks_dir.glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        if data.get("name"):
            blocks[data["name"]] = data
    return blocks


def merge_smoke() -> None:
    canonical = yaml.safe_load(CANONICAL.read_text(encoding="utf-8")) or {}
    tier_a = list(canonical.get("tier_a", {}).keys())
    refs = json.loads(WORLDGEN_REFS.read_text(encoding="utf-8")).get("slots", {})

    for scenario in MERGE_SCENARIOS:
        merged: dict[str, str] = {}
        for tier in ("primary", "secondary"):
            for pack_id in scenario[tier]:
                for name, block in load_blocks(pack_id).items():
                    if name not in merged:
                        merged[name] = pack_id
        missing = [n for n in tier_a if n not in merged]
        if missing:
            raise SystemExit(
                f"merge smoke failed for {scenario}: missing tier_a {missing}"
            )
        for slot, spec in refs.items():
            names = spec.get("block_names", [slot])
            if not any(n in merged for n in names):
                raise SystemExit(f"merge smoke: worldgen slot {slot!r} unresolved")
        print(f"merge smoke OK: {scenario}")


def collect_object_block_types() -> set[str]:
    types: set[str] = set()
    if not OBJECTS.is_dir():
        return types
    for path in OBJECTS.rglob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        for block in data.get("blocks", []):
            if isinstance(block, dict) and isinstance(block.get("type"), str):
                types.add(block["type"])
    return types


def texture_exists(pack_id: str, stem: str) -> bool:
    if "/" in stem:
        pack_id, stem = stem.split("/", 1)
    path = PACKS / pack_id / "textures" / "blocks" / f"{stem}.png"
    return path.is_file()


def check_object_prefab_blocks() -> None:
    object_types = collect_object_block_types()
    if not object_types:
        raise SystemExit("object prefab smoke: no block types found in objects/")
    decor = ("stone", "tree_bark", "tree_log")

    for scenario in MERGE_SCENARIOS:
        merged: dict[str, dict] = {}
        owner: dict[str, str] = {}
        for tier in ("primary", "secondary"):
            for pack_id in scenario[tier]:
                for name, block in load_blocks(pack_id).items():
                    if name not in merged:
                        merged[name] = block
                        owner[name] = pack_id
        for name in decor:
            if name not in merged:
                raise SystemExit(
                    f"object prefab smoke failed for {scenario}: "
                    f"missing decor block {name!r}"
                )
            block = merged[name]
            textures = block.get("textures") or []
            if not isinstance(textures, list) or len(textures) < 6:
                raise SystemExit(
                    f"object prefab smoke: block {name!r} has invalid textures"
                )
            pack_id = owner[name]
            for stem in textures[:6]:
                if not isinstance(stem, str) or not texture_exists(pack_id, stem):
                    raise SystemExit(
                        f"object prefab smoke: missing texture {pack_id}/{stem} "
                        f"for block {name!r} in {scenario}"
                    )

    minetest_merged = load_blocks("minetest_default_16")
    known_optional = {"dandelion"}
    missing_minetest = sorted(
        t for t in object_types if t not in minetest_merged and t not in known_optional
    )
    if missing_minetest:
        raise SystemExit(
            "object prefab smoke: minetest primary missing object types: "
            + ", ".join(missing_minetest)
        )
    print(
        f"object prefab smoke OK ({len(object_types)} object block types, "
        f"decor textures verified)"
    )


def check_texture_overrides_sync() -> None:
    run([sys.executable, "tools/sync_texture_overrides.py", "--check"])


def check_objects() -> None:
    run([sys.executable, "tools/generate_object_features.py", "--merge-calibrated"])
    run([sys.executable, "tools/validate_prefabs.py"])
    run([sys.executable, "tools/validate_object_features.py", "--strict"])
    run([sys.executable, "tools/validate_worldgen_smoothness.py"])


def check_content_completeness() -> None:
    run([sys.executable, "tools/validate_content_completeness.py"])


def main() -> int:
    print("=== resource pack smoke ===")
    check_worldgen_refs_drift()
    check_texture_overrides_sync()
    merge_smoke()
    check_object_prefab_blocks()
    check_primary_audit()
    check_validate_primary()
    check_objects()
    check_content_completeness()
    print("=== all smoke checks passed ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
