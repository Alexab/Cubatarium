#!/usr/bin/env python
"""Audit (and optionally fix) creature collision boxes against their visible model.

The visible glTF model is the reference: `bounds.rest` in creature.json must
cover the skinned mesh in its bind pose and in the idle / walk / fly clips.

  python tools/creature_bounds_audit.py                 # report all glTF creatures
  python tools/creature_bounds_audit.py --check         # exit 1 on mismatch (CI)
  python tools/creature_bounds_audit.py --apply         # rewrite mismatching bounds
  python tools/creature_bounds_audit.py --only owl,ogre # restrict

Mismatch = height or footprint differs from the model by more than --tolerance.
Rewritten files keep the min/max ratios of the old bounds and scale eye_height by
the same factor as the height.
"""
import argparse
import glob
import json
import os
import struct
import sys

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CREATURES = os.path.join(REPO, "models", "creatures")
POSE_CLIPS = ("idle", "walk", "fly")
SAMPLES_PER_CLIP = 12
# Collision boxes are axis-aligned; never thinner than this (kitten is 0.09 wide).
MIN_BOX = np.array([0.3, 0.15, 0.3])
COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2),
             5125: ("I", 4), 5126: ("f", 4)}
TYPE_COUNT = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_accessor(g, buf, index):
    acc = g["accessors"][index]
    view = g["bufferViews"][acc["bufferView"]]
    fmt, size = COMPONENT[acc["componentType"]]
    count = TYPE_COUNT[acc["type"]]
    stride = view.get("byteStride", size * count)
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    out = np.empty((acc["count"], count), dtype=np.float64)
    for i in range(acc["count"]):
        out[i] = struct.unpack_from("<" + fmt * count, buf, base + i * stride)
    if acc.get("normalized"):
        out /= {5121: 255.0, 5123: 65535.0, 5120: 127.0, 5122: 32767.0}[acc["componentType"]]
    return out


def quat_matrix(q):
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 0],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 0],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), 0],
        [0, 0, 0, 1]])


def node_local(node, override):
    if "matrix" in node and not override:
        return np.array(node["matrix"], dtype=np.float64).reshape(4, 4).T
    t = override.get("t", node.get("translation", [0, 0, 0]))
    r = override.get("r", node.get("rotation", [0, 0, 0, 1]))
    s = override.get("s", node.get("scale", [1, 1, 1]))
    m = np.eye(4)
    m[:3, 3] = t
    return m @ quat_matrix(np.array(r, dtype=np.float64)) @ np.diag([*s, 1.0])


def sample_channel(times, values, t, path):
    if len(times) == 1 or t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    i = int(np.searchsorted(times, t) - 1)
    f = (t - times[i]) / (times[i + 1] - times[i])
    a, b = values[i], values[i + 1]
    if path == "rotation":
        if np.dot(a, b) < 0:
            b = -b
        q = a * (1 - f) + b * f
        return q / np.linalg.norm(q)
    return a * (1 - f) + b * f


def load_model(folder):
    g = json.load(open(os.path.join(folder, "model.gltf"), encoding="utf-8"))
    buf = open(os.path.join(folder, "model.bin"), "rb").read()
    return g, buf


def mesh_vertices(g, buf):
    pos, joints, weights = [], [], []
    for mesh in g["meshes"]:
        for prim in mesh["primitives"]:
            attrs = prim["attributes"]
            p = read_accessor(g, buf, attrs["POSITION"])
            pos.append(p)
            if "JOINTS_0" in attrs and "WEIGHTS_0" in attrs:
                joints.append(read_accessor(g, buf, attrs["JOINTS_0"]).astype(int))
                weights.append(read_accessor(g, buf, attrs["WEIGHTS_0"]))
            else:
                joints.append(np.zeros((len(p), 4), dtype=int))
                weights.append(np.tile([1.0, 0, 0, 0], (len(p), 1)))
    return np.vstack(pos), np.vstack(joints), np.vstack(weights)


