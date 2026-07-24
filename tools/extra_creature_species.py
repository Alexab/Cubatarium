"""Additional Luanti creature species for generate_luanti_creature_catalog.py."""

from __future__ import annotations


def _q(
    display: str,
    sort: int,
    color: tuple[int, int, int],
    eye: float,
    walk: float,
    *,
    habitat: str = "terrestrial",
    hostile: bool = False,
    archetype: str = "terrestrial_quadruped",
    bounds: dict | None = None,
) -> dict:
    b = bounds or {
        "rest": [0.7, 0.8, 1.2],
        "max": [0.7, 0.8, 1.2],
        "min": [0.7, 0.6, 1.2],
    }
    return {
        "display": display,
        "tags": ["mobs", "hostile" if hostile else "passive"],
        "sort": sort,
        "archetype": archetype,
        "habitat": habitat,
        "color": color,
        "bounds": b,
        "eye": eye,
        "walk": walk,
        "icon": [color[0] / 255, color[1] / 255, color[2] / 255, 1.0],
    }


def _b(
    display: str,
    sort: int,
    color: tuple[int, int, int],
    eye: float,
    walk: float,
    *,
    habitat: str = "terrestrial",
    hostile: bool = True,
) -> dict:
    return {
        "display": display,
        "tags": ["mobs", "hostile" if hostile else "passive"],
        "sort": sort,
        "archetype": "terrestrial_biped",
        "habitat": habitat,
        "color": color,
        "bounds": {
            "rest": [0.65, 1.75, 0.65],
            "max": [0.65, 1.75, 0.65],
            "min": [0.65, 1.3, 0.65],
        },
        "eye": eye,
        "walk": walk,
        "icon": [color[0] / 255, color[1] / 255, color[2] / 255, 1.0],
    }


def _aq(
    display: str,
    sort: int,
    color: tuple[int, int, int],
    eye: float,
    walk: float,
    *,
    size: list[float] | None = None,
    habitat: str = "aquatic",
) -> dict:
    s = size or [0.8, 0.5, 1.2]
    return {
        "display": display,
        "tags": ["mobs", "passive"],
        "sort": sort,
        "archetype": "aquatic",
        "habitat": habitat,
        "color": color,
        "bounds": {"rest": s, "max": s, "min": s},
        "eye": eye,
        "walk": walk,
        "icon": [color[0] / 255, color[1] / 255, color[2] / 255, 1.0],
    }


