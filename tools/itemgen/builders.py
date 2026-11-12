"""Family builders: familiar silhouettes, original box geometry."""

from __future__ import annotations

from typing import Any


Part = dict[str, Any]


def _p(role: str, ox: float, oy: float, oz: float, sx: float, sy: float, sz: float) -> Part:
    return {
        "role": role,
        "offset": [ox, oy, oz],
        "size": [sx, sy, sz],
    }


def pickaxe(material: str) -> list[Part]:
    parts = [
        _p("handle", 0.0, -0.05, 0.0, 0.055, 0.72, 0.055),
    ]
    if material in ("wood", "stone"):
        # Lashed / chipped: thick bar + clearly drooping tips (not a hammer).
        parts += [
            _p("binding", 0.0, 0.24, 0.0, 0.12, 0.10, 0.09),
            _p("head", 0.0, 0.34, 0.0, 0.50, 0.10, 0.08),
            _p("edge", 0.22, 0.18, 0.0, 0.11, 0.22, 0.08),
            _p("edge", -0.22, 0.18, 0.0, 0.11, 0.22, 0.08),
        ]
        if material == "stone":
            parts.append(_p("accent", 0.0, 0.40, 0.0, 0.22, 0.06, 0.09))
        else:
            parts.append(_p("binding", 0.0, 0.18, 0.0, 0.14, 0.05, 0.07))
    else:
        # Forged: thinner bar, longer downward points, ferrule accent.
        parts += [
            _p("binding", 0.0, 0.28, 0.0, 0.08, 0.05, 0.06),
            _p("head", 0.0, 0.36, 0.0, 0.42, 0.07, 0.05),
            _p("edge", 0.20, 0.22, 0.0, 0.07, 0.24, 0.05),
            _p("edge", -0.20, 0.22, 0.0, 0.07, 0.24, 0.05),
            _p("accent", 0.0, 0.40, 0.0, 0.10, 0.04, 0.06),
        ]
    return parts


def axe(material: str) -> list[Part]:
    parts = [_p("handle", 0.0, -0.02, 0.0, 0.055, 0.74, 0.055)]
    if material in ("wood", "stone"):
        parts += [
            _p("binding", 0.02, 0.28, 0.0, 0.12, 0.12, 0.09),
            _p("head", 0.15, 0.30, 0.0, 0.26, 0.22, 0.07),
            _p("edge", 0.27, 0.30, 0.0, 0.07, 0.26, 0.045),
        ]
    else:
        parts += [
            _p("binding", 0.02, 0.33, 0.0, 0.07, 0.07, 0.06),
            _p("head", 0.14, 0.35, 0.0, 0.22, 0.14, 0.05),
            _p("edge", 0.25, 0.35, 0.0, 0.05, 0.22, 0.03),
            _p("accent", 0.07, 0.35, 0.0, 0.05, 0.09, 0.05),
        ]
    return parts


def shovel(material: str) -> list[Part]:
    parts = [_p("handle", 0.0, 0.02, 0.0, 0.05, 0.70, 0.05)]
    if material in ("wood", "stone"):
        parts += [
            _p("binding", 0.0, -0.30, 0.0, 0.08, 0.06, 0.06),
            _p("head", 0.0, -0.42, 0.0, 0.18, 0.22, 0.04),
            _p("edge", 0.0, -0.52, 0.0, 0.14, 0.06, 0.03),
            _p("accent", 0.0, 0.36, 0.0, 0.12, 0.04, 0.05),
        ]
    else:
        parts += [
            _p("binding", 0.0, -0.30, 0.0, 0.07, 0.05, 0.05),
            _p("head", 0.0, -0.42, 0.0, 0.17, 0.24, 0.035),
            _p("edge", 0.0, -0.53, 0.0, 0.12, 0.05, 0.025),
            _p("accent", 0.0, 0.36, 0.0, 0.11, 0.035, 0.045),
        ]
    return parts


