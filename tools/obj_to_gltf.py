#!/usr/bin/env python3
"""Convert OBJ/FBX mesh to model.gltf in a destination folder (trimesh)."""

from __future__ import annotations

from pathlib import Path

import trimesh


def mesh_to_gltf(src: Path, dest_dir: Path, gltf_name: str = "model.gltf") -> Path:
    if not src.is_file():
        raise FileNotFoundError(src)
    dest_dir.mkdir(parents=True, exist_ok=True)
    mesh = trimesh.load(src, force="mesh", process=True)
    if isinstance(mesh, trimesh.Scene):
        mesh = trimesh.util.concatenate(tuple(mesh.geometry.values()))
    out = dest_dir / gltf_name
    mesh.export(out)
    return out


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=Path)
    ap.add_argument("dest_dir", type=Path)
    args = ap.parse_args()
    print(mesh_to_gltf(args.src, args.dest_dir))
