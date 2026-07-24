#!/usr/bin/env python3
"""Rename struct fields from camelCase/lowercase to PascalCase (C++ identifiers only)."""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"
MAP_FILE = Path(__file__).resolve().parent / "struct_field_map.json"

# Do not rename identifiers inside string literals on these lines (JSON keys).
JSON_LINE_MARKERS = ('["', "['", '.value("', '.contains("', '<< "', "cout <<")


def load_fields() -> list[tuple[str, str]]:
    data = json.loads(MAP_FILE.read_text(encoding="utf-8"))
    fields = [(o, n) for o, n in data["fields"]]
    fields.sort(key=lambda x: len(x[0]), reverse=True)
    return fields


def line_has_json_key(line: str, old: str) -> bool:
    if any(m in line for m in JSON_LINE_MARKERS):
        return f'"{old}"' in line or f"'{old}'" in line
    return False


GUI_RECT_PREFIXES = (
    "Bounds", "bounds", "rect", "r", "area", "client", "parent", "other",
    "slot", "clip", "inset", "region", "event", "ev", "mouse", "panel",
    "content", "viewport", "hit", "item", "cell", "frame", "box", "rc",
)

GUI_RECT_AXES = (("x", "X"), ("y", "Y"), ("w", "W"), ("h", "H"))


def apply_gui_rect_axes(text: str) -> str:
    for prefix in GUI_RECT_PREFIXES:
        for old, new in GUI_RECT_AXES:
            text = re.sub(
                rf"\b{re.escape(prefix)}\.{old}\b",
                f"{prefix}.{new}",
                text,
            )
    # Struct field declarations in GuiTypes.h
    for old, new in GUI_RECT_AXES:
        text = re.sub(
            rf"(struct GuiRect\b[^{{}}]*\{{[^}}]*?)\b{old}\b",
            rf"\g<1>{new}",
            text,
            flags=re.S,
        )
    return text


def apply_fields(text: str, fields: list[tuple[str, str]]) -> str:
  lines = text.split("\n")
  out_lines = []
  for line in lines:
    new_line = line
    if not any(m in line for m in JSON_LINE_MARKERS) or not any(
        f'"{o}"' in line for o, _ in fields
    ):
      for old, new in fields:
        if old == new:
          continue
        if line_has_json_key(line, old):
          continue
        new_line = re.sub(r"\b" + re.escape(old) + r"\b", new, new_line)
    else:
      for old, new in fields:
        if old == new or line_has_json_key(line, old):
          continue
        # Replace only outside quoted JSON keys: skip if "old" appears as key
        if f'"{old}"' in line:
          continue
        new_line = re.sub(r"\b" + re.escape(old) + r"\b", new, new_line)
    out_lines.append(new_line)
  return "\n".join(out_lines)


def main() -> int:
    fields = load_fields()
    changed = 0
    for fp in sorted(ROOT.rglob("*")):
        if fp.suffix not in (".h", ".cpp") or fp.name == "stb_image.h":
            continue
        orig = fp.read_text(encoding="utf-8")
        text = apply_fields(orig, fields)
        text = apply_gui_rect_axes(text)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            changed += 1
    print(f"Updated struct fields in {changed} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