def hoe(material: str) -> list[Part]:
    parts = [_p("handle", 0.0, 0.0, 0.0, 0.05, 0.72, 0.05)]
    if material in ("wood", "stone"):
        parts += [
            _p("binding", 0.0, 0.30, 0.0, 0.08, 0.07, 0.07),
            _p("head", 0.12, 0.32, 0.0, 0.22, 0.05, 0.05),
            _p("edge", 0.22, 0.28, 0.0, 0.05, 0.12, 0.05),
        ]
    else:
        parts += [
            _p("binding", 0.0, 0.32, 0.0, 0.07, 0.06, 0.06),
            _p("head", 0.14, 0.34, 0.0, 0.24, 0.045, 0.045),
            _p("edge", 0.24, 0.30, 0.0, 0.045, 0.12, 0.045),
            _p("accent", 0.04, 0.34, 0.0, 0.06, 0.05, 0.05),
        ]
    return parts


def sword(material: str) -> list[Part]:
    parts = [
        _p("handle", 0.0, -0.18, 0.0, 0.05, 0.22, 0.05),
        _p("binding", 0.0, -0.06, 0.0, 0.16, 0.04, 0.04),
        _p("accent", 0.0, -0.28, 0.0, 0.07, 0.05, 0.05),
    ]
    if material in ("wood", "stone"):
        parts += [
            _p("head", 0.0, 0.18, 0.0, 0.06, 0.48, 0.03),
            _p("edge", 0.0, 0.42, 0.0, 0.04, 0.08, 0.02),
        ]
    else:
        parts += [
            _p("head", 0.0, 0.20, 0.0, 0.055, 0.52, 0.025),
            _p("edge", 0.0, 0.46, 0.0, 0.035, 0.08, 0.02),
            _p("accent", 0.0, -0.06, 0.0, 0.18, 0.03, 0.05),
        ]
    return parts


def bow(_material: str) -> list[Part]:
    return [
        _p("handle", 0.0, 0.0, 0.0, 0.05, 0.18, 0.05),
        _p("head", 0.0, 0.28, -0.04, 0.05, 0.28, 0.05),
        _p("head", 0.0, -0.28, -0.04, 0.05, 0.28, 0.05),
        _p("edge", 0.0, 0.40, 0.06, 0.03, 0.08, 0.03),
        _p("edge", 0.0, -0.40, 0.06, 0.03, 0.08, 0.03),
        _p("binding", 0.0, 0.0, 0.10, 0.02, 0.72, 0.02),
    ]


def arrow(_material: str) -> list[Part]:
    return [
        _p("handle", 0.0, 0.0, 0.0, 0.03, 0.55, 0.03),
        _p("head", 0.0, 0.30, 0.0, 0.06, 0.10, 0.04),
        _p("edge", 0.0, 0.36, 0.0, 0.04, 0.06, 0.03),
        _p("accent", 0.04, -0.26, 0.0, 0.06, 0.08, 0.01),
        _p("accent", -0.04, -0.26, 0.0, 0.06, 0.08, 0.01),
    ]


def shield(material: str) -> list[Part]:
    body = "head"
    return [
        _p(body, 0.0, 0.05, 0.0, 0.42, 0.52, 0.06),
        _p("edge", 0.0, 0.05, 0.0, 0.46, 0.56, 0.03),
        _p("accent", 0.0, 0.08, 0.04, 0.18, 0.22, 0.04),
        _p("handle", 0.0, 0.0, -0.08, 0.06, 0.16, 0.06),
        _p("binding", 0.0, -0.18, 0.02, 0.20, 0.08, 0.05),
    ]


def torch(_material: str) -> list[Part]:
    return [
        _p("handle", 0.0, -0.05, 0.0, 0.05, 0.45, 0.05),
        _p("head", 0.0, 0.20, 0.0, 0.08, 0.10, 0.08),
        _p("flame", 0.0, 0.30, 0.0, 0.07, 0.14, 0.07),
        _p("accent", 0.0, 0.36, 0.0, 0.04, 0.06, 0.04),
    ]


def apple(_material: str) -> list[Part]:
    return [
        _p("body", 0.0, 0.0, 0.0, 0.22, 0.22, 0.22),
        _p("stem", 0.0, 0.14, 0.0, 0.03, 0.08, 0.03),
        _p("leaf", 0.05, 0.14, 0.0, 0.08, 0.03, 0.05),
        _p("accent", 0.0, -0.02, 0.08, 0.06, 0.06, 0.04),
    ]


def bread(_material: str) -> list[Part]:
    return [
        _p("body", 0.0, 0.0, 0.0, 0.28, 0.12, 0.16),
        _p("accent", 0.0, 0.05, 0.0, 0.24, 0.04, 0.12),
        _p("edge", 0.0, -0.04, 0.0, 0.26, 0.04, 0.14),
    ]


