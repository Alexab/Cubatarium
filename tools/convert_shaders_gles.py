#!/usr/bin/env python3
"""Convert desktop GLSL 330 shaders to GLES 300 es in shaders/gles/."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "shaders"
DST = ROOT / "shaders" / "gles"

FRAGMENT_HINTS = ("fshader", "gui_textured_f", "_f.glsl")


def is_fragment(name: str) -> bool:
    lower = name.lower()
    return any(h in lower for h in FRAGMENT_HINTS) or lower.startswith("f")


def convert_source(text: str, filename: str) -> str:
    text = re.sub(r"#version\s+330\s+core\s*\n", "#version 300 es\n", text)
    if is_fragment(filename) and "precision " not in text:
        text = text.replace(
            "#version 300 es\n",
            "#version 300 es\nprecision mediump float;\n",
            1,
        )
    return text


def main() -> None:
    DST.mkdir(parents=True, exist_ok=True)
    count = 0
    for src in sorted(SRC.glob("*.glsl")):
        out = DST / src.name
        out.write_text(convert_source(src.read_text(encoding="utf-8"), src.name), encoding="utf-8")
        count += 1
    print(f"Wrote {count} GLES shaders to {DST}")


if __name__ == "__main__":
    main()
