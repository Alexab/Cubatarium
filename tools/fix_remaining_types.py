#!/usr/bin/env python3
"""Fix remaining type names missed or partially renamed."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"
SKIP = {"stb_image.h"}

# Type renames: old -> new (word boundary)
TYPE_RENAMES = [
    ("User", "UUser"),
    ("Camera", "UCamera"),
    ("Player", "UPlayer"),
    ("Chunk", "UChunk"),
    ("CubeGL", "UCubeGL"),
    ("Cube", "UCube"),
    ("Object", "UObject"),
    ("GameSession", "UGameSession"),
    ("GuiContext", "UGuiContext"),
    ("GuiRenderer", "UGuiRenderer"),
    ("GuiWidget", "UGuiWidget"),
    ("GuiScreenBase", "UGuiScreenBase"),
    ("GuiInputRouter", "UGuiInputRouter"),
    ("GuiIconSource", "UGuiIconSource"),
    ("MainMenuScreen", "UMainMenuScreen"),
    ("ShaderManager", "UShaderManager"),
    ("BlockDefinitionStorage", "UBlockDefinitionStorage"),
    ("Creature", "UCreature"),
]

# After Creature->UCreature, fix double-U and compound types
FIX_DOUBLE = [
    ("UUCreature", "UCreature"),
    ("UUCamera", "UCamera"),
    ("UUUser", "UUser"),
    ("UUChunk", "UChunk"),
    ("UUCube", "UCube"),
    ("UUObject", "UObject"),
    ("UUGui", "UGui"),
    ("UUWorld", "UWorld"),
    ("UUCore", "UCore"),
    ("UUPlayer", "UPlayer"),
    ("UUGameSession", "UGameSession"),
    ("UUMainMenuScreen", "UMainMenuScreen"),
    ("UUBlockDefinitionStorage", "UBlockDefinitionStorage"),
    ("UUShaderManager", "UShaderManager"),
    ("UUCreatureInventory", "UCreatureInventory"),
    ("UUCreatureDefinition", "UCreatureDefinition"),
    ("UUCreatureLocomotion", "UCreatureLocomotion"),
    ("UUCreatureVisual", "UCreatureVisual"),
    ("UUCreatureActivity", "UCreatureActivity"),
    ("UUCreatureTexture", "UCreatureTexture"),
    ("UUCreatureAppearance", "UCreatureAppearance"),
    ("UUCreatureBounds", "UCreatureBounds"),
    ("UUCreatureIntent", "UCreatureIntent"),
    ("UUCreatureCatalog", "UCreatureCatalog"),
    ("UUCreaturePose", "UCreaturePose"),
    ("UUCreatureIcon", "UCreatureIcon"),
]

MEMBER_FIXES = [
    ("chunks_.", "Chunks."),
    ("chunks_", "Chunks"),
    ("id_", "Id"),
    ("typeId_", "TypeId"),
    ("skinId_", "SkinId"),
    ("bodyOrigin_", "BodyOrigin"),
    ("eyeOffset_", "EyeOffset"),
    ("selectedAppearanceTypeId_", "SelectedAppearanceTypeId"),
    ("selectedSkinId_", "SelectedSkinId"),
]


def rename_type(text: str, old: str, new: str) -> str:
    if old == new:
        return text
    # Skip if would damage already-correct names
    pat = re.compile(r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])")
    return pat.sub(new, text)


def process(text: str) -> str:
    # Sort longest first
    for old, new in sorted(TYPE_RENAMES, key=lambda x: len(x[0]), reverse=True):
        text = rename_type(text, old, new)
    for old, new in FIX_DOUBLE:
        text = text.replace(old, new)
    for old, new in MEMBER_FIXES:
        text = re.sub(r"\b" + re.escape(old) + r"\b", new, text)
    return text


def main():
    n = 0
    for fp in ROOT.rglob("*"):
        if fp.suffix not in (".h", ".cpp") or fp.name in SKIP:
            continue
        orig = fp.read_text(encoding="utf-8")
        text = process(orig)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(f"Updated {n} files")


if __name__ == "__main__":
    main()
