"""Compare source-asset capability signals; counts alone are not art quality."""
from __future__ import annotations

import collections
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image

from review_creature_redesign import ROOT, accessor, extent, faces_for, read


def metrics(folder):
    definition = read(folder / "creature.json")
    visual = definition["visual"]
    result = {"backend": visual["backend"], "textures": [], "bounds": definition["bounds"]["rest"]}
    for p in sorted((folder / "textures").glob("*.png")):
        with Image.open(p) as im:
            rgba = np.array(im.convert("RGBA"))
        colors = rgba[rgba[:,:,3] > 12, :3]
        result["textures"].append({"stem": p.stem, "size": [rgba.shape[1], rgba.shape[0]],
                                   "opaque_colors": len(np.unique(colors, axis=0))})
    f = faces_for(folder)
    lo, hi = extent(f)
    result["rest_extent_blocks"] = (hi-lo).round(5).tolist()
    if visual["backend"] == "gltf_skeleton":
        g = read(folder / "model.gltf")
        blobs = [(folder / b["uri"]).read_bytes() for b in g["buffers"]]
        result["joints"] = max([len(s["joints"]) for s in g.get("skins", [])], default=0)
        result["triangles"] = sum(g["accessors"][p["indices"]]["count"]//3 for m in g["meshes"] for p in m["primitives"])
        result["clips"] = {}
        animated = set()
        for a in g.get("animations", []):
            tracks = []
            for c in a.get("channels", []):
                s = a["samplers"][c["sampler"]]
                values = accessor(g, blobs, s["output"])
                node = c["target"]["node"]
                animated.add(g["nodes"][node].get("name", str(node)))
                tracks.append({"bone": g["nodes"][node].get("name"), "path": c["target"]["path"],
                               "keys": len(values), "duration": float(accessor(g, blobs, s["input"])[-1,0]),
                               "component_range": np.ptp(values,axis=0).round(6).tolist()})
            result["clips"][a.get("name", "")] = tracks
        names = [g["nodes"][j].get("name", str(j)) for s in g.get("skins", []) for j in s["joints"]]
        result["never_directly_animated_joints"] = [n for n in names if n not in animated]
        child_nodes = {c for n in g["nodes"] for c in n.get("children", [])}
        result["scene_roots_with_parent"] = [i for scene in g.get("scenes", []) for i in scene.get("nodes", []) if i in child_nodes]
        total, multi = 0, 0
        for m in g["meshes"]:
            for p in m["primitives"]:
                index = p["attributes"].get("WEIGHTS_0")
                if index is not None:
                    w = accessor(g, blobs, index)
                    total += len(w)
                    multi += int(((w > 0.001).sum(axis=1)>1).sum())
        result["multi_weight_fraction"] = multi/max(1,total)
    elif visual["backend"] == "bone_skeleton":
        g = read(folder / visual.get("geometry_file", "geometry.geo.json"))
        b = g.get(visual.get("geometry"))
        if b is None:
            b = next(e for e in g["minecraft:geometry"] if e["description"]["identifier"] == visual["geometry"])
        result["bones"] = len(b["bones"])
        result["cubes"] = sum(len(n.get("cubes",[])) for n in b["bones"])
    return result


def main():
    baseline = ROOT / "assetgen_output/creatures/review_2026_11_10/baseline"
    rows = []
    for p in sorted((ROOT / "models/creatures").glob("*/creature.json")):
        sid = p.parent.name
        old, new = metrics(baseline / "models/creatures" / sid), metrics(p.parent)
        rows.append({"id": sid, "baseline": old, "current": new,
                     "lost_clips": sorted(set(old.get("clips", {}))-set(new.get("clips", {})))})
    g = [r for r in rows if r["current"]["backend"] == "gltf_skeleton"]
    summary = {
        "species": len(rows),
        "gltf_joint_reductions": sum(r["current"]["joints"]<r["baseline"]["joints"] for r in g),
        "gltf_lost_clip_species": [r["id"] for r in g if r["lost_clips"]],
        "gltf_scene_root_errors": [r["id"] for r in g if r["current"]["scene_roots_with_parent"]],
        "gltf_all_rigid_weights": all(r["current"]["multi_weight_fraction"] == 0 for r in g),
        "manifest_states": dict(collections.Counter(r.get("state") for r in read(ROOT/"models/creatures/asset_manifest.json")["species"])),
    }
    out = ROOT / "docs/research/creature-review-2026-11-10/metrics.json"
    out.write_text(json.dumps({"baseline_revision":"b35f00a75", "candidate_revision":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),
                               "scope":"Counts are diagnostic signals, not aesthetic acceptance; offline extents include bind transforms and scale.",
                               "summary":summary,"species":rows},ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(summary,indent=2))
    for sid in ("badger","crab","hermitcrab","lobster","seahorse","stingray","wasp","water_dragon"):
        r=next(r for r in rows if r["id"]==sid)
        print(sid, {k:(r['baseline'].get(k),r['current'].get(k)) for k in ('joints','triangles','rest_extent_blocks')},'lost',r['lost_clips'])


if __name__ == "__main__":
    main()
