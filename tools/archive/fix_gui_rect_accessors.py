#!/usr/bin/env python3
"""Fix remaining GuiRect .x/.y/.w/.h accessors in Gui module."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src" / "Gui"
AXES = ("x", "y", "w", "h")
UPPER = {"x": "X", "y": "Y", "w": "W", "h": "H"}

# Common GuiRect holder names in this codebase.
PREFIXES = (
    "rect", "r", "bounds", "Bounds", "area", "clientArea", "listArea",
    "cursor", "vp", "contentRect", "titleBar", "tabRect", "contentBounds",
    "pageBounds", "anchor", "bar", "track", "thumb", "client", "parent",
    "other", "slot", "clip", "inset", "region", "mouse", "panel", "content",
    "viewport", "hit", "item", "cell", "frame", "box", "rc", "btn", "tab",
    "row", "col", "grid", "layout", "spec", "result", "inner", "outer",
    "label", "icon", "hotbar", "footer", "header", "dialog", "screen",
    "window", "widget", "list", "scroll", "popup", "menu", "page", "field",
    "input", "check", "tile", "ghost", "drag", "drop", "hint", "pad",
)


def fix_text(text: str) -> str:
    for prefix in PREFIXES:
        for lo, up in UPPER.items():
            text = re.sub(
                rf"\b{re.escape(prefix)}\.{lo}\b",
                f"{prefix}.{up}",
                text,
            )
    # GuiMouseEvent / similar partial renames
    for lo, up in UPPER.items():
        text = re.sub(rf"\bevent\.{lo}\b", f"event.{up}", text)
        text = re.sub(rf"\bev\.{lo}\b", f"ev.{up}", text)
    return text


def main() -> int:
    n = 0
    for fp in sorted(ROOT.rglob("*")):
        if fp.suffix not in (".h", ".cpp"):
            continue
        orig = fp.read_text(encoding="utf-8")
        text = fix_text(orig)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(f"Fixed GuiRect accessors in {n} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