EXTRA_SPECIES: dict[str, dict] = {
    # mobs_animal wave
    "bunny": _q("Bunny", 18, (180, 160, 140), 0.45, 2.5),
    "rat": _q(
        "Rat",
        19,
        (100, 90, 85),
        0.25,
        2.2,
        bounds={
            "rest": [0.35, 0.35, 0.55],
            "max": [0.35, 0.35, 0.55],
            "min": [0.35, 0.25, 0.55],
        },
    ),
    "panda": _q("Panda", 21, (40, 40, 45), 0.95, 1.8),
    "kitten": _q(
        "Kitten",
        22,
        (200, 170, 130),
        0.4,
        2.0,
        bounds={
            "rest": [0.45, 0.45, 0.7],
            "max": [0.45, 0.45, 0.7],
            "min": [0.45, 0.35, 0.7],
        },
    ),
    "penguin": _q(
        "Penguin",
        23,
        (30, 30, 35),
        0.45,
        2.0,
        habitat="amphibious",
        archetype="aquatic",
        bounds={
            "rest": [0.45, 0.65, 0.45],
            "max": [0.45, 0.65, 0.45],
            "min": [0.45, 0.5, 0.45],
        },
    ),
    "bee": {
        "display": "Bee",
        "tags": ["mobs", "passive"],
        "sort": 24,
        "archetype": "aerial",
        "habitat": "aerial",
        "color": (220, 180, 40),
        "bounds": {
            "rest": [0.35, 0.35, 0.55],
            "max": [0.35, 0.35, 0.55],
            "min": [0.35, 0.25, 0.55],
        },
        "eye": 0.25,
        "walk": 2.5,
        "icon": [0.9, 0.75, 0.15, 1.0],
    },
    "warthog": _q("Warthog", 25, (120, 85, 70), 0.8, 2.3),
    # mobs_monster
    "spider": _q(
        "Spider",
        40,
        (40, 40, 45),
        0.35,
        2.6,
        hostile=True,
        bounds={
            "rest": [0.9, 0.5, 0.9],
            "max": [0.9, 0.5, 0.9],
            "min": [0.9, 0.35, 0.9],
        },
    ),
    "stone_monster": _b("Stone Monster", 41, (130, 130, 135), 1.5, 2.0),
    "tree_monster": _b("Tree Monster", 42, (70, 110, 50), 1.7, 1.8),
    "mese_monster": _b("Mese Monster", 43, (100, 60, 180), 1.55, 2.2),
    "dirt_monster": _b("Dirt Monster", 44, (90, 70, 50), 1.5, 2.0),
    "dungeon_master": _b("Dungeon Master", 45, (80, 30, 30), 1.7, 2.3),
    "fire_spirit": _b(
        "Fire Spirit", 46, (255, 120, 40), 1.2, 2.5, habitat="aerial"
    ),
    "land_guard": _b("Land Guard", 47, (160, 140, 90), 1.65, 2.1),
    "lava_flan": _b(
        "Lava Flan", 48, (200, 80, 30), 1.0, 1.8, habitat="lava"
    ),
    # dmobs
    "fox": _q("Fox", 50, (200, 100, 50), 0.55, 2.8),
    "badger": _q("Badger", 51, (80, 70, 60), 0.4, 2.2),
    "hedgehog": _q(
        "Hedgehog",
        52,
        (110, 100, 90),
        0.25,
        1.5,
        bounds={
            "rest": [0.4, 0.3, 0.55],
            "max": [0.4, 0.3, 0.55],
            "min": [0.4, 0.2, 0.55],
        },
    ),
    "tortoise": _q(
        "Tortoise",
        53,
        (90, 120, 70),
        0.35,
        1.2,
        archetype="serpentine",
        bounds={
            "rest": [0.7, 0.45, 0.9],
            "max": [0.7, 0.45, 0.9],
            "min": [0.7, 0.35, 0.9],
        },
    ),
    "orc": _b("Orc", 54, (80, 120, 70), 1.55, 2.4),
    "ogre": _b("Ogre", 55, (100, 90, 75), 1.8, 2.0),
    "golem": _b("Golem", 56, (140, 140, 150), 1.75, 1.6, hostile=False),
    "treeman": _b("Treeman", 57, (60, 100, 45), 1.85, 1.5),
    "butterfly": {
        "display": "Butterfly",
        "tags": ["mobs", "passive"],
        "sort": 58,
        "archetype": "aerial",
        "habitat": "aerial",
        "color": (180, 100, 200),
        "bounds": {
            "rest": [0.5, 0.35, 0.5],
            "max": [0.5, 0.35, 0.5],
            "min": [0.5, 0.25, 0.5],
        },
        "eye": 0.2,
        "walk": 2.0,
        "icon": [0.75, 0.45, 0.85, 1.0],
    },
    "owl": {
        "display": "Owl",
        "tags": ["mobs", "passive"],
        "sort": 59,
        "archetype": "aerial",
        "habitat": "aerial",
        "color": (120, 90, 60),
        "bounds": {
            "rest": [0.55, 0.65, 0.55],
            "max": [0.55, 0.65, 0.55],
            "min": [0.55, 0.5, 0.55],
        },
        "eye": 0.5,
        "walk": 2.2,
        "icon": [0.5, 0.4, 0.25, 1.0],
    },
    "wasp": {
        "display": "Wasp",
        "tags": ["mobs", "hostile"],
        "sort": 60,
        "archetype": "aerial",
        "habitat": "aerial",
        "color": (220, 200, 40),
        "bounds": {
            "rest": [0.4, 0.35, 0.6],
            "max": [0.4, 0.35, 0.6],
            "min": [0.4, 0.25, 0.6],
        },
        "eye": 0.22,
        "walk": 3.0,
        "icon": [0.9, 0.85, 0.15, 1.0],
    },
    # animalworld marine + dmobs aquatic
    "trout": _aq("Trout", 70, (180, 120, 80), 0.25, 2.5),
    "shark": _aq(
        "Shark",
        71,
        (120, 125, 130),
        0.45,
        3.5,
        size=[1.2, 0.6, 2.0],
    ),
    "squid": _aq("Squid", 72, (180, 80, 120), 0.35, 2.8, size=[0.9, 0.7, 1.4]),
    "stingray": _aq(
        "Stingray", 73, (90, 90, 100), 0.2, 2.2, size=[1.1, 0.15, 1.1]
    ),
    "seahorse": _aq(
        "Seahorse",
        74,
        (80, 140, 160),
        0.35,
        1.5,
        size=[0.25, 0.55, 0.25],
    ),
    "manatee": _aq(
        "Manatee", 75, (130, 130, 140), 0.45, 1.8, size=[1.0, 0.7, 1.8]
    ),
    "lobster": _aq(
        "Lobster", 76, (180, 60, 50), 0.25, 1.8, size=[0.7, 0.35, 1.0]
    ),
    "hermitcrab": _aq(
        "Hermit Crab", 77, (180, 100, 70), 0.2, 1.5, size=[0.5, 0.35, 0.5]
    ),
    "seal": _aq(
        "Seal",
        78,
        (110, 110, 115),
        0.4,
        2.0,
        size=[0.9, 0.55, 1.4],
        habitat="amphibious",
    ),
    "dolphin": _aq(
        "Dolphin", 79, (130, 140, 150), 0.45, 3.2, size=[0.9, 0.55, 1.8]
    ),
    "whale": _aq(
        "Whale", 80, (70, 85, 110), 0.8, 2.0, size=[2.5, 1.2, 4.0]
    ),
    "water_dragon": _aq(
        "Water Dragon",
        81,
        (40, 120, 160),
        0.9,
        2.8,
        size=[1.4, 1.0, 2.2],
    ),
    "crab": _aq("Crab", 82, (200, 80, 60), 0.2, 1.6, size=[0.65, 0.3, 0.85]),
    "octopus": _aq(
        "Octopus", 83, (160, 60, 100), 0.35, 2.2, size=[0.9, 0.5, 0.9]
    ),
    "puffin": {
        "display": "Puffin",
        "tags": ["mobs", "passive"],
        "sort": 84,
        "archetype": "aerial",
        "habitat": "aerial",
        "color": (40, 40, 45),
        "bounds": {
            "rest": [0.45, 0.5, 0.45],
            "max": [0.45, 0.5, 0.45],
            "min": [0.45, 0.35, 0.45],
        },
        "eye": 0.35,
        "walk": 2.0,
        "icon": [0.2, 0.2, 0.25, 1.0],
    },
}
