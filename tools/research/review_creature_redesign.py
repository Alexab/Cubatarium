"""Comparative, read-only creature review: published baseline vs candidate.

Offline orthographic diagnostic, not an assertion of runtime/render parity.
glTF uses actual skin weights, joint indirection, bind matrices and sampled
clips. Bone sheets use rest geometry; they do not simulate C++ pose profiles.
Reference assets are read only here, never by the production generator.
"""
from __future__ import annotations

import argparse
import copy
import io
import json
import math
from pathlib import Path
import sys
import subprocess
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from creaturegen import preview


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def accessor(g, blobs, index):
    a = g["accessors"][index]
    v = g["bufferViews"][a["bufferView"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    dtype = np.dtype({5126: "<f4", 5123: "<u2", 5121: "u1", 5125: "<u4", 5122: "<i2", 5120: "i1"}[a["componentType"]])
    offset = a.get("byteOffset", 0) + v.get("byteOffset", 0)
    array = np.ndarray((a["count"], n), dtype=dtype, buffer=blobs[v.get("buffer", 0)], offset=offset,
                       strides=(v.get("byteStride", n * dtype.itemsize), dtype.itemsize)).copy()
    if a.get("normalized") and dtype.kind in "iu":
        array = np.maximum(array.astype(float) / np.iinfo(dtype).max, -1)
    return array


def authored_vertex_owners(model, g, blobs):
    """Cube provenance from non-overlapping atlas islands, not unused skin slots.

    A zero-weight slot has no semantic owner in glTF. Tests of physical seams
    must identify source geometry independently of that encoding detail.
    """
    attrs=g['meshes'][0]['primitives'][0]['attributes']
    uv=accessor(g,blobs,attrs['TEXCOORD_0'])*np.array(model.texture_size)
    owners=np.full(len(uv),-1,dtype=int)
    for index,bone in enumerate(model.bones):
        for cube in bone.cubes:
            u,v=cube.uv;w,h,d=cube.size
            mask=(uv[:,0]>u)&(uv[:,0]<u+2*(w+d))&(uv[:,1]>v)&(uv[:,1]<v+h+d)
            if mask.sum()!=24 or np.any(owners[mask]!=-1):
                raise ValueError('Ambiguous authored cube atlas island: '+cube.name)
            owners[mask]=index
    if np.any(owners<0):raise ValueError('Unowned exported vertices')
    return owners


def quat(q):
    x, y, z, w = np.asarray(q) / np.linalg.norm(q)
    return np.array([[1-2*y*y-2*z*z, 2*x*y-2*z*w, 2*x*z+2*y*w, 0],
                     [2*x*y+2*z*w, 1-2*x*x-2*z*z, 2*y*z-2*x*w, 0],
                     [2*x*z-2*y*w, 2*y*z+2*x*w, 1-2*x*x-2*y*y, 0], [0, 0, 0, 1]])


def translation(v):
    m = np.eye(4)
    m[:3, 3] = v
    return m


def world_matrices(nodes):
    parents = {}
    for i, node in enumerate(nodes):
        for child in node.get("children", []):
            if child in parents:
                raise ValueError("multiple node parents")
            parents[child] = i
    cache = {}

    def get(i):
        if i not in cache:
            n = nodes[i]
            if "matrix" in n:
                local = np.array(n["matrix"]).reshape(4, 4).T
            else:
                local = translation(n.get("translation", [0, 0, 0])) @ quat(n.get("rotation", [0, 0, 0, 1])) @ np.diag([*n.get("scale", [1, 1, 1]), 1])
            cache[i] = (get(parents[i]) if i in parents else np.eye(4)) @ local
        return cache[i]
    return [get(i) for i in range(len(nodes))]


def sample_nodes(g, blobs, clip=None, phase=0):
    nodes = copy.deepcopy(g.get("nodes", []))
    animation = next((a for a in g.get("animations", []) if a.get("name") == clip), None)
    if animation is None:
        return nodes
    duration = max(float(accessor(g, blobs, s["input"])[-1, 0]) for s in animation["samplers"])
    for channel in animation["channels"]:
        s = animation["samplers"][channel["sampler"]]
        times = accessor(g, blobs, s["input"])[:, 0]
        values = accessor(g, blobs, s["output"])
        if s.get("interpolation", "LINEAR") not in ("LINEAR", "STEP"):
            raise ValueError("unsupported interpolation in review")
        t = float(times[0]) + phase * (duration - float(times[0]))
        j = max(0, min(len(times)-2, int(np.searchsorted(times, t, side="right"))-1))
        k = min(j+1, len(times)-1)
        alpha = 0 if s.get("interpolation") == "STEP" or times[k] == times[j] else float(np.clip((t-times[j])/(times[k]-times[j]), 0, 1))
        path = channel["target"]["path"]
        a, b = values[j].astype(float), values[k].astype(float)
        if path == "rotation":
            dot = float(np.dot(a, b))
            if dot < 0:
                b = -b
                dot = -dot
            if dot < 0.9995:
                theta = math.acos(np.clip(dot, -1, 1))
                value = (math.sin((1-alpha)*theta)*a + math.sin(alpha*theta)*b)/math.sin(theta)
            else:
                value = a*(1-alpha)+b*alpha
            value /= np.linalg.norm(value)
        else:
            value = a*(1-alpha)+b*alpha
        nodes[channel["target"]["node"]][path] = value.tolist()
    return nodes


def gltf_faces(folder, clip=None, phase=0):
    definition = read(folder/"creature.json")
    g = read(folder/"model.gltf")
    blobs = [(folder/b["uri"]).read_bytes() for b in g["buffers"]]
    worlds = world_matrices(sample_nodes(g, blobs, clip, phase))
    scale = definition["visual"]["gltf"].get("model_scale", 1)
    yaw = definition["visual"]["gltf"].get("model_yaw_offset_deg", 0)
    # Common diagnostic forward +Z. Generated assets face -Z, as do bone geo.
    # Actual glTF runtime yaw_offset is retained; no per-species visual guessing.
    faces = []
    for ni, node in enumerate(g["nodes"]):
        if "mesh" not in node:
            continue
        skin = g.get("skins", [])[node["skin"]] if "skin" in node else None
        palette = None
        if skin:
            ibm = accessor(g, blobs, skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
            palette = np.array([worlds[j] @ inv for j, inv in zip(skin["joints"], ibm)])
        for p in g["meshes"][node["mesh"]]["primitives"]:
            a = p["attributes"]
            positions = accessor(g, blobs, a["POSITION"])
            v = np.column_stack([positions, np.ones(len(positions))])
            if palette is not None and "JOINTS_0" in a:
                j = accessor(g, blobs, a["JOINTS_0"]).astype(int)
                w = accessor(g, blobs, a["WEIGHTS_0"])
                mats = (palette[j] * w[:, :, None, None]).sum(axis=1)
                pos = np.einsum("nij,nj->ni", mats, v)[:, :3]
            else:
                pos = (v @ worlds[ni].T)[:, :3]
            pos *= scale
            if yaw:
                pos = pos @ np.array(preview._euler([0, yaw, 0])).T
            material = g.get("materials", [])[p.get("material", 0)]
            path = folder/"textures"/(material.get("name", "body")+".png")
            with Image.open(path) as im:
                image = np.array(im.convert("RGBA"))
            uv = accessor(g, blobs, a["TEXCOORD_0"])
            idx = accessor(g, blobs, p["indices"]).reshape(-1, 3)
            for tri in idx:
                faces.append((pos[tri], image, uv[tri]))
    return faces


def bone_faces(folder, atlas_override=None):
    d = read(folder/"creature.json")
    v = d["visual"]
    g = read(folder/v.get("geometry_file", "geometry.geo.json"))
    block = g.get(v.get("geometry"))
    if block is None:
        block = next(e for e in g.get("minecraft:geometry", []) if e.get("description", {}).get("identifier") == v.get("geometry"))
    atlas = atlas_override or folder/"textures"/(v.get("texture", "diffuse")+".png")
    if not atlas.is_file():
        atlas = folder/"textures"/(v.get("default_texture", "body")+".png")
    with Image.open(atlas) as im:
        image = np.array(im.convert("RGBA"))
    texture_size = (block.get("texturewidth", block.get("description", {}).get("texture_width", 64)),
                    block.get("textureheight", block.get("description", {}).get("texture_height", 32)))
    bones = block["bones"]
    byname = {b["name"]: b for b in bones}
    cache = {}

    def matrix(b):
        if b["name"] in cache:
            return cache[b["name"]]
        pivot = np.array(b.get("pivot", [0, 0, 0]))/16
        chain = b
        absolute = False
        while chain:
            if any(chain.get("bind_pose_rotation", [])):
                absolute = True
            chain = byname.get(chain.get("parent"))
        bind = b.get("bind_pose_rotation", [0, 0, 0])
        rotation = np.array([-bind[0], -bind[1], bind[2]]) + b.get("rotation", [0, 0, 0])
        rot = np.eye(4)
        rot[:3, :3] = preview._euler(rotation)
        parent = byname.get(b.get("parent"))
        if parent and not absolute:
            m = matrix(parent) @ translation(pivot - np.array(parent.get("pivot", [0, 0, 0]))/16) @ rot
        else:
            m = translation(pivot) @ rot
        cache[b["name"]] = m
        return m
    faces = []
    for b in bones:
        pivot = np.array(b.get("pivot", [0, 0, 0]))/16
        for c in b.get("cubes", []):
            # Preview box UV geometry; retain zero-axis plane face selection.
            fake = preview.Sampler(Image.fromarray(image))
            fake.w, fake.h = texture_size
            for face_index, (points, _, pixel_uv) in enumerate(preview._box(c["origin"], c["size"], [0, 0, 0], None, fake, c.get("uv", [0, 0]), True)):
                zero_axis = next((axis for axis, size in enumerate(c["size"]) if size == 0), None)
                if zero_axis is not None and face_index not in ((2, 3), (4, 5), (0, 1))[zero_axis]:
                    continue
                p = np.array(points)
                if c.get("rotation"):
                    cp = np.array(c.get("pivot", b.get("pivot", [0, 0, 0])))/16
                    p = (p-cp) @ np.array(preview._euler(c["rotation"])).T + cp
                p = np.column_stack([p-pivot, np.ones(len(p))]) @ matrix(b).T
                p = p[:, :3]
                p[:, 2] *= -1
                uv = np.array(pixel_uv)/np.array(texture_size)
                for tri in ([0, 1, 2], [0, 2, 3]):
                    faces.append((p[tri], image, uv[tri]))
    return faces


def faces_for(folder):
    d = read(folder/"creature.json")
    if d["visual"].get("sprite", {}).get("billboard"):
        with Image.open(folder/"textures/body.png") as im:
            image = np.array(im.convert("RGBA"))
        p = np.array([[-0.5, 0, 0], [0.5, 0, 0], [0.5, 1, 0], [-0.5, 1, 0]])
        uv = np.array([[0, 1], [1, 1], [1, 0], [0, 0]])
        return [(p[t], image, uv[t]) for t in ([0, 1, 2], [0, 2, 3])]
    if d["visual"]["backend"] == "gltf_skeleton":
        return gltf_faces(folder)
    if d["visual"]["backend"] == "bone_skeleton":
        return bone_faces(folder)
    result = []
    for points, sampler, pixel_uv in preview._rigid_faces(folder, True):
        image = np.frombuffer(sampler.data, np.uint8).reshape(sampler.h, sampler.w, 4)
        for t in ([0, 1, 2], [0, 2, 3]):
            result.append((np.array(points)[t], image, (np.array(pixel_uv)/[sampler.w, sampler.h])[t]))
    return result


def extent(faces):
    points = np.concatenate([p for p, _, _ in faces])
    return points.min(axis=0), points.max(axis=0)


def panel(faces, yaw, pitch, size=176, silhouette=False, span=None):
    rotation = np.array(preview._euler([pitch, 0, 0])) @ np.array(preview._euler([0, yaw, 0]))
    lo, hi = extent(faces)
    center = (lo+hi)/2
    # Equal world scale within each baseline/candidate pair, including all views.
    world_span = span or float(np.linalg.norm(hi-lo))
    output = np.full((size, size, 3), [230, 232, 235], dtype=np.uint8)
    depth = np.full((size, size), np.inf)
    for vertices, image, uv in faces:
        p = (vertices-center) @ rotation.T
        p[:, :2] *= (size-16)/world_span
        p[:, 0] += size/2
        p[:, 1] = size/2-p[:, 1]
        x0, y0 = np.maximum(np.floor(p[:, :2].min(axis=0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(p[:, :2].max(axis=0)).astype(int), size-1)
        if x0>x1 or y0>y1:
            continue
        den = (p[1,1]-p[2,1])*(p[0,0]-p[2,0])+(p[2,0]-p[1,0])*(p[0,1]-p[2,1])
        if abs(den)<1e-9:
            continue
        yy, xx = np.mgrid[y0:y1+1, x0:x1+1]
        xx, yy = xx+0.5, yy+0.5
        a = ((p[1,1]-p[2,1])*(xx-p[2,0])+(p[2,0]-p[1,0])*(yy-p[2,1]))/den
        b = ((p[2,1]-p[0,1])*(xx-p[2,0])+(p[0,0]-p[2,0])*(yy-p[2,1]))/den
        c = 1-a-b
        z = a*p[0,2]+b*p[1,2]+c*p[2,2]
        u = np.minimum((np.clip(a*uv[0,0]+b*uv[1,0]+c*uv[2,0], 0, 1)*image.shape[1]).astype(int), image.shape[1]-1)
        v = np.minimum((np.clip(a*uv[0,1]+b*uv[1,1]+c*uv[2,1], 0, 1)*image.shape[0]).astype(int), image.shape[0]-1)
        colors = image[v,u]
        mask = (a>=0)&(b>=0)&(c>=0)&(colors[:,:,3]>=13)&(z<depth[y0:y1+1,x0:x1+1])
        depth[y0:y1+1,x0:x1+1][mask] = z[mask]
        if silhouette:
            output[y0:y1+1,x0:x1+1][mask] = [26,28,31]
        else:
            output[y0:y1+1,x0:x1+1][mask] = colors[:,:,:3][mask]
    return Image.fromarray(output)


def font(size=14):
    return ImageFont.truetype("C:/Windows/Fonts/arial.ttf", size)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline-root", type=Path, default=ROOT/"assetgen_output/creatures/review_2026_11_10/baseline")
    ap.add_argument("--out", type=Path, default=ROOT/"docs/research/creature-review-2026-11-10")
    ap.add_argument("--extract-baseline", action="store_true", help="extract b35f00a75 assets into an empty baseline folder")
    args = ap.parse_args()
    if args.extract_baseline:
        destination = args.baseline_root.resolve()
        if destination.exists() and any(destination.iterdir()):
            raise ValueError("baseline extraction requires an empty folder; existing data is preserved")
        archive = subprocess.check_output(["git", "archive", "--format=zip", "b35f00a75", "models/creatures", "models/skins"], cwd=ROOT)
        with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
            for name in zipped.namelist():
                target = (destination/name).resolve()
                if not target.is_relative_to(destination):
                    raise ValueError("archive path outside baseline folder")
                if not name.endswith("/"):
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(zipped.read(name))
    args.out.mkdir(parents=True, exist_ok=True)
    ids = sorted(p.parent.name for p in (ROOT/"models/creatures").glob("*/creature.json"))
    cards = []
    errors = []
    for sid in ids:
        old = faces_for(args.baseline_root/"models/creatures"/sid)
        new = faces_for(ROOT/"models/creatures"/sid)
        card = Image.new("RGB", (1080, 220), (248,249,250))
        draw = ImageDraw.Draw(card)
        draw.text((8, 2), sid+"     BASELINE b35f00a75 (left) | HEAD (right), equal world scale", font=font(), fill=(35,40,45))
        span = max(np.linalg.norm(extent(old)[1]-extent(old)[0]), np.linalg.norm(extent(new)[1]-extent(new)[0]))
        for version, faces in enumerate((old, new)):
            for k, yaw in enumerate((35, 90, 180)):
                card.paste(panel(faces, yaw, -15, span=span), (version*540+k*180, 24))
        cards.append(card)
        card.save(args.out/f"pair_{sid}.png")
        print(sid, flush=True)
    for start in range(0, len(cards), 7):
        sheet = Image.new("RGB", (1080, 40+220*len(cards[start:start+7])), (248,249,250))
        ImageDraw.Draw(sheet).text((8,8), "Offline rest geometry + sampled texels. Not runtime animation acceptance.",font=font(16),fill=(35,40,45))
        for index, card in enumerate(cards[start:start+7]):
            sheet.paste(card, (0,40+index*220))
        sheet.save(args.out/f"comparison_{start//7+1:02}.png")
    # Real glTF clip samples, no name-based synthetic swings.
    for sid in ("badger", "hermitcrab", "stingray", "octopus", "wasp", "water_dragon"):
        sheet = Image.new("RGB", (1080,440),(248,249,250))
        for row, base in enumerate((args.baseline_root, ROOT)):
            folder = base/"models/creatures"/sid
            d = read(folder/"creature.json")
            mapping = d.get("visual",{}).get("animation",{}).get("state_map",{})
            clip = mapping.get("Swim") or mapping.get("Fly") or mapping.get("Walk") or "walk"
            sampled = [gltf_faces(folder,clip,t) for t in (0,0.2,0.4,0.6,0.8,1)]
            span = max(np.linalg.norm(extent(f)[1]-extent(f)[0]) for f in sampled)
            ImageDraw.Draw(sheet).text((8,row*220),f"{sid}: {'baseline' if row==0 else 'HEAD'} / {clip} / t=0,.2,.4,.6,.8,1",font=font(),fill=(35,40,45))
            for col,f in enumerate(sampled):
                sheet.paste(panel(f,35,-15,span=span),(col*180,24+row*220))
        sheet.save(args.out/f"motion_{sid}.png")
    skin_cards = []
    for path in sorted((ROOT/"models/skins").glob("*/skin.json")):
        skin_id = path.parent.name
        species_id = read(path)["creature_id"]
        pair = []
        for base in (args.baseline_root, ROOT):
            pair.append(bone_faces(base/"models/creatures"/species_id,
                                   base/"models/skins"/skin_id/"textures/diffuse.png"))
        span = max(np.linalg.norm(extent(f)[1]-extent(f)[0]) for f in pair)
        card = Image.new("RGB",(1080,220),(248,249,250))
        ImageDraw.Draw(card).text((8,2),f"{skin_id} on {species_id}: baseline | HEAD, equal world scale",font=font(),fill=(35,40,45))
        for version,faces in enumerate(pair):
            for col,yaw in enumerate((35,90,180)):
                card.paste(panel(faces,yaw,-15,span=span),(version*540+col*180,24))
        skin_cards.append(card)
    skin_sheet = Image.new("RGB",(1080,220*len(skin_cards)),(248,249,250))
    for i,card in enumerate(skin_cards):
        skin_sheet.paste(card,(0,220*i))
    skin_sheet.save(args.out/"skins.png")
    html = '<!doctype html><meta charset="utf-8"><title>Creature review</title><style>body{background:#ddd;font-family:sans-serif}img{max-width:100%}</style><h1>Baseline / HEAD</h1><p>Offline diagnostic. Rest geometry, same pair scale. Motion sheets sample actual glTF clips. Bone C++ procedural poses, lighting, UI and runtime culling require Release checks.</p>'
    for p in sorted(args.out.glob("comparison_*.png"))+sorted(args.out.glob("motion_*.png"))+[args.out/"skins.png"]:
        html += f'<h2>{p.stem}</h2><img src="{p.name}">'
    (args.out/"index.html").write_text(html,encoding="utf-8")


if __name__ == "__main__":
    main()