def world_matrices(g, overrides):
    nodes = g["nodes"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    cache = {}

    def world(i):
        if i in cache:
            return cache[i]
        m = node_local(nodes[i], overrides.get(i, {}))
        if i in parent:
            m = world(parent[i]) @ m
        cache[i] = m
        return m

    return [world(i) for i in range(len(nodes))]


def posed_extents(g, buf):
    """Yield (min, max) of the skinned mesh for bind pose and each sampled pose."""
    pos, jnt, wgt = mesh_vertices(g, buf)
    hom = np.hstack([pos, np.ones((len(pos), 1))])
    if not g.get("skins"):
        yield pos.min(axis=0), pos.max(axis=0)
        return
    skin = g["skins"][0]
    joints = skin["joints"]
    ibm = read_accessor(g, buf, skin["inverseBindMatrices"]).reshape(-1, 4, 4)
    ibm = np.transpose(ibm, (0, 2, 1))  # column-major -> row-major

    def skinned(overrides):
        world = world_matrices(g, overrides)
        palette = np.array([world[j] @ ibm[k] for k, j in enumerate(joints)])
        out = np.zeros((len(pos), 3))
        for slot in range(4):
            w = wgt[:, slot]
            if not w.any():
                continue
            m = palette[jnt[:, slot]]
            out += w[:, None] * np.einsum("nij,nj->ni", m, hom)[:, :3]
        return out

    bind = skinned({})
    yield bind.min(axis=0), bind.max(axis=0)
    for anim in g.get("animations", []):
        if anim.get("name") not in POSE_CLIPS:
            continue
        chans = []
        t_end = 0.0
        for ch in anim["channels"]:
            sampler = anim["samplers"][ch["sampler"]]
            times = read_accessor(g, buf, sampler["input"])[:, 0]
            vals = read_accessor(g, buf, sampler["output"])
            chans.append((ch["target"]["node"], ch["target"]["path"], times, vals))
            t_end = max(t_end, times[-1])
        for step in range(SAMPLES_PER_CLIP):
            t = t_end * step / SAMPLES_PER_CLIP
            overrides = {}
            for node, path, times, vals in chans:
                key = {"translation": "t", "rotation": "r", "scale": "s"}.get(path)
                if key:
                    overrides.setdefault(node, {})[key] = sample_channel(times, vals, t, path)
            pose = skinned(overrides)
            yield pose.min(axis=0), pose.max(axis=0)


def model_box(folder, scale):
    g, buf = load_model(folder)
    bind_min_y = None
    width = depth = 0.0
    top = -1e9
    for lo, hi in posed_extents(g, buf):
        if bind_min_y is None:
            bind_min_y = lo[1]
        width = max(width, hi[0] - lo[0])
        depth = max(depth, hi[2] - lo[2])
        top = max(top, hi[1])
    height = top - bind_min_y
    return np.array([width, height, depth]) * scale


def audit(only=None):
    rows = []
    for path in sorted(glob.glob(os.path.join(CREATURES, "*", "creature.json"))):
        folder = os.path.dirname(path)
        name = os.path.basename(folder)
        if only and name not in only:
            continue
        if not os.path.exists(os.path.join(folder, "model.gltf")):
            continue
        data = json.load(open(path, encoding="utf-8"))
        gltf = (data.get("visual", {}).get("gltf") or {})
        scale = gltf.get("model_scale", 1.0)
        box = np.maximum(model_box(folder, scale), MIN_BOX)
        rows.append((name, path, data, box))
    return rows


def ratio(new, old):
    return new / old if old > 1e-6 else 99.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--tolerance", type=float, default=0.25)
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    only = {s for s in args.only.split(",") if s}

    bad = 0
    print(f"{'name':16} {'box (w,h,d)':22} {'model (w,h,d)':22} ratio(w,h,d)")
    for name, path, data, box in audit(only):
        old = np.array(data["bounds"]["rest"], dtype=np.float64)
        r = np.array([ratio(box[i], old[i]) for i in range(3)])
        mismatch = bool(np.any(np.abs(r - 1.0) > args.tolerance))
        flag = "  <-- MISMATCH" if mismatch else ""
        print(f"{name:16} {str(old.round(2).tolist()):22} {str(box.round(2).tolist()):22} "
              f"{r.round(2).tolist()}{flag}")
        if not mismatch:
            continue
        bad += 1
        if args.apply:
            apply_bounds(path, data, box, old)
    if args.check and bad:
        print(f"{bad} creature(s) have bounds that do not match the visible model")
        return 1
    return 0


def apply_bounds(path, data, box, old):
    new = [round(float(max(0.15, v)), 2) for v in box]
    b = data["bounds"]
    min_ratio = (b["min"][1] / b["rest"][1]) if b["rest"][1] else 1.0
    b["rest"] = list(new)
    b["max"] = list(new)
    b["min"] = [new[0], round(new[1] * min(1.0, min_ratio), 2), new[2]]
    if "eye_height" in data and old[1] > 1e-6:
        data["eye_height"] = round(data["eye_height"] * new[1] / old[1], 2)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")


if __name__ == "__main__":
    sys.exit(main())
