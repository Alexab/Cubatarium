#!/usr/bin/env python3
"""Create minimal cubatarium_cc0_base resource pack for smoke tests.

DEPRECATED for release assets — use tools/rebuild_release_resource_packs.py
(Kenney CC0 tiles) instead.
"""
import json
import struct
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


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
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def solid_frame(rgb: list[int], size: int = 16) -> bytes:
    return bytes(rgb * 4) * (size * size)


def fluid_strip(rgb_frames: list[list[int]], size: int = 16) -> bytes:
    return b"".join(solid_frame(c, size) for c in rgb_frames)


def main() -> None:
    base = REPO / "resource_packs" / "cubatarium_cc0_base"
    base.mkdir(parents=True, exist_ok=True)
    (base / "pack.json").write_text(
        json.dumps(
            {
                "id": "cubatarium_cc0_base",
                "name": "Cubatarium CC0 Base",
                "version": 1,
                "license": "CC0-1.0",
                "resolution": 16,
                "priority": 10,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    (base / "LICENSE.txt").write_text("CC0-1.0 placeholder base pack\n", encoding="utf-8")

    colors = {
        "dirt": [107, 77, 46],
        "stone": [128, 128, 128],
        "grass_side": [90, 140, 60],
        "grass_top": [100, 180, 70],
        "tree_side": [100, 70, 40],
        "tree_top": [120, 90, 50],
        "sand": [194, 178, 128],
        "water": [40, 80, 200],
        "lava": [220, 80, 20],
        "fire_0": [255, 120, 20],
        "fire_1": [255, 200, 40],
        "sandstone": [180, 160, 100],
        "gravel": [130, 130, 130],
        "snow": [240, 240, 255],
        "clay": [150, 120, 100],
        "ice": [180, 220, 255],
        "hellrock": [60, 30, 30],
        "bedrock": [50, 50, 50],
    }

    fluid_anim = {"frame_count": 4, "frametime": 2}
    water_extra = {
        "physics": {"preset": "water"},
        "render": {"transparent": True, "style": "fluid"},
        "animation": fluid_anim,
    }
    lava_extra = {
        "physics": {"preset": "lava"},
        "render": {"transparent": True, "style": "fluid"},
        "animation": fluid_anim,
    }
    fire_extra = {
        "physics": {"preset": "fire"},
        "render": {"transparent": True},
        "animation": {"frame_count": 2, "frametime": 2},
    }

    blocks = {
        "dirt": (["dirt"] * 6, ["natural"], None),
        "stone": (["stone"] * 6, ["building"], None),
        "grass": (
            ["grass_side", "grass_side", "grass_side", "grass_side", "grass_top", "dirt"],
            ["natural"],
            None,
        ),
        "wood": (
            ["tree_side", "tree_side", "tree_side", "tree_side", "tree_top", "tree_top"],
            ["natural"],
            None,
        ),
        "sand": (["sand"] * 6, ["natural"], None),
        "sandstone": (["sandstone"] * 6, ["building"], None),
        "gravel": (["gravel"] * 6, ["natural"], None),
        "snow": (["snow"] * 6, ["natural"], None),
        "clay": (["clay"] * 6, ["natural"], None),
        "ice": (["ice"] * 6, ["natural"], None),
        "hellrock": (["hellrock"] * 6, ["natural"], None),
        "bedrock": (["bedrock"] * 6, ["building"], None),
        "water": (["water"] * 6, ["natural"], water_extra),
        "lava": (["lava"] * 6, ["natural"], lava_extra),
        "fire": (
            ["fire_0"] * 6 + ["fire_1"] * 6,
            ["natural"],
            fire_extra,
        ),
    }

    for name, (stems, types, extra) in blocks.items():
        (base / "blocks").mkdir(parents=True, exist_ok=True)
        block = {"name": name, "types": types, "textures": stems}
        if extra:
            block.update(extra)
        (base / "blocks" / f"{name}.json").write_text(
            json.dumps(block, indent=2),
            encoding="utf-8",
        )

    for stem, c in colors.items():
        tex = base / "textures" / "blocks" / f"{stem}.png"
        if stem == "water":
            frames = [
                [40, 80, 200],
                [45, 90, 210],
                [50, 100, 220],
                [45, 90, 210],
            ]
            rgba = fluid_strip(frames)
            write_png(tex, 16, 64, rgba)
        elif stem == "lava":
            frames = [
                [200, 60, 10],
                [220, 80, 20],
                [240, 100, 30],
                [220, 80, 20],
            ]
            rgba = fluid_strip(frames)
            write_png(tex, 16, 64, rgba)
        elif stem in ("fire_0", "fire_1"):
            rgba = solid_frame(c)
            write_png(tex, 16, 16, rgba)
        else:
            rgba = solid_frame(c)
            write_png(tex, 16, 16, rgba)

    print(f"Created {base}")


if __name__ == "__main__":
    main()
