"""Ship-set Tier A creature ids for visual overhaul tooling."""

from __future__ import annotations

TIER_A_SPECIES: tuple[str, ...] = (
    "human",
    "sheep",
    "wolf",
    "pig",
    "cow",
    "chicken",
    "oerkki",
    "skeleton",
    "sand_monster",
)

TIER_A_MOBS: tuple[str, ...] = tuple(s for s in TIER_A_SPECIES if s != "human")
