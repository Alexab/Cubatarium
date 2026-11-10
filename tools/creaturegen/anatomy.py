"""Author box anatomy in model pixels (1/16 block). No imported meshes."""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Cube:
    origin: list[int]
    size: list[int]
    role: str
    uv: tuple[int, int] = (0, 0)

    def top(self) -> int:
        return self.origin[1] + self.size[1]


@dataclass
class Bone:
    name: str
    parent: str | None
    pivot: list[int]
    cubes: list[Cube] = field(default_factory=list)
    mirror: bool = False
    bind_rotation: list[float] | None = None


@dataclass
class Model:
    bones: list[Bone]
    texture_size: tuple[int, int] = (64, 64)

    def cubes(self) -> list[Cube]:
        found: list[Cube] = []
        for bone in self.bones:
            found.extend(bone.cubes)
        return found


def _i(value: float) -> int:
    return max(1, int(round(value)))


def _triple(values, fallback: tuple[int, int, int]) -> tuple[int, int, int]:
    if not values:
        return fallback
    nums = [int(v) for v in values]
    while len(nums) < 3:
        nums.append(nums[-1])
    return _i(nums[0]), _i(nums[1]), _i(nums[2])


def _even(value: int) -> int:
    value = int(value)
    return value if value % 2 == 0 else value + 1


def _mirror_x(sign: int, gap: int, width: int) -> int:
    """Origin x of a cube mirrored across the body axis.

    gap is the distance from x=0 to the inner face. Odd widths stay symmetric:
    the old pattern ``sign * c - width // 2`` shoved both feet toward +x.
    """
    if sign < 0:
        return -(gap + width)
    return gap


def _cube(origin, size, role: str) -> Cube:
    ox, oy, oz = [int(round(v)) for v in origin]
    sx, sy, sz = [_i(v) for v in size]
    return Cube([ox, oy, oz], [sx, sy, sz], role)


def _bone(name, parent, pivot, cubes, mirror=False, bind=None) -> Bone:
    return Bone(name, parent, [int(round(v)) for v in pivot], list(cubes), mirror, bind)


def _tokens(recipe: dict) -> list[str]:
    extras = recipe.get("extras") or []
    if isinstance(extras, str):
        extras = [part.strip() for part in extras.split(",") if part.strip()]
    return list(extras)


def _pack(model: Model) -> None:
    x = 1
    y = 1
    row_h = 0
    width = 64
    for cube in model.cubes():
        fw = 2 * (cube.size[0] + cube.size[2]) + 1
        fh = cube.size[1] + cube.size[2] + 1
        if x + fw >= width:
            x = 1
            y += row_h + 1
            row_h = 0
        if y + fh >= width:
            width *= 2
        cube.uv = (x, y)
        x += fw
        row_h = max(row_h, fh)
    height = max(width, y + row_h + 2)
    # Box UV reads downward from the top of the atlas in the same pixel grid.
    model.texture_size = (width, max(width, height))