def raw_meat(_material: str) -> list[Part]:
    return [
        _p("body", 0.0, 0.0, 0.0, 0.24, 0.10, 0.16),
        _p("accent", 0.04, 0.02, 0.02, 0.12, 0.06, 0.08),
        _p("edge", -0.08, -0.02, -0.02, 0.08, 0.05, 0.08),
    ]


def leather_item(_material: str) -> list[Part]:
    return [
        _p("leather", 0.0, 0.0, 0.0, 0.26, 0.04, 0.20),
        _p("accent", 0.0, 0.02, 0.0, 0.20, 0.02, 0.14),
        _p("binding", 0.10, 0.0, 0.0, 0.04, 0.05, 0.18),
    ]


def waterskin(_material: str) -> list[Part]:
    return [
        _p("leather", 0.0, -0.02, 0.0, 0.16, 0.22, 0.12),
        _p("water", 0.0, -0.02, 0.02, 0.10, 0.14, 0.06),
        _p("binding", 0.0, 0.12, 0.0, 0.08, 0.06, 0.08),
        _p("accent", 0.0, 0.16, 0.0, 0.05, 0.05, 0.05),
    ]


def armor_head(material: str) -> list[Part]:
    # Distinct helmet, not a crown.
    return [
        _p("head", 0.0, 0.08, 0.0, 0.34, 0.18, 0.36),
        _p("edge", 0.0, -0.02, 0.0, 0.36, 0.06, 0.38),
        _p("accent", 0.0, 0.14, 0.14, 0.20, 0.10, 0.08),
        _p("binding", 0.0, 0.02, -0.16, 0.28, 0.08, 0.06),
    ]


def armor_chest(material: str) -> list[Part]:
    return [
        _p("head", 0.0, 0.05, 0.0, 0.42, 0.48, 0.22),
        _p("edge", 0.0, 0.05, 0.0, 0.46, 0.52, 0.12),
        _p("accent", 0.0, 0.12, 0.10, 0.24, 0.20, 0.06),
        _p("binding", 0.18, 0.18, 0.0, 0.10, 0.14, 0.18),
        _p("binding", -0.18, 0.18, 0.0, 0.10, 0.14, 0.18),
    ]


def armor_legs(material: str) -> list[Part]:
    return [
        _p("head", 0.10, 0.0, 0.0, 0.14, 0.42, 0.16),
        _p("head", -0.10, 0.0, 0.0, 0.14, 0.42, 0.16),
        _p("edge", 0.10, -0.18, 0.0, 0.15, 0.08, 0.17),
        _p("edge", -0.10, -0.18, 0.0, 0.15, 0.08, 0.17),
        _p("accent", 0.0, 0.16, 0.0, 0.28, 0.08, 0.14),
    ]


def armor_feet(material: str) -> list[Part]:
    # Boots, not gloves.
    return [
        _p("head", 0.10, -0.02, 0.04, 0.14, 0.12, 0.24),
        _p("head", -0.10, -0.02, 0.04, 0.14, 0.12, 0.24),
        _p("edge", 0.10, -0.05, 0.12, 0.14, 0.08, 0.10),
        _p("edge", -0.10, -0.05, 0.12, 0.14, 0.08, 0.10),
        _p("binding", 0.10, 0.05, 0.0, 0.13, 0.05, 0.14),
        _p("binding", -0.10, 0.05, 0.0, 0.13, 0.05, 0.14),
    ]


FAMILY_BUILDERS = {
    "pickaxe": pickaxe,
    "axe": axe,
    "shovel": shovel,
    "hoe": hoe,
    "sword": sword,
    "bow": bow,
    "arrow": arrow,
    "shield": shield,
    "torch": torch,
    "apple": apple,
    "bread": bread,
    "raw_meat": raw_meat,
    "leather": leather_item,
    "waterskin": waterskin,
    "armor_head": armor_head,
    "armor_chest": armor_chest,
    "armor_legs": armor_legs,
    "armor_feet": armor_feet,
}


def build_parts(family: str, material: str) -> list[Part]:
    builder = FAMILY_BUILDERS.get(family)
    if builder is None:
        raise KeyError(f"unknown item family: {family}")
    return builder(material)
