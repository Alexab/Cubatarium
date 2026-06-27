#!/usr/bin/env python3
"""Repair includes and identifiers broken by style refactor collisions."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

INCLUDE_FIXES = {
    "App/Settings/Render.h": "App/Settings/RenderSettings.h",
    "App/Settings/Ui.h": "App/Settings/UiSettings.h",
    "Gui/Batch/UGuiQuadBatch.h": "Gui/Batch/UiQuadBatch.h",
    "Gui/Batch/UGuiTexturedQuadBatch.h": "Gui/Batch/UiTexturedQuadBatch.h",
}

INCLUDE_PREFIX_FIXES = [
    ("Creatures/UPlayer/", "Creatures/Player/"),
]


def fix_includes(text: str) -> str:
    for old, new in INCLUDE_FIXES.items():
        text = text.replace(f'#include "{old}"', f'#include "{new}"')
    for old, new in INCLUDE_PREFIX_FIXES:
        text = re.sub(
            rf'#include\s+"{re.escape(old)}([^"]+)"',
            rf'#include "{new}\1"',
            text,
        )
    return text


def fix_gui_rect(text: str) -> str:
    if "struct GuiRect" not in text:
        return text
  # Complete partial GuiRect migration.
    text = text.replace("int y{0};", "int Y{0};")
    text = text.replace("int w{0};", "int W{0};")
    text = text.replace("int h{0};", "int H{0};")
    replacements = [
        ("px >= x && px < x + w", "px >= X && px < X + W"),
        ("py >= y && py < y + h", "py >= Y && py < Y + H"),
        ("x < other.X + other.W && x + w > other.X && y < other.Y + other.H &&\n           y + h > other.Y",
         "X < other.X + other.W && X + W > other.X && Y < other.Y + other.H &&\n           Y + H > other.Y"),
        ("return {x + pad, y + pad, w - 2 * pad, h - 2 * pad};",
         "return {X + pad, Y + pad, W - 2 * pad, H - 2 * pad};"),
    ]
    for old, new in replacements:
        text = text.replace(old, new)
    return text


def fix_console_history(text: str) -> str:
    if "UConsoleCommandHistory" not in text:
        return text
    text = text.replace(
        "const std::vector<std::string> &Entries() const { return Entries; }",
        "const std::vector<std::string> &Entries() const { return History; }",
    )
    text = text.replace(
        "std::vector<std::string> Entries;",
        "std::vector<std::string> History;",
    )
    text = text.replace("Entries.size()", "History.size()")
    text = text.replace("Entries[", "History[")
    text = text.replace("Entries.push_back", "History.push_back")
    text = text.replace("Entries.clear", "History.clear")
    text = text.replace("Entries.empty", "History.empty")
    text = text.replace("for (const auto &entry : Entries)", "for (const auto &entry : History)")
    return text


def fix_shader_manager_members(text: str) -> str:
    # Member ShaderManager was corrupted to type name UShaderManager by class rename.
    fixes = [
        ("UShaderManager(std::move(shader_manager))", "ShaderManager(std::move(shader_manager))"),
        ("if (!UShaderManager ||", "if (!ShaderManager ||"),
        ("if (!UShaderManager)", "if (!ShaderManager)"),
        ("if (!UShaderManager ", "if (!ShaderManager "),
        ("UShaderManager->", "ShaderManager->"),
        ("Initialize(UShaderManager,", "Initialize(ShaderManager,"),
        ("&& UShaderManager)", "&& ShaderManager)"),
        ("BlockDefinitions, UShaderManager)", "BlockDefinitions, ShaderManager)"),
        ("UShaderManager);", "ShaderManager);"),
    ]
    for old, new in fixes:
        text = text.replace(old, new)
    return text


def main() -> int:
    changed = 0
    for fp in sorted(ROOT.rglob("*")):
        if fp.suffix not in (".h", ".cpp"):
            continue
        orig = fp.read_text(encoding="utf-8")
        text = orig
        text = fix_includes(text)
        text = fix_gui_rect(text)
        text = fix_console_history(text)
        text = fix_shader_manager_members(text)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            changed += 1
    print(f"Repaired {changed} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
