"""Shared helpers for creature UV validation and wave rollout."""

from __future__ import annotations

import json
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
MODELS = ROOT / "models" / "creatures"
RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

from creature_tier_a import TIER_A_MOBS, TIER_A_SPECIES  # noqa: E402

WAVE_MAP: dict[str, str] = {
    "human": "W0",
    "sheep": "W1",
    "cow": "W1",
    "chicken": "W1",
    "wolf": "W2",
    "pig": "W2",
    "oerkki": "W3",
    "skeleton": "W3",
    "sand_monster": "W3",
    "badger": "W4",
    "bunny": "W4",
    "fox": "W4",
    "hedgehog": "W4",
    "penguin": "W4",
    "spider": "W4",
    "tortoise": "W4",
    "rat": "W5",
    "panda": "W5",
    "kitten": "W5",
    "warthog": "W5",
    "dirt_monster": "W6",
    "dungeon_master": "W6",
    "fire_spirit": "W6",
    "golem": "W6",
    "land_guard": "W6",
    "lava_flan": "W6",
    "mese_monster": "W6",
    "orc": "W6",
    "ogre": "W6",
    "stone_monster": "W6",
    "tree_monster": "W6",
    "treeman": "W6",
    "bee": "W7",
    "butterfly": "W7",
    "owl": "W7",
    "wasp": "W7",
    "puffin": "W7",
    "trout": "W8",
    "shark": "W8",
    "seahorse": "W8",
    "dolphin": "W8",
    "crab": "W8",
    "lobster": "W8",
    "hermitcrab": "W8",
    "seal": "W8",
    "squid": "W8",
    "stingray": "W8",
    "manatee": "W8",
    "octopus": "W8",
    "water_dragon": "W8",
    "whale": "W8",
}

WAVE_SPECIES: dict[str, list[str]] = {}
for _sid, _wave in WAVE_MAP.items():
    WAVE_SPECIES.setdefault(_wave, []).append(_sid)
for _w in WAVE_SPECIES:
    WAVE_SPECIES[_w].sort()

GATE_IDS: tuple[str, ...] = tuple(f"G{i:02d}" for i in range(1, 14))

PLACEHOLDER_SPECIES: frozenset[str] = frozenset(
    {
        "dolphin",
        "kitten",
        "lava_flan",
        "mese_monster",
        "octopus",
        "warthog",
        "water_dragon",
        "whale",
    }
)

SNOUT_PART_IDS: frozenset[str] = frozenset({"snout", "beak"})


def load_yaml(path: Path) -> dict:
    if not path.is_file():
        return {}
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def load_sources() -> dict:
    return load_yaml(TOOLS / "creature_luanti_sources.yaml")


def species_with_b3d() -> set[str]:
    sources = load_sources()
    return {sid for sid, spec in sources.get("species", {}).items() if spec.get("model")}


NO_B3D_SPECIES: frozenset[str] = frozenset(
    s for s in WAVE_MAP if s != "human" and s not in species_with_b3d()
)


def load_audit() -> dict:
    return load_yaml(TOOLS / "creature_audit_status.yaml")


def load_thresholds() -> dict:
    return load_yaml(TOOLS / "creature_uv_thresholds.yaml")


def load_creature(species_id: str) -> dict:
    path = MODELS / species_id / "creature.json"
    return json.loads(path.read_text(encoding="utf-8"))


def list_all_species() -> list[str]:
    return sorted(p.parent.name for p in MODELS.glob("*/creature.json"))


def threshold_profile(species_id: str, creature: dict | None = None) -> str:
    thresholds = load_thresholds()
    overrides = thresholds.get("overrides") or {}
    if species_id in overrides:
        ov = overrides[species_id]
        if ov.get("skip_all"):
            return "human"
        if isinstance(ov, dict) and "profile" in ov:
            return str(ov["profile"])
    if species_id == "human":
        return "human"
    if species_id in TIER_A_MOBS:
        return "tier_a"
    if species_id in NO_B3D_SPECIES:
        archetype = (creature or load_creature(species_id)).get("locomotion_archetype", "")
        if archetype == "terrestrial_quadruped":
            return "quadruped_no_b3d"
    archetype = (creature or load_creature(species_id)).get("locomotion_archetype", "")
    if archetype == "aquatic" or (creature or load_creature(species_id)).get("habitat") == "aquatic":
        return "aquatic"
    return "default"


def profile_thresholds(species_id: str, creature: dict | None = None) -> dict[str, Any]:
    data = load_thresholds()
    profiles = data.get("profiles") or {}
    name = threshold_profile(species_id, creature)
    base = dict(profiles.get("default") or {})
    if name != "default" and name in profiles:
        base.update(profiles[name])
    overrides = (data.get("overrides") or {}).get(species_id) or {}
    for k, v in overrides.items():
        if k not in ("skip_all", "profile"):
            base[k] = v
    return base


def gate_skips(species_id: str, creature: dict | None = None) -> list[str]:
    if species_id == "human":
        return ["G02", "G03", "G04", "G05", "G06", "G07", "G08", "G09", "G12"]
    skips: list[str] = []
    sources = load_sources()
    spec = (sources.get("species") or {}).get(species_id) or {}
    if not spec.get("model"):
        skips.extend(["G02", "G03"])
    creature = creature or load_creature(species_id)
    parts = creature.get("visual", {}).get("parts", [])
    if not any(p.get("id") in SNOUT_PART_IDS for p in parts):
        skips.append("G09")
    return skips


def has_snout_part(creature: dict) -> bool:
    return any(p.get("id") in SNOUT_PART_IDS for p in creature.get("visual", {}).get("parts", []))


def is_placeholder(species_id: str) -> bool:
    audit = load_audit()
    entry = (audit.get("species") or {}).get(species_id) or {}
    if entry.get("asset_tier") == "placeholder":
        return True
    lic = MODELS / species_id / "LICENSE.txt"
    if lic.is_file() and "Placeholder procedural" in lic.read_text(encoding="utf-8", errors="ignore"):
        return True
    return species_id in PLACEHOLDER_SPECIES


def upstream_exists(species_id: str, research: Path = RESEARCH_DEFAULT) -> bool:
    if species_id == "human":
        return True
    sources = load_sources()
    spec = (sources.get("species") or {}).get(species_id)
    if not spec:
        return False
    if "composite" in spec:
        for layer in spec["composite"]:
            path = research / layer["mod"] / layer["file"]
            if not path.is_file():
                return False
        return True
    tex = spec.get("texture")
    if tex:
        return (research / tex).is_file()
    return bool(spec.get("model"))


def now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def texture_stems(creature: dict) -> set[str]:
    return {p["texture"] for p in creature.get("visual", {}).get("parts", []) if p.get("texture")}


def parts_count(creature: dict) -> int:
    return len(creature.get("visual", {}).get("parts", []))
