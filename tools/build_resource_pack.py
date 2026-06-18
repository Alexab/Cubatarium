#!/usr/bin/env python3
"""Build a Cubatarium resource pack from a YAML mapping and source PNG folder."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import zlib
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw = b"".join(b"\x00" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def solid_rgba(rgb: tuple[int, int, int], size: int = 16) -> bytes:
    return bytes(list(rgb) * 4) * (size * size)


def read_png_size(path: Path) -> tuple[int, int] | None:
    try:
        raw = path.read_bytes()
        if raw[:8] != b"\x89PNG\r\n\x1a\n":
            return None
        return struct.unpack(">I", raw[16:20])[0], struct.unpack(">I", raw[20:24])[0]
    except OSError:
        return None


def upscale_png_nearest(src: Path, dst: Path, target_w: int, target_h: int | None = None) -> None:
    """Nearest-neighbor resize to target_w x target_h (height derived if omitted)."""
    import struct as st
    import zlib as zl

    if target_h is None:
        target_h = target_w

    raw = src.read_bytes()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        shutil.copy2(src, dst)
        return
    sw = st.unpack(">I", raw[16:20])[0]
    sh = st.unpack(">I", raw[20:24])[0]
    if sw == target_w and sh == target_h:
        shutil.copy2(src, dst)
        return

    pos = 8
    idat = b""
    while pos < len(raw):
        length = st.unpack(">I", raw[pos : pos + 4])[0]
        tag = raw[pos + 4 : pos + 8]
        data = raw[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if tag == b"IDAT":
            idat += data
        elif tag == b"IEND":
            break
    inflated = zl.decompress(idat)
    src_rgba = bytearray()
    stride = 1 + sw * 4
    for y in range(sh):
        row = inflated[y * stride + 1 : y * stride + 1 + sw * 4]
        src_rgba.extend(row)

    out = bytearray()
    for y in range(target_h):
        sy = y * sh // target_h if target_h else 0
        for x in range(target_w):
            sx = x * sw // target_w if target_w else 0
            idx = (sy * sw + sx) * 4
            out.extend(src_rgba[idx : idx + 4])

    write_png(dst, target_w, target_h, bytes(out))


def copy_or_generate_texture(
    src_png: Path, dst_png: Path, resolution: int, fallback_rgb: tuple[int, int, int]
) -> None:
    if src_png.is_file():
        size = read_png_size(src_png)
        if size:
            sw, sh = size
            target_w = resolution
            target_h = sh * resolution // sw if sw else resolution
            if sw != target_w or sh != target_h:
                upscale_png_nearest(src_png, dst_png, target_w, target_h)
            else:
                shutil.copy2(src_png, dst_png)
        else:
            shutil.copy2(src_png, dst_png)
    elif not dst_png.is_file():
        write_png(dst_png, resolution, resolution, solid_rgba(fallback_rgb, resolution))


def build_pack(
    mapping: dict,
    source: Path,
    out: Path,
    pack_id: str,
    priority: int,
    resolution: int,
) -> None:
    if out.exists():
        shutil.rmtree(out)
    blocks_dir = out / "blocks"
    tex_dir = out / "textures" / "blocks"
    blocks_dir.mkdir(parents=True)
    tex_dir.mkdir(parents=True)

    (out / "pack.json").write_text(
        json.dumps(
            {
                "id": pack_id,
                "name": pack_id.replace("_", " ").title(),
                "version": 1,
                "license": mapping.get("license", "CC0-1.0"),
                "resolution": resolution,
                "priority": priority,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    license_text = mapping.get("license_text", "CC0-1.0\n")
    (out / "LICENSE.txt").write_text(license_text, encoding="utf-8")

    for name, spec in mapping.get("blocks", {}).items():
        if isinstance(spec, str):
            stems = [spec] * 6
            types = ["natural"]
        else:
            stems = spec.get("faces")
            if stems is None:
                stems = [spec.get("texture", name)] * 6
            if len(stems) not in (6, 12):
                raise ValueError(f"block {name}: need 6 or 12 face stems")
            types = spec.get("types", ["natural"])
        block = {"name": name, "types": types, "textures": stems}
        if "physics" in spec and isinstance(spec, dict):
            block["physics"] = spec["physics"]
        if "render" in spec and isinstance(spec, dict):
            block["render"] = spec["render"]
        if "animation" in spec and isinstance(spec, dict):
            block["animation"] = spec["animation"]
        (blocks_dir / f"{name}.json").write_text(json.dumps(block, indent=2), encoding="utf-8")

        for stem in set(stems):
            src_png = source / f"{stem}.png"
            dst_png = tex_dir / f"{stem}.png"
            copy_or_generate_texture(src_png, dst_png, resolution, (160, 160, 160))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mapping", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=Path("."))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--priority", type=int, default=5)
    parser.add_argument("--resolution", type=int, default=16)
    args = parser.parse_args()

    if yaml is None:
        raise SystemExit("PyYAML required: pip install pyyaml")

    mapping = yaml.safe_load(args.mapping.read_text(encoding="utf-8"))
    build_pack(mapping, args.source, args.out, args.pack_id, args.priority, args.resolution)
    print(f"Built {args.out}")


if __name__ == "__main__":
    main()
