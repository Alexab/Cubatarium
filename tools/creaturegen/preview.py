"""Offline contact sheets from the published creature files.

Reads geometry, glTF or rigid parts and the textures the game would sample.
Does not launch the desktop app and does not read Bedrock reference meshes.
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "models" / "creatures"
SKINS = ROOT / "models" / "skins"
OUT = ROOT / "docs" / "research" / "creature-previews"
OUT_TEXTURED = ROOT / "docs" / "research" / "creature-previews-textured"

YAWS = [i * 45 for i in range(8)]


def _read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def _euler(deg) -> tuple[tuple[float, float, float], ...]:
    """X then Y then Z, matching BoneSkeletonEulerDegToMat (applied as Z*Y*X)."""
    x, y, z = [math.radians(v) for v in deg]
    cx, sx = math.cos(x), math.sin(x)
    cy, sy = math.cos(y), math.sin(y)
    cz, sz = math.cos(z), math.sin(z)
    # Column-major glm rotates applied to a column vector: Rz * Ry * Rx.
    rx = ((1, 0, 0), (0, cx, -sx), (0, sx, cx))
    ry = ((cy, 0, sy), (0, 1, 0), (-sy, 0, cy))
    rz = ((cz, -sz, 0), (sz, cz, 0), (0, 0, 1))

    def mul(a, b):
        return tuple(
            tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3))
            for i in range(3)
        )

    return mul(rz, mul(ry, rx))


def _apply(m, v):
    return tuple(sum(m[i][j] * v[j] for j in range(3)) for i in range(3))


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def _rot_around(points, pivot, degrees):
    if not degrees or all(abs(v) < 1e-3 for v in degrees):
        return points
    # Geo bind stores upstream degrees; the loader negates X and Y.
    mapped = (-degrees[0], -degrees[1], degrees[2])
    matrix = _euler(mapped)
    return [_add(pivot, _apply(matrix, _sub(point, pivot))) for point in points]


class Sampler:
    """Nearest-texel lookup. The game loads creature PNGs with GL_NEAREST."""

    def __init__(self, image: Image.Image):
        rgba = image.convert("RGBA")
        self.w, self.h = rgba.size
        self.data = rgba.tobytes()

    def at(self, u: float, v: float):
        x = int(u)
        y = int(v)
        if x < 0:
            x = 0
        elif x >= self.w:
            x = self.w - 1
        if y < 0:
            y = 0
        elif y >= self.h:
            y = self.h - 1
        index = (y * self.w + x) * 4
        if self.data[index + 3] < 16:
            return None
        return self.data[index], self.data[index + 1], self.data[index + 2]


def _face_color(image: Image.Image, u0, v0, u1, v1) -> tuple[int, int, int]:
    if image is None:
        return (160, 120, 80)
    x0, x1 = sorted((int(u0), int(u1)))
    y0, y1 = sorted((int(v0), int(v1)))
    x1 = max(x0 + 1, min(image.width, x1))
    y1 = max(y0 + 1, min(image.height, y1))
    x0 = max(0, min(image.width - 1, x0))
    y0 = max(0, min(image.height - 1, y0))
    crop = image.crop((x0, y0, x1, y1)).convert("RGBA")
    pixels = [pixel for pixel in crop.getdata() if pixel[3] > 16]
    if not pixels:
        return (0, 0, 0)
    count = len(pixels)
    return (
        sum(pixel[0] for pixel in pixels) // count,
        sum(pixel[1] for pixel in pixels) // count,
        sum(pixel[2] for pixel in pixels) // count,
    )


def _box(origin, size, pivot, bind, atlas, uv, textured: bool = False):
    ox, oy, oz = [component / 16.0 for component in origin]
    sx, sy, sz = [max(component, 1) / 16.0 for component in size]
    w, h, d = [max(int(component), 1) for component in size]
    u, v = uv
    corners = []
    for iz in (0, 1):
        for iy in (0, 1):
            for ix in (0, 1):
                corners.append((ox + sx * ix, oy + sy * iy, oz + sz * iz))
    corners = _rot_around(corners, tuple(component / 16.0 for component in pivot), bind or (0, 0, 0))
    # Corner order: x + 2y + 4z.
    # index = ix + 2*iy + 4*iz. Order is bottom-left, bottom-right, top-right, top-left.
    quads = {
        "+z": [4, 5, 7, 6],
        "-z": [1, 0, 2, 3],
        "+x": [5, 1, 3, 7],
        "-x": [0, 4, 6, 2],
        "+y": [2, 3, 7, 6],
        "-y": [4, 5, 1, 0],
    }
    regions = {
        "+z": (u + d + w + d, v + d, u + d + w + d + w, v + d + h),
        "+x": (u + d + w, v + d, u + d + w + d, v + d + h),
        "-z": (u + d, v + d, u + d + w, v + d + h),
        "-x": (u, v + d, u + d, v + d + h),
        "+y": (u + d, v, u + d + w, v + d),
        "-y": (u + d + w, v, u + d + w + w, v + d),
    }
    sampler = atlas if isinstance(atlas, Sampler) else None
    faces = []
    for name, indices in quads.items():
        u0, v0, u1, v1 = regions[name]
        points = [corners[index] for index in indices]
        if textured and sampler is not None:
            # Stay half a texel inside the painted island so the gutter is not a hole.
            faces.append((points, sampler, (
                (u0 + 0.5, v1 - 0.5),
                (u1 - 0.5, v1 - 0.5),
                (u1 - 0.5, v0 + 0.5),
                (u0 + 0.5, v0 + 0.5),
            )))
        else:
            faces.append((points, _face_color(atlas if not isinstance(atlas, Sampler) else None, u0, v0, u1, v1)))
    return faces


def _limb_swing(name: str) -> bool:
    """Bones the walk preview actually swings. Held items follow their arm."""
    if name in ("rightItem", "leftItem", "hat"):
        return False
    return name.startswith(("leg", "wing", "haunch", "front", "rear")) or name in (
        "rightArm", "leftArm", "rightLeg", "leftLeg")


def _bone_faces(species_dir: Path, texture_name: str = "diffuse", pose: str = "rest", textured: bool = False) -> list:
    creature = _read_json(species_dir / "creature.json")
    visual = creature.get("visual", {})
    geo_path = species_dir / visual.get("geometry_file", "geometry.geo.json")
    geo = _read_json(geo_path)
    block = None
    wanted = visual.get("geometry")
    for key, value in geo.items():
        if key.startswith("geometry.") and isinstance(value, dict):
            if wanted is None or key == wanted:
                block = value
                break
    if block is None:
        return []
    atlas_path = species_dir / "textures" / f"{texture_name}.png"
    if not atlas_path.is_file():
        atlas_path = species_dir / "textures" / "body.png"
    image = Image.open(atlas_path).convert("RGBA") if atlas_path.is_file() else None
    atlas = Sampler(image) if textured and image is not None else image
    bones = block.get("bones", [])
    by_name = {bone.get("name", ""): bone for bone in bones}
    signs = {}
    sign = 1
    for bone in bones:
        name = bone.get("name", "")
        if pose == "walk" and _limb_swing(name):
            signs[name] = sign
            sign *= -1
    faces = []
    for bone in bones:
        pivot = bone.get("pivot", [0, 0, 0])
        bind = bone.get("bind_pose_rotation")
        for cube in bone.get("cubes", []):
            painted = _box(cube["origin"], cube["size"], pivot, bind, atlas, cube.get("uv", [0, 0]), textured)
            if pose == "walk":
                ancestors = []
                current = bone
                seen = set()
                while current and current.get("name", "") not in seen:
                    seen.add(current.get("name", ""))
                    ancestors.append(current)
                    parent_name = current.get("parent")
                    current = by_name.get(parent_name) if parent_name else None
                swung = []
                for part in painted:
                    points = part[0]
                    for ancestor in ancestors:
                        ancestor_name = ancestor.get("name", "")
                        if ancestor_name not in signs:
                            continue
                        ancestor_pivot = tuple(component / 16.0 for component in ancestor.get("pivot", [0, 0, 0]))
                        points = _rot_around(points, ancestor_pivot, (28 * signs[ancestor_name], 0, 0))
                    swung.append((points, *part[1:]))
                painted = swung
            faces.extend(painted)
    if image is not None:
        image.close()
    return faces


def _accessor(gltf, blob, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    offset = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[acc["type"]]
    fmt = {5126: "f", 5123: "H", 5121: "B", 5125: "I", 5120: "b", 5122: "h"}[acc["componentType"]]
    size = struct.calcsize("<" + fmt)
    stride = view.get("byteStride") or size * comps
    rows = []
    for i in range(acc["count"]):
        rows.append(struct.unpack_from("<" + fmt * comps, blob, offset + i * stride))
    return rows


def _gltf_faces(species_dir: Path, pose: str = "rest", textured: bool = False) -> list:
    creature = _read_json(species_dir / "creature.json")
    scale = float(creature.get("visual", {}).get("gltf", {}).get("model_scale", 1) or 1)
    gltf = _read_json(species_dir / "model.gltf")
    blob = (species_dir / "model.bin").read_bytes()
    materials = gltf.get("materials", [])
    textures = {}
    for material in materials:
        stem = material.get("name") or "body"
        path = species_dir / "textures" / f"{stem}.png"
        if path.is_file():
            textures[stem] = Image.open(path).convert("RGBA")
    faces = []
    for mesh in gltf.get("meshes", []):
        for prim in mesh.get("primitives", []):
            attrs = prim["attributes"]
            positions = _accessor(gltf, blob, attrs["POSITION"])
            indices = _accessor(gltf, blob, prim["indices"])
            joints = _accessor(gltf, blob, attrs["JOINTS_0"]) if "JOINTS_0" in attrs else None
            index = prim.get("material", -1)
            stem = materials[index].get("name", "body") if 0 <= index < len(materials) else "body"
            image = textures.get(stem)
            sampler = Sampler(image) if textured and image is not None else None
            uvs = _accessor(gltf, blob, attrs["TEXCOORD_0"]) if textured and "TEXCOORD_0" in attrs else None
            color = None if sampler else _face_color(image, 0, 0, image.width if image else 1, image.height if image else 1)
            nodes = gltf.get("nodes", [])
            parents = [-1] * len(nodes)
            for node_index, node in enumerate(nodes):
                for child in node.get("children") or []:
                    if 0 <= int(child) < len(parents):
                        parents[int(child)] = node_index
            world = {}

            def rest_world(node_index: int) -> tuple[float, float, float]:
                if node_index in world:
                    return world[node_index]
                translation = nodes[node_index].get("translation", [0, 0, 0])
                parent = parents[node_index]
                if parent < 0:
                    found = (translation[0], translation[1], translation[2])
                else:
                    base = rest_world(parent)
                    found = (base[0] + translation[0], base[1] + translation[1], base[2] + translation[2])
                world[node_index] = found
                return found

            signs = {}
            sign = 1
            for node_index, node in enumerate(nodes):
                if _limb_swing(node.get("name", "")):
                    signs[node_index] = sign
                    sign *= -1
            posed = []
            for vertex, position in enumerate(positions):
                point = (position[0] * scale, position[1] * scale, position[2] * scale)
                if pose == "walk" and joints:
                    joint = int(joints[vertex][0])
                    seen = set()
                    while 0 <= joint < len(nodes) and joint not in seen:
                        seen.add(joint)
                        if joint in signs:
                            pivot = rest_world(joint)
                            point = _rot_around([point], (pivot[0] * scale, pivot[1] * scale, pivot[2] * scale), (24 * signs[joint], 0, 0))[0]
                        joint = parents[joint]
                posed.append(point)
            for i in range(0, len(indices), 3):
                tri = [posed[int(indices[i + k][0])] for k in range(3)]
                if sampler is not None and uvs is not None:
                    tri_uv = []
                    for k in range(3):
                        vertex = int(indices[i + k][0])
                        tri_uv.append((uvs[vertex][0] * (sampler.w - 1), uvs[vertex][1] * (sampler.h - 1)))
                    faces.append((tri, sampler, tri_uv))
                else:
                    faces.append((tri, color))
    for image in textures.values():
        image.close()
    return faces


def _rigid_faces(species_dir: Path, textured: bool = False) -> list:
    creature = _read_json(species_dir / "creature.json")
    visual = creature.get("visual", {})
    if visual.get("sprite", {}).get("billboard"):
        return []
    model_name = visual.get("rigid_model", "rigid_model.json")
    parts = _read_json(species_dir / model_name).get("parts", [])
    colors = {}
    samplers = {}
    faces = []
    for part in parts:
        stem = part.get("texture") or "body"
        if stem not in colors and stem not in samplers:
            path = species_dir / "textures" / f"{stem}.png"
            image = Image.open(path).convert("RGBA") if path.is_file() else None
            if textured and image is not None:
                samplers[stem] = Sampler(image)
            else:
                colors[stem] = _face_color(image, 0, 0, image.width if image else 1, image.height if image else 1)
            if image:
                image.close()
        ox, oy, oz = part["offset"]
        sx, sy, sz = part["size"]
        origin = ((ox - sx / 2) * 16, (oy - sy / 2) * 16, (oz - sz / 2) * 16)
        size = (max(sx, 0.05) * 16, max(sy, 0.05) * 16, max(sz, 0.05) * 16)
        painted = _box(origin, size, (ox * 16, oy * 16, oz * 16), None, None, (0, 0))
        if stem in samplers:
            sampler = samplers[stem]
            uvs = ((0, 0), (sampler.w - 1, 0), (sampler.w - 1, sampler.h - 1), (0, sampler.h - 1))
            faces.extend((points, sampler, uvs) for points, _color in painted)
        else:
            faces.extend((points, colors[stem]) for points, _color in painted)
    return faces


def _project(point, yaw_deg, pitch_deg):
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    x, y, z = point
    cy, sy = math.cos(yaw), math.sin(yaw)
    x, z = cy * x + sy * z, -sy * x + cy * z
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = cp * y - sp * z, sp * y + cp * z
    return x, -y, z


def _raster_triangle(pixels, zbuf, size, a, b, c, uv_a, uv_b, uv_c, sampler, silhouette):
    minx = max(0, int(min(a[0], b[0], c[0])))
    maxx = min(size - 1, int(max(a[0], b[0], c[0])))
    miny = max(0, int(min(a[1], b[1], c[1])))
    maxy = min(size - 1, int(max(a[1], b[1], c[1])))
    denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    if abs(denom) < 1e-6:
        return
    for y in range(miny, maxy + 1):
        for x in range(minx, maxx + 1):
            w0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / denom
            w1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / denom
            w2 = 1.0 - w0 - w1
            if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
                continue
            depth = w0 * a[2] + w1 * b[2] + w2 * c[2]
            slot = y * size + x
            if depth >= zbuf[slot]:
                continue
            if silhouette or sampler is None:
                zbuf[slot] = depth
                pixels[x, y] = (24, 22, 20)
                continue
            u = w0 * uv_a[0] + w1 * uv_b[0] + w2 * uv_c[0]
            v = w0 * uv_a[1] + w1 * uv_b[1] + w2 * uv_c[1]
            color = sampler.at(u, v)
            if color is not None:
                zbuf[slot] = depth
                pixels[x, y] = color


def _panel(faces, yaw, pitch, size, silhouette=False, background=(236, 232, 220)) -> Image.Image:
    image = Image.new("RGB", (size, size), background)
    if not faces:
        return image
    textured = len(faces[0]) == 3
    projected = []
    for face in faces:
        points = face[0]
        screen = [_project(point, yaw, pitch) for point in points]
        projected.append((screen, face[1:]))
    xs = [p[0] for screen, _rest in projected for p in screen]
    ys = [p[1] for screen, _rest in projected for p in screen]
    minx, maxx = min(xs), max(xs)
    miny, maxy = min(ys), max(ys)
    span = max(maxx - minx, maxy - miny, 1e-4)
    scale = (size - 16) / span
    cx = (minx + maxx) / 2
    cy = (miny + maxy) / 2

    def to_px(point):
        return ((point[0] - cx) * scale + size / 2, (point[1] - cy) * scale + size / 2, point[2])

    if textured:
        pixels = image.load()
        zbuf = [1e9] * (size * size)
        for screen, rest in projected:
            sampler, uvs = rest
            mapped = [to_px(point) for point in screen]
            triangles = [(0, 1, 2)]
            if len(mapped) >= 4:
                triangles.append((0, 2, 3))
            for i0, i1, i2 in triangles:
                _raster_triangle(
                    pixels, zbuf, size,
                    mapped[i0], mapped[i1], mapped[i2],
                    uvs[i0], uvs[i1], uvs[i2],
                    None if silhouette else sampler,
                    silhouette,
                )
        return image

    draw = ImageDraw.Draw(image)
    order = sorted(projected, key=lambda item: sum(p[2] for p in item[0]) / len(item[0]))
    for screen, rest in order:
        color = rest[0]
        poly = [to_px(point)[:2] for point in screen]
        fill = (24, 22, 20) if silhouette else color
        outline = (24, 22, 20) if silhouette else tuple(max(0, channel - 35) for channel in color)
        draw.polygon(poly, fill=fill, outline=outline)
    return image


def _sprite_panel(path: Path, size: int, dark: bool) -> Image.Image:
    background = (28, 32, 40) if dark else (236, 232, 220)
    image = Image.new("RGB", (size, size), background)
    if not path.is_file():
        return image
    sprite = Image.open(path).convert("RGBA")
    sprite = sprite.resize((size - 24, size - 24), Image.Resampling.NEAREST)
    image.paste(sprite, (12, 12), sprite)
    sprite.close()
    return image


def _font(size: int):
    for name in ("segoeui.ttf", "arial.ttf", "calibri.ttf"):
        path = Path("C:/Windows/Fonts") / name
        if path.is_file():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def _sheet(species_id: str, faces_rest, faces_walk, sprite: Path | None, subtitle: str) -> Image.Image:
    panel = 132
    labels = [f"{yaw}°" for yaw in YAWS] + ["сверху", "снизу", "силуэт", "синт. поза"]
    font = _font(13)
    width = 16 + len(labels) * (panel + 8)
    height = panel + 46
    sheet = Image.new("RGB", (width, height), (248, 246, 238))
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 4), f"{species_id}  {subtitle}", fill=(40, 36, 32), font=font)
    views = [(yaw, -18, False, faces_rest) for yaw in YAWS]
    views.append((0, -90, False, faces_rest))
    views.append((0, 90, False, faces_rest))
    views.append((90, 0, True, faces_rest))
    views.append((45, -18, False, faces_walk or faces_rest))
    for index, (yaw, pitch, silhouette, faces) in enumerate(views):
        x = 12 + index * (panel + 8)
        if sprite and not faces:
            tile = _sprite_panel(sprite, panel, dark=(index % 2 == 0))
        else:
            tile = _panel(faces, yaw, pitch, panel, silhouette)
        sheet.paste(tile, (x, 28))
        draw.text((x, height - 18), labels[index], fill=(70, 64, 58), font=font)
    return sheet


def render_species(species_id: str, out_dir: Path = OUT, textured: bool = False) -> Path:
    species_dir = MODELS / species_id
    creature = _read_json(species_dir / "creature.json")
    visual = creature.get("visual", {})
    backend = visual.get("backend")
    billboard = bool(visual.get("sprite", {}).get("billboard"))
    sprite = species_dir / "textures" / f"{visual.get('default_texture', 'body')}.png" if billboard else None
    if billboard:
        rest, walk = [], []
        subtitle = "billboard"
    elif backend == "bone_skeleton":
        rest = _bone_faces(species_dir, "diffuse", "rest", textured)
        walk = _bone_faces(species_dir, "diffuse", "walk", textured)
        subtitle = "bone"
    elif backend == "gltf_skeleton":
        rest = _gltf_faces(species_dir, "rest", textured)
        walk = _gltf_faces(species_dir, "walk", textured)
        subtitle = "gltf"
    else:
        rest = _rigid_faces(species_dir, textured)
        walk = rest
        subtitle = "rigid"
    if textured and not billboard:
        subtitle += ", текстура"
    sheet = _sheet(species_id, rest, walk, sprite, subtitle)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{species_id}.png"
    sheet.save(path)
    return path


def render_skin(skin_id: str, out_dir: Path = OUT, textured: bool = False) -> Path | None:
    skin_dir = SKINS / skin_id
    definition = _read_json(skin_dir / "skin.json")
    species_id = definition.get("creature_id")
    if not species_id:
        return None
    species_dir = MODELS / species_id
    # Bone and glTF sample skin/<id>/diffuse before the species atlas.
    temp = out_dir / "_skin_src" / skin_id
    if temp.exists():
        for child in temp.rglob("*"):
            if child.is_file():
                child.unlink()
    # Render by temporarily pointing the loader at a copied species whose diffuse is the skin.
    # Copy is avoided: paint using the skin texture through a small adapter directory of links.
    # Windows may not allow symlinks, so read the skin image by swapping the path argument.
    creature = _read_json(species_dir / "creature.json")
    backend = creature.get("visual", {}).get("backend")
    skin_tex = skin_dir / "textures" / "diffuse.png"
    if backend != "bone_skeleton" or not skin_tex.is_file():
        return None
    faces = _bone_faces_with_atlas(species_dir, skin_tex, "rest", textured)
    walk = _bone_faces_with_atlas(species_dir, skin_tex, "walk", textured)
    sheet = _sheet(f"{skin_id} на {species_id}", faces, walk, None, "skin")
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"skin_{skin_id}.png"
    sheet.save(path)
    return path


def _bone_faces_with_atlas(species_dir: Path, atlas_path: Path, pose: str, textured: bool = False) -> list:
    creature = _read_json(species_dir / "creature.json")
    visual = creature.get("visual", {})
    geo = _read_json(species_dir / visual.get("geometry_file", "geometry.geo.json"))
    block = None
    wanted = visual.get("geometry")
    for key, value in geo.items():
        if key.startswith("geometry.") and isinstance(value, dict) and (wanted is None or key == wanted):
            block = value
            break
    if block is None:
        return []
    image = Image.open(atlas_path).convert("RGBA")
    atlas = Sampler(image) if textured else image
    painted = []
    sign = 1
    for bone in block.get("bones", []):
        pivot = bone.get("pivot", [0, 0, 0])
        bind = bone.get("bind_pose_rotation")
        if pose == "walk" and bone.get("name", "").startswith(("leg", "wing", "haunch", "front", "rear", "right", "left")):
            extra = (28 * sign, 0, 0)
            sign *= -1
            bind = [*(bind or (0, 0, 0))]
            bind = [bind[0] + extra[0], bind[1] + extra[1], bind[2] + extra[2]]
        for cube in bone.get("cubes", []):
            painted.extend(_box(cube["origin"], cube["size"], pivot, bind, atlas, cube.get("uv", [0, 0]), textured))
    image.close()
    return painted


def write_index(paths: list[Path], out_dir: Path, textured: bool = False) -> Path:
    cards = []
    for path in paths:
        cards.append(
            f'<figure><img src="{path.name}" alt="{path.stem}"><figcaption>{path.stem}</figcaption></figure>'
        )
    html = """<!DOCTYPE html>
