#!/usr/bin/env python3
"""Convert a .glb (binary glTF) into model.gltf + model.bin in a destination folder."""

from __future__ import annotations

import json
import struct
from pathlib import Path


def glb_to_gltf(src: Path, dest_dir: Path, gltf_name: str = "model.gltf") -> Path:
    data = src.read_bytes()
    if len(data) < 12:
        raise ValueError(f"too small for GLB: {src}")
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:  # glTF
        raise ValueError(f"not a GLB (magic={magic:#x}): {src}")
    if version != 2:
        raise ValueError(f"unsupported GLB version {version}: {src}")

    offset = 12
    json_bytes = None
    bin_bytes = b""
    while offset + 8 <= len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        offset += 8
        chunk = data[offset : offset + chunk_len]
        offset += chunk_len
        if chunk_type == 0x4E4F534A:  # JSON
            json_bytes = chunk
        elif chunk_type == 0x004E4942:  # BIN
            bin_bytes = chunk

    if json_bytes is None:
        raise ValueError(f"GLB missing JSON chunk: {src}")

    gltf = json.loads(json_bytes.decode("utf-8"))
    dest_dir.mkdir(parents=True, exist_ok=True)
    bin_name = "model.bin"
    if bin_bytes:
        (dest_dir / bin_name).write_bytes(bin_bytes)
        if "buffers" not in gltf or not gltf["buffers"]:
            gltf["buffers"] = [{"byteLength": len(bin_bytes)}]
        gltf["buffers"][0]["uri"] = bin_name
        gltf["buffers"][0]["byteLength"] = len(bin_bytes)
    else:
        # JSON-only GLB
        for buf in gltf.get("buffers", []):
            buf.pop("uri", None)

    out = dest_dir / gltf_name
    out.write_text(json.dumps(gltf, indent=2) + "\n", encoding="utf-8")
    return out


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=Path)
    ap.add_argument("dest_dir", type=Path)
    args = ap.parse_args()
    path = glb_to_gltf(args.src, args.dest_dir)
    print(path)
