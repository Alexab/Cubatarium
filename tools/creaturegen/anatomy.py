"""Author box anatomy in model pixels (1/16 block). No imported meshes."""

from __future__ import annotations

from dataclasses import dataclass, field
from copy import deepcopy


@dataclass
class Cube:
    origin: list[int]
    size: list[int]
    role: str
    uv: tuple[int, int] = (0, 0)
    name: str = ""
    shape: dict = field(default_factory=dict)
    blend: dict = field(default_factory=dict)

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
    motion: dict = field(default_factory=dict)

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


def _sil(recipe: dict) -> dict:
    return recipe.get("silhouette") or {}


def _tokens(recipe: dict) -> list[str]:
    extras = recipe.get("extras") or []
    if isinstance(extras, str):
        extras = [part.strip() for part in extras.split(",") if part.strip()]
    return list(extras)


def _pack(model: Model, layout: dict | None = None) -> None:
    # Repack from the beginning when growing. Changing the width midway leaves
    # poorly utilised atlases and can still overflow a very wide island.
    layout = layout or {}
    slots = layout.get("slots", {})
    width = max(64, int(layout.get("minimum_size", 64)))
    reserved_bottom = 0
    for cube in model.cubes():
        if cube.name in slots:
            u, v = slots[cube.name]
            if min(u, v) < 0 or u+2*(cube.size[0]+cube.size[2]) > width:
                raise ValueError("Invalid reserved runtime UV slot")
            reserved_bottom = max(reserved_bottom, v+cube.size[1]+cube.size[2]+2)
    while True:
        x, y, row_h = 1, max(1, reserved_bottom), 0
        placements = []
        for cube in model.cubes():
            if cube.name in slots:
                placements.append((cube, tuple(slots[cube.name])))
                continue
            fw = 2 * (cube.size[0] + cube.size[2])
            fh = cube.size[1] + cube.size[2]
            if fw + 2 > width:
                break
            if x + fw + 1 > width:
                x, y, row_h = 1, y + row_h + 2, 0
            if y + fh + 1 > width:
                break
            placements.append((cube, (x, y)))
            x += fw + 2
            row_h = max(row_h, fh)
        else:
            for cube, uv in placements:
                cube.uv = uv
            model.texture_size = (width, width)
            return
        width *= 2
        if width > 2048:
            raise ValueError("Authored anatomy exceeds the atlas budget")


