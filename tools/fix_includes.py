#!/usr/bin/env python3
"""Revert U-prefix in #include file paths (filenames stay original)."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"
SKIP = {"stb_image.h"}

# Class name in code -> actual header basename (without .h)
SPECIAL_HEADER = {
    "GuiQuadBatch": "UiQuadBatch",
    "GuiTexturedQuadBatch": "UiTexturedQuadBatch",
}

STRIP_U = {
    "TerrestrialBipedPosePresenter", "CreaturePosePresenterRegistry",
    "CreatureLocomotionController", "CreatureDefinitionStorage",
    "CreatureActivityDirector", "WorldCreatureActivitySink",
    "CreatureActivityRegistry", "BlockDefinitionStorage",
    "OverworldBiomesPipeline", "ProceduralWorldGenFactory",
    "GreedyTransparentPipeline", "CreatureTextureStorage",
    "OverworldHeightSampler", "WorldGenSettingsForm",
    "CreativePaletteScreen", "ConsoleCommandHistory",
    "OverworldFullPipeline", "SkinDefinitionStorage",
    "InGameHudScreen", "ContentTypeRegistry", "CreatureVisualRigid",
    "WanderActivityAgent", "CreatureVisualGltf", "CreatureIconCache",
    "CreatureInventory", "GuiTexturedQuadBatch", "GreedyMesher",
    "ShaderManager", "ChunkMeshCache", "MainMenuScreen",
    "OverworldPipeline", "SettingsScreen", "CommandRegistry",
    "LoadWorldScreen", "PrefabIconCache", "AnimationClock",
    "BlockRegistry", "ChunkStreamer", "ChunkManager", "ConsoleScreen",
    "NewWorldScreen", "CreatureVisualFactory", "CreatureAppearance",
    "GuiInputRouter", "GuiScreenBase", "GuiScrollView", "GuiTextInput",
    "GuiDialogFrame", "GuiPopupMenu", "TextureCubeStorage",
    "TextureBaseStorage", "LegacyHashPipeline", "ObjectPrototype",
    "ObjectStorage", "WorldGenerator", "CreatureVisual", "GameSession",
    "GuiCheckbox", "GuiListView", "GuiRenderer", "GuiTabBar",
    "GuiButton", "GuiContext", "GuiIconSource", "PrefabLibrary",
    "FlatPipeline", "BiomeSampler", "GlStateScope", "GuiLayout",
    "GuiPanel", "GuiLabel", "GuiWindow", "GuiWidget", "GuiSlot",
    "GuiQuadBatch", "ShaderProgram", "BlockWorld", "TextureCube",
    "TextureBase", "BlockRaycast", "PrefabUtil", "Creature", "Camera",
    "Player", "Chunk", "CubeGL", "Object", "Cube", "User",
    "TerrainPlane", "SingleCube", "Terrain", "Person", "Rect",
    "AndroidPlatformWindow", "AndroidPlatformPaths",
    "DesktopPlatformWindow", "DesktopPlatformPaths",
    "TouchInputBridge", "GuiTouchControls",
    "TouchControlPanel", "TouchHoldButton", "TouchVirtualJoystick",
    "TouchLookPad", "GlfwClipboard", "NullClipboard", "AssetStreamBuf",
    "GuiQuadBatch", "GuiTexturedQuadBatch",
}

INCLUDE_RE = re.compile(r'(#include\s+["<])([^">]+)([">])')


def fix_include_path(path: str) -> str:
    parts = path.replace("\\", "/").split("/")
    fname = parts[-1]
    if not fname.endswith(".h"):
        return path
    base = fname[:-2]
    if base.startswith("U") and base[1:] in STRIP_U:
        stem = SPECIAL_HEADER.get(base[1:], base[1:])
        parts[-1] = stem + ".h"
        return "/".join(parts)
    return path


def main():
    n = 0
    for fp in ROOT.rglob("*"):
        if fp.suffix not in (".h", ".cpp") or fp.name in SKIP:
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text

        def repl(m):
            return m.group(1) + fix_include_path(m.group(2)) + m.group(3)

        text = INCLUDE_RE.sub(repl, text)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(f"Fixed includes in {n} files")


if __name__ == "__main__":
    main()
