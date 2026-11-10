#!/usr/bin/env python3
"""Prepare and audit an isolated opaque-leaf resource-pack A/B variant."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def is_leaf_texture(path: Path) -> bool:
    name = path.name.lower()
    return path.suffix.lower() == ".png" and ("leaf" in name or "leaves" in name)


def alpha_profile(path: Path) -> dict:
    with Image.open(path) as source:
        image = source.convert("RGBA")
        alpha = np.asarray(image)[:, :, 3]
        return {
            "size": list(image.size),
            "transparent_share": float(np.mean(alpha == 0)),
            "partial_share": float(np.mean((alpha > 0) & (alpha < 255))),
            "opaque_share": float(np.mean(alpha == 255)),
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--audit", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    output = args.output.resolve()
    if source == output or source in output.parents:
        raise SystemExit("output must not be the source or its child")
    if not (source / "pack.json").is_file():
        raise SystemExit(f"missing source pack manifest: {source}")

    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(source, output)

    manifest_path = output / "pack.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["id"] = args.pack_id
    manifest["name"] = "Cubatarium Worldgen PERF opaque leaves"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    texture_root = output / "textures" / "blocks"
    leaf_files = sorted(
        path for path in texture_root.glob("*.png") if is_leaf_texture(path)
    )
    if not leaf_files:
        raise SystemExit("no leaf textures found")

    profiles = []
    for path in leaf_files:
        relative = path.relative_to(output).as_posix()
        before = alpha_profile(source / relative)
        with Image.open(path) as image:
            rgba = image.convert("RGBA")
            pixels = np.array(rgba)
            pixels[:, :, 3] = 255
            Image.fromarray(pixels, "RGBA").save(path)
        after = alpha_profile(path)
        if before["size"] != after["size"]:
            raise SystemExit(f"texture dimensions changed: {relative}")
        profiles.append(
            {
                "path": relative,
                "before_sha256": sha256(source / relative),
                "after_sha256": sha256(path),
                "before": before,
                "after": after,
            }
        )

    source_files = {
        path.relative_to(source).as_posix(): path
        for path in source.rglob("*")
        if path.is_file()
    }
    output_files = {
        path.relative_to(output).as_posix(): path
        for path in output.rglob("*")
        if path.is_file()
    }
    if source_files.keys() != output_files.keys():
        raise SystemExit("pack file set changed")
    allowed_changes = {"pack.json", *(item["path"] for item in profiles)}
    unexpected = [
        relative
        for relative in source_files
        if relative not in allowed_changes
        and sha256(source_files[relative]) != sha256(output_files[relative])
    ]
    if unexpected:
        raise SystemExit(f"unexpected pack changes: {unexpected}")

    audit = {
        "source_pack": str(source),
        "output_pack": str(output),
        "pack_id": args.pack_id,
        "changed_non_manifest_files": len(profiles),
        "unexpected_changes": unexpected,
        "textures": profiles,
    }
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    args.audit.write_text(
        json.dumps(audit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"opaque-leaf pack: {len(profiles)} textures; audit: {args.audit}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