def _quadruped(recipe: dict, names: dict[str, str] | None = None) -> Model:
    names = names or {}
    body_n = names.get("body", "body")
    bw, bh, bd = _triple(recipe.get("body"), (12, 8, 16))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (7, 7, 7))
    lw, lh = _triple(recipe.get("limb"), (4, 10, 4))[:2]
    tokens = _tokens(recipe)
    muzzle = _triple(recipe.get("muzzle"), (max(3, hw - 2), max(2, hh // 3), max(2, hd // 2)))
    body_bottom = max(lh - 2, 4)
    bind = recipe.get("bind_body")
    bones: list[Bone] = []
    pivot = [0, body_bottom + bh // 2, 0]
    origin = [-bw // 2, body_bottom, -bd // 2]
    body_cubes = [
        _cube(origin, (bw, bh, bd), "wool" if "wool" in tokens else "body"),
        _cube((-bw // 2 + 1, body_bottom, -bd // 4), (bw - 2, max(2, bh // 3), bd // 2), "belly"),
    ]
    if "shoulders" in tokens:
        body_cubes.append(_cube((-bw // 2 - 1, body_bottom + bh // 2, -bd // 2), (bw + 2, max(3, bh // 3), max(3, bd // 5)), "body"))
    if "hump" in tokens:
        body_cubes.append(_cube((-bw // 4, body_bottom + bh - 1, -bd // 5), (bw // 2, max(3, bh // 3), bd // 3), "accent"))
    if "spines" in tokens:
        body_cubes.append(_cube((-bw // 3, body_bottom + bh, -bd // 3), (max(2, bw // 2), max(3, bh // 2), max(2, bd // 2)), "horn"))
    if "shell" in tokens:
        body_cubes.append(_cube((-bw // 2 - 1, body_bottom + bh // 2, -bd // 2), (bw + 2, max(3, bh // 2), bd), "shell"))
    bones.append(_bone(body_n, None, pivot, body_cubes, bind=list(bind) if bind else None))
    shoulder_y = body_bottom + bh - 1
    head_pivot = [0, shoulder_y, -bd // 2]
    head_origin = [-hw // 2, head_pivot[1] - hh // 3, head_pivot[2] - hd]
    head_cubes = [_cube(head_origin, (hw, hh, hd), "face")]
    if "muzzle" in tokens or recipe.get("muzzle"):
        head_cubes.append(_cube((-muzzle[0] // 2, head_origin[1] + hh // 5, head_origin[2] - muzzle[2]), muzzle, "muzzle"))
    if "horns" in tokens:
        horn_h = max(2, hh // 2)
        head_cubes.append(_cube((-hw // 2, head_origin[1] + hh - 1, head_origin[2] + hd // 3), (2, horn_h, 2), "horn"))
        head_cubes.append(_cube((hw // 2 - 2, head_origin[1] + hh - 1, head_origin[2] + hd // 3), (2, horn_h, 2), "horn"))
    if "ears" in tokens:
        head_cubes.append(_cube((-hw // 2 - 1, head_origin[1] + hh // 2, head_origin[2] + 1), (2, max(3, hh // 2), 2), "face"))
        head_cubes.append(_cube((hw // 2 - 1, head_origin[1] + hh // 2, head_origin[2] + 1), (2, max(4, hh // 2 + 1), 2), "face"))
    if "tusks" in tokens:
        head_cubes.append(_cube((-2, head_origin[1], head_origin[2] - 2), (1, 3, 1), "horn"))
        head_cubes.append(_cube((1, head_origin[1], head_origin[2] - 2), (1, 3, 1), "horn"))
    if "beak" in tokens:
        head_cubes.append(_cube((-1, head_origin[1] + hh // 3, head_origin[2] - 3), (2, 2, 3), "accent"))
    bones.append(_bone(names.get("head", "head"), body_n, head_pivot, head_cubes))
    spreads = ((-1, -1), (1, -1), (-1, 1), (1, 1))
    leg_names = [names.get(f"leg{i}", f"leg{i}") for i in range(4)]
    gap_x = max(0, bw // 2 - lw)
    gap_z = max(0, bd // 2 - lw - max(1, bd // 8))
    for index, (sx, sz) in enumerate(spreads):
        ox = _mirror_x(sx, gap_x, lw)
        oz = _mirror_x(sz, gap_z, lw)
        pivot = [ox + lw / 2, lh, oz + lw / 2]
        bones.append(_bone(leg_names[index], body_n, pivot, [_cube((ox, 0, oz), (lw, lh, lw), "limb")], mirror=sx > 0))
    tail_len = max(3, bd // 3)
    tail_pivot = [0, body_bottom + bh // 2, bd // 2]
    bones.append(_bone(names.get("tail", "tail"), body_n, tail_pivot, [
        _cube((-1, tail_pivot[1] - 1, tail_pivot[2]), (2, 2, tail_len), "accent")
    ]))
    return Model(bones)


def _chicken(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (8, 8, 10))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (5, 6, 5))
    lw, lh = _triple(recipe.get("limb"), (2, 6, 2))[:2]
    body_bottom = lh
    pivot = [0, body_bottom + bh // 2, 0]
    bones = [
        _bone("body", None, pivot, [
            _cube((-bw // 2, body_bottom, -bd // 2), (bw, bh, bd), "body"),
            _cube((-bw // 4, body_bottom + bh - 2, bd // 5), (bw // 2, 3, max(3, bd // 3)), "accent"),
        ]),
        _bone("head", "body", [0, body_bottom + bh, -bd // 2], [
            _cube((-hw // 2, body_bottom + bh - 1, -bd // 2 - hd), (hw, hh, hd), "face"),
            _cube((-2, body_bottom + bh + hh - 3, -bd // 2 - hd - 1), (4, 2, 2), "accent"),
            _cube((-1, body_bottom + bh, -bd // 2 - hd - 3), (2, 2, 3), "horn"),
        ]),
    ]
    for index, sx in ((0, -1), (1, 1)):
        gap_x = max(1, bw // 2 - lw)
        ox = _mirror_x(sx, gap_x, lw)
        oz = -lw // 2
        bones.append(_bone(f"leg{index}", "body", [ox + lw / 2, lh, 0], [
            _cube((ox, 0, oz), (lw, lh, lw), "limb")
        ]))
    for index, sx in ((0, -1), (1, 1)):
        pivot_w = [sx * (bw // 2), body_bottom + bh // 2, 0]
        bones.append(_bone(f"wing{index}", "body", pivot_w, [
            _cube([pivot_w[0] + (0 if sx < 0 else -1), pivot_w[1] - 1, -bd // 4], (1, max(3, bh // 2), bd // 2), "wing")
        ]))
    return Model(bones)


def _rabbit(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (8, 7, 12))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (5, 5, 5))
    body_bottom = 6
    bones = [
        _bone("body", None, [0, body_bottom + bh // 2, 1], [
            _cube((-bw // 2, body_bottom, -bd // 3), (bw, bh, bd), "body"),
            _cube((-bw // 2 + 1, body_bottom, -1), (bw - 2, max(2, bh // 3), bd // 3), "belly"),
        ]),
        _bone("head", "body", [0, body_bottom + bh, -bd // 3], [
            _cube((-hw // 2, body_bottom + bh - 1, -bd // 3 - hd), (hw, hh, hd), "face"),
        ]),
    ]
    ear_h = max(6, hh + 2)
    bones.append(_bone("earLeft", "head", [-2, body_bottom + bh + hh - 2, -bd // 3 - 1], [
        _cube((-3, body_bottom + bh + hh - 2, -bd // 3 - 1), (2, ear_h, 1), "accent")
    ]))
    bones.append(_bone("earRight", "head", [2, body_bottom + bh + hh - 2, -bd // 3 - 1], [
        _cube((1, body_bottom + bh + hh - 2, -bd // 3 - 1), (2, ear_h + 1, 1), "face")
    ]))
    leg_w = 4
    gap_x = max(1, bw // 2 - leg_w)
    rear_z = bd // 5
    front_z = -bd // 3
    for name, sx in (("haunchLeft", -1), ("haunchRight", 1)):
        ox = _mirror_x(sx, gap_x, leg_w)
        bones.append(_bone(name, "body", [ox + leg_w / 2, body_bottom, rear_z], [
            _cube((ox, 0, rear_z), (leg_w, body_bottom + 3, 5), "limb")
        ]))
    for name, sx in (("rearFootLeft", -1), ("rearFootRight", 1)):
        ox = _mirror_x(sx, gap_x, leg_w)
        bones.append(_bone(name, "body", [ox + leg_w / 2, 2, rear_z], [
            _cube((ox + 1, 0, rear_z), (leg_w - 2, 2, 4), "limb")
        ]))
    for name, sx in (("frontLegLeft", -1), ("frontLegRight", 1)):
        ox = _mirror_x(sx, gap_x, leg_w)
        bones.append(_bone(name, "body", [ox + leg_w / 2, body_bottom, front_z], [
            _cube((ox, 0, front_z), (leg_w, body_bottom, leg_w), "limb")
        ]))
    bones.append(_bone("tail", "body", [0, body_bottom + 2, bd // 2], [
        _cube((-1, body_bottom + 1, bd // 2), (2, 2, 2), "belly")
    ]))
    return Model(bones)


def _arachnid(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (10, 6, 12))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (6, 4, 5))
    lw, lh = _triple(recipe.get("limb"), (2, 8, 2))[:2]
    body_y = max(4, lh // 2)
    bones = [
        _bone("body0", None, [0, body_y + bh // 2, 2], [
            _cube((-bw // 2, body_y, -1), (bw, bh, bd // 2 + 2), "body"),
            _cube((-bw // 3, body_y + 1, bd // 3), (max(4, bw // 2), bh, max(4, bd // 3)), "accent"),
        ]),
        _bone("head", "body0", [0, body_y + 1, -2], [
            _cube((-hw // 2, body_y, -hd - 1), (hw, hh, hd), "face"),
        ]),
    ]
    for index in range(8):
        side = -1 if index % 2 == 0 else 1
        along = -bd // 3 + (index // 2) * (bd // 4)
        ox = _mirror_x(side, max(0, bw // 2 - lw), lw)
        bones.append(_bone(f"leg{index}", "body0", [ox + lw / 2, body_y + 1, along], [
            _cube((ox, 0, along - lw // 2), (lw, lh, lw), "limb")
        ], mirror=side > 0))
    return Model(bones)


def _humanoid(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (8, 12, 4))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (8, 8, 8))
    lw, lh = _triple(recipe.get("limb"), (4, 12, 4))[:2]
    tokens = _tokens(recipe)
    hip = lh
    bones = [
        _bone("waist", None, [0, hip, 0], [
            _cube((-_even(max(4, bw // 2)) // 2, hip - 2, -bd // 2), (_even(max(4, bw // 2)), 3, bd), "body"),
        ]),
    ]
    body_cubes = [_cube((-bw // 2, hip, -bd // 2), (bw, bh, bd), "body")]
    if "pauldron" in tokens:
        body_cubes.append(_cube((-bw // 2 - 1, hip + bh - 4, -bd // 2 - 1), (bw + 2, 4, bd + 2), "shell"))
    if "jaw" in tokens:
        body_cubes.append(_cube((-bw // 4, hip + bh - 1, -bd), (bw // 2, 3, 3), "accent"))
    if "belly" in tokens:
        body_cubes.append(_cube((-bw // 2 + 1, hip, -bd // 2 + 1), (bw - 2, max(3, bh // 2), max(2, bd // 2)), "belly"))
    bones.append(_bone("body", "waist", [0, hip + bh // 2, 0], body_cubes))
    head_y = hip + bh
    head_cubes = [_cube((-hw // 2, head_y, -hd // 2), (hw, hh, hd), "face")]
    if "crest" in tokens:
        head_cubes.append(_cube((-1, head_y + hh - 1, -1), (2, max(3, hh // 2), 2), "accent"))
    if "hat" in tokens or True:
        head_cubes.append(_cube((-hw // 2 - 1, head_y + hh - 2, -hd // 2 - 1), (hw + 2, 2, hd + 2), "horn"))
    bones.append(_bone("head", "waist", [0, head_y, 0], head_cubes))
    bones.append(_bone("hat", "head", [0, head_y + hh - 1, 0], [
        _cube((-hw // 2, head_y + hh - 1, -hd // 2), (hw, 2, hd), "accent")
    ]))
    for name, sx in (("rightArm", -1), ("leftArm", 1)):
        ox = _mirror_x(sx, bw // 2, lw)
        pivot = [ox + lw / 2, hip + bh - 2, 0]
        origin = [ox, hip, -lw // 2]
        arm_h = bh
        if name == "rightArm" and "long_arm" in tokens:
            arm_h = bh + 3
            origin[1] = hip - 3
        bones.append(_bone(name, "waist", pivot, [_cube(origin, (lw, arm_h, lw), "limb")], mirror=sx > 0))
        hand_y = origin[1]
        item = "rightItem" if sx < 0 else "leftItem"
        bones.append(_bone(item, name, [pivot[0], hand_y, 0], [
            _cube((pivot[0] - 1, hand_y - 2, -1), (2, 2, 2), "horn")
        ]))
    for name, sx in (("rightLeg", -1), ("leftLeg", 1)):
        gap = max(0, bw // 2 - lw)
        ox = _mirror_x(sx, gap, lw)
        bones.append(_bone(name, "waist", [ox + lw / 2, hip, 0], [
            _cube((ox, 0, -lw // 2), (lw, lh, lw), "limb")
        ], mirror=sx > 0))
    return Model(bones)


def _aerial(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (7, 6, 10))
    hw, hh, hd = _triple(recipe.get("head"), (5, 4, 4))
    tokens = _tokens(recipe)
    bones = [
        _bone("body", None, [0, 8, 0], [
            _cube((-bw // 2, 6, -bd // 2), (bw, bh, bd), "body"),
        ]),
        _bone("head", "body", [0, 9, -bd // 3], [
            _cube((-hw // 2, 8, -bd // 3 - hd), (hw, hh, hd), "face"),
        ]),
    ]
    wing_w = 8 if "broad_wings" in tokens else 5
    # wing0/wing1 are the visible slabs. The alternate bone names exist for the
    # aerial pose and stay inside the body so they do not draw a second wing.
    for name, sx, visible in (
        ("wing0", -1, True), ("wing1", 1, True),
        ("rightwing_bone", -1, False), ("leftwing_bone", 1, False),
    ):
        pivot = [sx * (bw // 2), 9, 0]
        if visible:
            cube = _cube((pivot[0] - (wing_w if sx < 0 else 0), 8, -3), (wing_w, 2, 7), "wing")
        else:
            cube = _cube((-1, 7, -1), (2, 2, 2), "body")
        bones.append(_bone(name, "body", pivot, [cube]))
    return Model(bones)


def _aquatic(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (8, 8, 18))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (6, 6, 6))
    tokens = _tokens(recipe)
    nose = bd // 2
    bones = [
        _bone("body", None, [0, bh, 0], [
            _cube((-bw // 2, 2, -bd // 2), (bw, bh, bd), "body"),
            _cube((-bw // 2 + 1, 2, -bd // 4), (bw - 2, max(2, bh // 3), bd // 2), "belly"),
        ]),
        _bone("head", "body", [0, bh, -nose], [
            _cube((-hw // 2, bh - hh // 2, -nose - hd), (hw, hh, hd), "face"),
        ]),
        _bone("body_back", "body", [0, bh // 2 + 2, bd // 4], [
            _cube((-max(2, bw // 3), 3, bd // 5), (max(4, bw // 2), max(3, bh - 2), max(4, bd // 4)), "body"),
        ]),
        _bone("tail", "body_back", [0, bh // 2 + 2, bd // 2], [
            _cube((-2, bh // 2, bd // 2), (4, 3, max(4, bd // 4)), "accent"),
        ]),
        _bone("tailfin", "tail", [0, bh // 2 + 2, bd // 2 + 3], [
            _cube((-4, bh // 2, bd // 2 + 3), (8, 1, 4), "wing"),
        ]),
        _bone("tail_fin", "tail", [0, bh // 2 + 3, bd // 2 + 3], [
            _cube((-3, bh // 2 + 2, bd // 2 + 2), (6, 2, 3), "wing"),
        ]),
    ]
    if "fins" in tokens:
        for sx, name in ((-1, "fin_l"), (1, "fin_r")):
            ox = _mirror_x(sx, bw // 2, 4)
            bones.append(_bone(name, "body", [ox + 2, bh, 0], [
                _cube((ox, bh - 1, -2), (4, 2, 4), "wing")
            ]))
    if "tentacles" in tokens:
        for index, gap, length in ((0, 1, 6), (1, 1, 6), (2, 3, 8), (3, 3, 8)):
            sx = -1 if index % 2 == 0 else 1
            ox = _mirror_x(sx, gap, 2)
            bones.append(_bone(f"arm{index}", "body", [ox + 1, 2, -8], [
                _cube((ox, 0, -8), (2, 2, length), "limb")
            ]))
    return Model(bones)


def _serpentine(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (5, 5, 8))
    bones = [_bone("body", None, [0, bh, 0], [
        _cube((-bw // 2, 2, -bd), (bw, bh, bd), "body")
    ])]
    parent = "body"
    for index, name in enumerate(("seg1", "seg2", "seg3", "tail")):
        z = index * bd
        bones.append(_bone(name, parent, [0, bh, z], [
            _cube((-bw // 2 + index, 2, z), (max(2, bw - index), max(2, bh - index // 2), bd), "accent" if index else "body")
        ]))
        parent = name
    bones.append(_bone("head", "body", [0, bh + 1, -bd], [
        _cube((-bw // 2, bh, -bd - 4), (bw, bh, 4), "face")
    ]))
    return Model(bones)


def _crab(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (14, 4, 10))
    bw = _even(bw)
    tokens = _tokens(recipe)
    bones = [
        _bone("body", None, [0, 4, 0], [
            _cube((-bw // 2, 3, -bd // 2), (bw, bh, bd), "shell"),
        ]),
        _bone("head", "body", [0, 5, -bd // 2], [
            _cube((-3, 4, -bd // 2 - 3), (6, 3, 3), "face"),
        ]),
    ]
    if "shell" in tokens:
        bones.append(_bone("shell", "body", [0, 6, 1], [
            _cube((-bw // 2 - 1, 6, -bd // 3), (bw + 2, max(4, bh), bd), "horn")
        ]))
    reach_l, reach_r = (6, 4) if "uneven_claws" in tokens else (5, 5)
    for name, sx, reach in (("claw_l", -1, reach_l), ("claw_r", 1, reach_r)):
        ox = _mirror_x(sx, bw // 2, reach)
        bones.append(_bone(name, "body", [ox + reach / 2, 4, -2], [
            _cube((ox, 2, -bd // 2 - 3), (reach, 3, 3), "accent")
        ]))
    leg_w = 3
    gap_x = max(0, bw // 2 - leg_w)
    for index in range(6):
        side = -1 if index % 2 == 0 else 1
        z = -bd // 3 + (index // 2) * (bd // 3)
        ox = _mirror_x(side, gap_x, leg_w)
        bones.append(_bone(f"leg{index}", "body", [ox + leg_w / 2, 3, z], [
            _cube((ox, 0, z - 1), (leg_w, 4, 2), "limb")
        ]))
    return Model(bones)


def _blob(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (12, 6, 12))
    return Model([
        _bone("body", None, [0, bh // 2, 0], [
            _cube((-bw // 2, 1, -bd // 2), (bw, bh, bd), "body"),
            _cube((-bw // 4, bh // 2, -bd // 4), (bw // 2, max(2, bh // 3), bd // 2), "accent"),
        ]),
        _bone("head", "body", [0, bh, -bd // 4], [
            _cube((-3, bh - 1, -bd // 2), (6, 4, 4), "face"),
        ]),
    ])


def _octopus(recipe: dict) -> Model:
    model = _aquatic(recipe)
    layout = (
        (0, -1, 2, 6, -4), (1, 1, 2, 6, -4),
        (2, -1, 5, 8, -4), (3, 1, 5, 8, -4),
        (4, -1, 2, 7, -9), (5, 1, 2, 7, -9),
        (6, -1, 5, 8, -9), (7, 1, 5, 8, -9),
    )
    for index, sx, gap, length, z0 in layout:
        ox = _mirror_x(sx, gap, 2)
        model.bones.append(_bone(f"tentacle{index}", "body", [ox + 1, 2, z0], [
            _cube((ox, 0, z0 - length), (2, 2, length), "limb")
        ]))
    return model


def _ray(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (18, 2, 14))
    return Model([
        _bone("body", None, [0, 2, 0], [
            _cube((-bw // 2, 1, -bd // 2), (bw, bh, bd), "body"),
            _cube((-bw // 5, 1, -bd // 5), (max(3, bw // 4), bh, max(3, bd // 3)), "accent"),
        ]),
        _bone("head", "body", [0, 2, -bd // 2], [
            _cube((-3, 1, -bd // 2 - 2), (6, 2, 3), "face"),
        ]),
        _bone("wing0", "body", [-bw // 4, 2, 0], [
            _cube((-bw // 2, 2, -bd // 3), (bw // 3, 1, bd // 2), "wing")
        ]),
        _bone("wing1", "body", [bw // 4, 2, 0], [
            _cube((bw // 6, 2, -bd // 3), (bw // 3, 1, bd // 2), "wing")
        ]),
        _bone("tail", "body", [0, 2, bd // 2], [
            _cube((-1, 1, bd // 2), (2, 1, max(6, bd // 2)), "accent")
        ]),
    ])


def _sprite(recipe: dict) -> Model:
    return Model([
        _bone("body", None, [0, 8, 0], [
            _cube((-4, 2, -1), (8, 12, 2), "accent"),
        ])
    ])


BUILDERS = {
    "quadruped": _quadruped,
    "chicken": _chicken,
    "rabbit": _rabbit,
    "arachnid": _arachnid,
    "humanoid": _humanoid,
    "aerial": _aerial,
    "aquatic": _aquatic,
    "serpentine": _serpentine,
    "crab": _crab,
    "blob": _blob,
    "octopus": _octopus,
    "ray": _ray,
    "sprite": _sprite,
}


def _scale_recipe(recipe: dict, scale: tuple[float, float, float] | None) -> dict:
    if not scale:
        return recipe
    copied = dict(recipe)
    for key in ("body", "head", "limb", "muzzle"):
        if copied.get(key):
            copied[key] = [max(1, int(round(float(value) * float(scale[index % 3]))))
                           for index, value in enumerate(copied[key])]
    return copied


def model_span_blocks(model: Model) -> float:
    cubes = model.cubes()
    if not cubes:
        return 1.0
    span = []
    for axis in range(3):
        lo = min(cube.origin[axis] for cube in cubes)
        hi = max(cube.origin[axis] + cube.size[axis] for cube in cubes)
        span.append(hi - lo)
    return max(1.0, float(max(span)) / 16.0)


def build_model(recipe: dict, target_px: int | None = None, concept_scale=None,
                bounds: tuple[float, float, float] | None = None) -> Model:
    del target_px, bounds
    template = recipe.get("template") or "quadruped"
    builder = BUILDERS[template]
    model = builder(_scale_recipe(recipe, concept_scale))
    _pack(model)
    return model
