#!/usr/bin/env python
"""Remove stray geometry (e.g. a leftover stand/perch) from a creature glTF.

Deletes every triangle whose three vertices all lie inside the box given by
--region (bind-pose coordinates, any of xmin/xmax/ymin/ymax/zmin/zmax) and
compacts the vertex streams so the orphaned vertices are gone too.

  python tools/strip_model_region.py models/creatures/owl --region xmax=-0.22,ymax=0.7

Rewrites model.gltf and model.bin in place (the original bytes are kept next to
them as model.gltf.orig / model.bin.orig the first time).
"""
import argparse
import json
import os
import shutil
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import creature_bounds_audit as audit  # noqa: E402

FMT = audit.COMPONENT


def accessor_slot(g, index):
    acc = g["accessors"][index]
    view = g["bufferViews"][acc["bufferView"]]
    fmt, size = FMT[acc["componentType"]]
    count = audit.TYPE_COUNT[acc["type"]]
    stride = view.get("byteStride", size * count)
    base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    return acc, fmt, size, count, stride, base


def parse_region(text):
    box = {}
    for part in text.split(","):
        key, value = part.split("=")
        box[key.strip()] = float(value)
    return box


def inside(p, box):
    return all(
        (p[0] >= box.get("xmin", -1e9), p[0] <= box.get("xmax", 1e9),
         p[1] >= box.get("ymin", -1e9), p[1] <= box.get("ymax", 1e9),
         p[2] >= box.get("zmin", -1e9), p[2] <= box.get("zmax", 1e9)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--region", required=True)
    ap.add_argument("--any-abs-z", type=float, default=None,
                    help="only remove triangles with a vertex at |z| >= this")
    args = ap.parse_args()
    box = parse_region(args.region)

    gltf_path = os.path.join(args.folder, "model.gltf")
    bin_path = os.path.join(args.folder, "model.bin")
    for path in (gltf_path, bin_path):
        if not os.path.exists(path + ".orig"):
            shutil.copyfile(path, path + ".orig")
    g, raw = audit.load_model(args.folder)
    buf = bytearray(raw)

    # Every accessor of a primitive must own its bytes, otherwise compaction
    # would corrupt a neighbour.
    owners = {}
    removed_total = 0
    for mesh in g["meshes"]:
        for prim in mesh["primitives"]:
            attr_ids = list(prim["attributes"].values()) + [prim["indices"]]
            for ai in attr_ids:
                view = g["accessors"][ai]["bufferView"]
                if owners.setdefault(view, ai) != ai:
                    sys.exit("accessors share a bufferView; unsupported")

            pos = audit.read_accessor(g, raw, prim["attributes"]["POSITION"])
            idx = audit.read_accessor(g, raw, prim["indices"])[:, 0].astype(np.int64)
            tris = idx.reshape(-1, 3)
            def doomed(t):
                if not all(inside(pos[v], box) for v in t):
                    return False
                if args.any_abs_z is not None:
                    return any(abs(pos[v][2]) >= args.any_abs_z for v in t)
                return True

            keep = np.array([not doomed(t) for t in tris])
            removed_total += int((~keep).sum())
            tris = tris[keep]
            used = np.unique(tris.reshape(-1))
            remap = {int(old): new for new, old in enumerate(used)}

            # Compact each vertex attribute.
            for name, ai in prim["attributes"].items():
                acc, fmt, size, count, stride, base = accessor_slot(g, ai)
                elems = [raw[base + int(o) * stride: base + int(o) * stride + size * count]
                         for o in used]
                for i, e in enumerate(elems):
                    buf[base + i * stride: base + i * stride + size * count] = e
                acc["count"] = len(used)
                if name == "POSITION":
                    kept = pos[used]
                    acc["min"] = [float(x) for x in kept.min(axis=0)]
                    acc["max"] = [float(x) for x in kept.max(axis=0)]
            # Rewrite indices with the new numbering.
            acc, fmt, size, count, stride, base = accessor_slot(g, prim["indices"])
            flat = [remap[int(v)] for v in tris.reshape(-1)]
            for i, v in enumerate(flat):
                struct.pack_into("<" + fmt, buf, base + i * stride, v)
            acc["count"] = len(flat)
            if "min" in acc:
                acc["min"] = [min(flat)] if flat else [0]
                acc["max"] = [max(flat)] if flat else [0]

    with open(bin_path, "wb") as f:
        f.write(bytes(buf))
    with open(gltf_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(g, f, indent=2)
        f.write("\n")
    print(f"removed {removed_total} triangle(s)")


if __name__ == "__main__":
    main()