<html lang="ru">
<meta charset="utf-8">
<title>Существа Cubatarium</title>
<style>
body { font-family: sans-serif; background: #1c1a17; color: #f4efe6; margin: 24px; }
h1 { font-size: 22px; }
p { max-width: 70em; color: #d9d0c3; }
figure { margin: 0 0 18px; background: #2a2622; padding: 8px; }
img { width: 100%; height: auto; }
figcaption { margin-top: 6px; }
</style>
<h1>Оригинальные существа</h1>
<p>Листы собраны из опубликованных моделей. """ + (
        "Каждая грань несёт тексели своего UV-участка, ближайшая выборка, как GL_NEAREST в игре."
        if textured else
        "Грань залита средним цветом своего участка атласа, без рисунка пятен и полос."
    ) + """ Слева направо: восемь рысканий, вид сверху, снизу, силуэт и синтетическая поза. Последняя не читает клипы и не подтверждает качество анимации.</p>
""" + "\n".join(cards) + "\n</html>\n"
    index = out_dir / "index.html"
    index.write_text(html, encoding="utf-8")
    return index


def render_all(out_dir: Path = OUT, textured: bool = False) -> list[Path]:
    paths = []
    for species_dir in sorted(MODELS.glob("*/creature.json")):
        paths.append(render_species(species_dir.parent.name, out_dir, textured))
        print(paths[-1].name)
    for skin_dir in sorted(SKINS.glob("*/skin.json")):
        rendered = render_skin(skin_dir.parent.name, out_dir, textured)
        if rendered:
            paths.append(rendered)
            print(rendered.name)
    write_index(paths, out_dir, textured)
    return paths