def _quadruped(recipe: dict, names: dict[str, str] | None = None) -> Model:
    names = names or {}
    body_n = names.get("body", "body")
    bw, bh, bd = _triple(recipe.get("body"), (12, 8, 16))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (7, 7, 7))
    lw, lh = _triple(recipe.get("limb"), (4, 10, 4))[:2]
    tokens = _tokens(recipe)
    sil = _sil(recipe)
    muzzle = _triple(recipe.get("muzzle"), (max(3, hw - 2), max(2, hh // 3), max(2, hd // 2)))
    if sil.get("snout"):
        muzzle = (
            max(muzzle[0], int(hw * float(sil.get("snout_width") or 0.85))),
            muzzle[1],
            max(2, int(hd * float(sil["snout"]))),
        )
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
    if "shell" in tokens or sil.get("shell"):
        shell = float(sil.get("shell") or 1.15)
        pad = max(1, int(min(bw, bd) * (shell - 1)))
        body_cubes.append(_cube((-bw // 2 - pad, body_bottom + bh // 3, -bd // 2 - pad), (bw + 2 * pad, max(4, int(bh * 0.7)), bd + 2 * pad), "shell"))
    if sil.get("wool"):
        pad = max(1, int(min(bw, bh) * (float(sil["wool"]) - 1)))
        body_cubes.append(_cube((-bw // 2 - pad, body_bottom - 1, -bd // 2 - pad), (bw + 2 * pad, bh + pad + 2, bd + 2 * pad), "wool"))
    bones.append(_bone(body_n, None, pivot, body_cubes, bind=list(bind) if bind else None))
    shoulder_y = body_bottom + bh - 1
    head_pivot = [0, shoulder_y, -bd // 2]
    head_origin = [-hw // 2, head_pivot[1] - hh // 3, head_pivot[2] - hd]
    head_cubes = [_cube(head_origin, (hw, hh, hd), "face")]
    if "muzzle" in tokens or recipe.get("muzzle"):
        head_cubes.append(_cube((-muzzle[0] // 2, head_origin[1] + hh // 5, head_origin[2] - muzzle[2]), muzzle, "muzzle"))
    if "horns" in tokens or sil.get("horns"):
        horn_h = max(3, int(hh * float(sil.get("horns") or 0.7)))
        horn_w = max(3, hw // 3)
        for sx in (-1, 1):
            ox = _mirror_x(sx, hw // 2, horn_w)
            head_cubes.append(_cube((ox, head_origin[1] + hh - 2, head_origin[2] + hd // 3), (horn_w, horn_h, 2), "horn"))
    if "ears" in tokens or sil.get("ears"):
        ear_h = max(3, int(hh * float(sil.get("ears") or 0.6)))
        for sx in (-1, 1):
            ox = _mirror_x(sx, max(0, hw // 2 - 1), 2)
            head_cubes.append(_cube((ox, head_origin[1] + hh - 2, head_origin[2] + 1), (2, ear_h, 2), "face"))
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
    tail_len = max(3, int(bd * float(sil["tail"]))) if sil.get("tail") else max(3, bd // 3)
    drop = int(bh * float(sil.get("tail_drop") or 0))
    tail_pivot = [0, body_bottom + bh // 2 - drop, bd // 2]
    thick = 4 if float(sil.get("tail") or 0) >= 0.6 else 2
    bones.append(_bone(names.get("tail", "tail"), body_n, tail_pivot, [
        _cube((-thick // 2, tail_pivot[1] - thick // 2, tail_pivot[2]), (thick, thick, tail_len), "accent")
    ]))
    return Model(bones)


def _chicken(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (8, 8, 10))
    bw = _even(bw)
    hw, hh, hd = _triple(recipe.get("head"), (5, 6, 5))
    lw, lh = _triple(recipe.get("limb"), (2, 6, 2))[:2]
    body_bottom = lh
    sil = _sil(recipe)
    tokens = _tokens(recipe)
    pivot = [0, body_bottom + bh // 2, 0]
    head_y = body_bottom + bh - 1
    face_z = -bd // 2 - hd
    head_cubes = [_cube((-hw // 2, head_y, face_z), (hw, hh, hd), "face")]
    if sil.get("comb"):
        comb_h = max(2, int(hh * float(sil["comb"])))
        head_cubes.append(_cube((-2, head_y + hh - 1, face_z), (4, comb_h, 2), "accent"))
    if sil.get("beak") or "beak" in tokens:
        beak_d = max(2, int(hd * float(sil.get("beak") or 0.4)))
        head_cubes.append(_cube((-1, head_y + hh // 3, face_z - beak_d), (2, 2, beak_d), "horn"))
    if sil.get("disc"):
        pad = max(1, int(min(hw, hh) * max(0.1, float(sil["disc"]) - 1)))
        head_cubes.append(_cube((-hw // 2 - pad, head_y, face_z - 1), (hw + 2 * pad, hh, 1), "face"))
    if sil.get("tufts"):
        tuft_h = max(3, int(hh * float(sil["tufts"])))
        for sx in (-1, 1):
            ox = _mirror_x(sx, max(0, hw // 2 - 1), 2)
            head_cubes.append(_cube((ox, head_y + hh - 2, face_z + 1), (2, tuft_h, 2), "accent"))
    bones = [
        _bone("body", None, pivot, [
            _cube((-bw // 2, body_bottom, -bd // 2), (bw, bh, bd), "body"),
            _cube((-bw // 4, body_bottom + bh - 2, bd // 5), (bw // 2, 3, max(3, bd // 3)), "body"),
        ]),
        _bone("head", "body", [0, body_bottom + bh, -bd // 2], head_cubes),
    ]
    for index, sx in ((0, -1), (1, 1)):
        gap_x = max(1, bw // 2 - lw)
        ox = _mirror_x(sx, gap_x, lw)
        oz = -lw // 2
        bones.append(_bone(f"leg{index}", "body", [ox + lw / 2, lh, 0], [
            _cube((ox, 0, oz), (lw, lh, lw), "limb")
        ]))
    for index, sx in ((0, -1), (1, 1)):
        ox = _mirror_x(sx, bw // 2, 1)
        pivot_w = [ox + 0.5, body_bottom + bh // 2, 0]
        bones.append(_bone(f"wing{index}", "body", pivot_w, [
            _cube((ox, pivot_w[1] - 1, -bd // 4), (1, max(3, bh // 2), max(2, bd // 3)), "wing")
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
    ear_h = max(6, int(hh * float(_sil(recipe).get("ears") or 1.4)))
    ear_y = body_bottom + bh + hh - 2
    ear_z = -bd // 3 - 1
    for name, sx in (("earLeft", -1), ("earRight", 1)):
        ox = _mirror_x(sx, 1, 2)
        bones.append(_bone(name, "head", [ox + 1, ear_y, ear_z], [
            _cube((ox, ear_y, ear_z), (2, ear_h, 1), "accent")
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
            _cube((ox, 0, rear_z), (leg_w, 2, 4), "limb")
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
        splay = float(_sil(recipe).get("leg_splay") or 0)
        if splay:
            reach = max(lw, int(lh * splay))
            ox = _mirror_x(side, bw // 2, reach)
            bones.append(_bone(f"leg{index}", "body0", [ox + reach / 2, body_y, along], [
                _cube((ox, body_y - 1, along - lw // 2), (reach, 2, lw), "limb")
            ], mirror=side > 0))
        else:
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
    show_hat = "hat" in tokens or "hat" in (recipe.get("marks") or [])
    if show_hat:
        head_cubes.append(_cube((-hw // 2 - 1, head_y + hh - 2, -hd // 2 - 1), (hw + 2, 2, hd + 2), "horn"))
    bones.append(_bone("head", "waist", [0, head_y, 0], head_cubes))
    if show_hat:
        hat_cube = _cube((-hw // 2, head_y + hh - 1, -hd // 2), (hw, 2, hd), "accent")
    else:
        hat_cube = _cube((0, head_y + hh // 2, 0), (1, 1, 1), "face")
    bones.append(_bone("hat", "head", [0, head_y + hh - 1, 0], [hat_cube]))
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


def _bee_segments(bw: int, bh: int, bd: int, sil: dict) -> list:
    count = max(3, int(sil.get("segments") or 3))
    span = max(count, bd // count)
    cubes = []
    for index in range(count):
        role = "body" if index % 2 == 0 else "accent"
        z = -bd // 2 + index * (span - 1)
        cubes.append(_cube((-bw // 2, 6, z), (bw if index == 1 else max(3, bw - 1), bh - (0 if index == 1 else 1), span), role))
    return cubes


def _aerial(recipe: dict) -> Model:
    bw, bh, bd = _triple(recipe.get("body"), (7, 6, 10))
    hw, hh, hd = _triple(recipe.get("head"), (5, 4, 4))
    tokens = _tokens(recipe)
    bones = [
        _bone("body", None, [0, 8, 0], _bee_segments(bw, bh, bd, _sil(recipe)) if int(_sil(recipe).get("segments") or 1) >= 3 else [
            _cube((-bw // 2, 6, -bd // 2), (bw, bh, bd), "body"),
        ]),
        _bone("head", "body", [0, 9, -bd // 3], [
            _cube((-hw // 2, 8, -bd // 3 - hd), (hw, hh, hd), "face"),
        ]),
    ]
    wing_scale = _sil(recipe).get("wing")
    wing_w = max(3, int(bw * float(wing_scale if wing_scale is not None else (0.9 if "broad_wings" in tokens else 0.6))))
    wing_d = 4 if wing_scale is not None else 7
    wing_y = 6 + bh - 1 if int(_sil(recipe).get("segments") or 1) >= 3 else 8
    # wing0/wing1 are the visible slabs. The alternate bone names exist for the
    # aerial pose and stay inside the body so they do not draw a second wing.
    for name, sx, visible in (
        ("wing0", -1, True), ("wing1", 1, True),
        ("rightwing_bone", -1, False), ("leftwing_bone", 1, False),
    ):
        pivot = [sx * (bw // 2), wing_y, 0]
        if visible:
            cube = _cube((pivot[0] - (wing_w if sx < 0 else 0), wing_y, -wing_d // 2), (wing_w, 1, wing_d), "wing")
        else:
            cube = _cube((-1, 7, -1), (2, 2, 2), "body")
        bones.append(_bone(name, "body", pivot, [cube]))
    return Model(bones)


def _dorsal_pair(bw: int, bh: int, bd: int) -> list:
    height = max(2, bh // 2)
    cubes = []
    for sx in (-1, 1):
        ox = _mirror_x(sx, 0, 1)
        cubes.append(_cube((ox, 2 + bh, -bd // 6), (1, height, max(3, bd // 4)), "wing"))
    return cubes


def _tail_flukes(bh: int, bd: int, tucked: bool = False) -> list:
    fluke = 2 if tucked else 4
    bones = []
    z = 1 if tucked else bd // 2 + 3
    y = bh // 2
    depth = 2 if tucked else 4
    for name, sx in (("tailfin", -1), ("tail_fin", 1)):
        ox = _mirror_x(sx, 0, fluke)
        bones.append(_bone(name, "tail", [ox + fluke / 2, y + 2, z], [
            _cube((ox, y, z), (fluke, 1, depth), "wing")
        ]))
    return bones


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
            *(_dorsal_pair(bw, bh, bd) if "fins" in tokens else []),
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
        *_tail_flukes(bh, bd, tucked="fins" not in tokens),
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
    if recipe.get("anatomy"):
        bones = []
        for entry in recipe["anatomy"]["bones"]:
            cubes = []
            for c in entry.get("cubes", []):
                size = list(c["size"])
                if len(size) != 3 or any(type(n) is not int or n <= 0 for n in size):
                    raise ValueError(f"{recipe['id']}: authored cubes require positive integer texel dimensions")
                shape = c.get("shape", {})
                if "planform_y" in shape:
                    heights = shape["planform_y"]
                    if ("planform" not in shape or not isinstance(heights,list) or len(heights)!=2 or any(
                            not isinstance(row,list) or len(row)!=4 or any(type(v) not in (int,float) or not 0<=v<=1 for v in row)
                            for row in heights) or any(lo>=hi for lo,hi in zip(*heights))):
                        raise ValueError(f"{recipe['id']}: planform_y requires four normalized lower/upper heights")
                if "side_profile" in shape:
                    q = shape["side_profile"]
                    widths = shape.get("side_width", [1, 1])
                    if (set(shape) - {"side_profile", "side_width"} or
                            not isinstance(widths, list) or len(widths) != 2 or any(
                                type(v) not in (int, float) or not 0 < v <= 2 for v in widths)):
                        raise ValueError(f"{recipe['id']}: side_width requires positive bottom/top factors <= 2")
                    if not isinstance(q, list) or len(q) != 4 or any(
                            not isinstance(p, list) or len(p) != 2 or any(
                                type(v) not in (int, float) or not 0 <= v <= 1 for v in p) for p in q):
                        raise ValueError(f"{recipe['id']}: side_profile requires four normalized Y/Z corners")
                    polygon = [q[i] for i in (0, 1, 3, 2)]
                    turns = []
                    for i in range(4):
                        a, b, d = polygon[i], polygon[(i+1)%4], polygon[(i+2)%4]
                        turns.append((b[0]-a[0])*(d[1]-b[1])-(b[1]-a[1])*(d[0]-b[0]))
                    if min(turns) < 0 or max(turns) <= 0:
                        raise ValueError(f"{recipe['id']}: side_profile must be convex with positive area")
                cubes.append(Cube(list(c["origin"]), size, c["role"], name=c.get("name", ""),
                                  shape=shape, blend=c.get("blend", {})))
            bones.append(Bone(entry["name"], entry.get("parent"), list(entry["pivot"]), cubes,
                              entry.get("mirror", False), entry.get("bind_rotation")))
        names = [b.name for b in bones]
        if len(names) != len(set(names)) or any(b.parent and b.parent not in names for b in bones):
            raise ValueError(f"{recipe['id']}: duplicate bone or unknown parent")
        by_name = {b.name:b for b in bones}
        for bone in bones:
            seen = set()
            current = bone
            while current:
                if current.name in seen:
                    raise ValueError(f"{recipe['id']}: cyclic anatomy hierarchy")
                seen.add(current.name)
                current = by_name.get(current.parent)
        model = Model(bones, motion=deepcopy(recipe.get("motion", {})))
        from creaturegen.motion import apply_ground_contact
        apply_ground_contact(model)
        _pack(model, recipe.get("atlas_layout"))
        return model
    template = recipe.get("template") or "quadruped"
    builder = BUILDERS[template]
    model = builder(_scale_recipe(recipe, concept_scale))
    _pack(model)
    return model
