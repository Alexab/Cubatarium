#!/usr/bin/env python3
"""Bulk style refactor: class U-prefix renames and member trailing_ -> PascalCase."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"
SKIP_FILES = {"stb_image.h"}

# Longest names first to avoid partial replacements.
CLASS_RENAMES = [
    ("TerrestrialBipedPosePresenter", "UTerrestrialBipedPosePresenter"),
    ("CreaturePosePresenterRegistry", "UCreaturePosePresenterRegistry"),
    ("CreatureLocomotionController", "UCreatureLocomotionController"),
    ("CreatureDefinitionStorage", "UCreatureDefinitionStorage"),
    ("CreatureActivityDirector", "UCreatureActivityDirector"),
    ("WorldCreatureActivitySink", "UWorldCreatureActivitySink"),
    ("CreatureActivityRegistry", "UCreatureActivityRegistry"),
    ("BlockDefinitionStorage", "UBlockDefinitionStorage"),
    ("OverworldBiomesPipeline", "UOverworldBiomesPipeline"),
    ("ProceduralWorldGenFactory", "UProceduralWorldGenFactory"),
    ("GreedyTransparentPipeline", "UGreedyTransparentPipeline"),
    ("CreatureTextureStorage", "UCreatureTextureStorage"),
    ("OverworldHeightSampler", "UOverworldHeightSampler"),
    ("WorldGenSettingsForm", "UWorldGenSettingsForm"),
    ("CreativePaletteScreen", "UCreativePaletteScreen"),
    ("ConsoleCommandHistory", "UConsoleCommandHistory"),
    ("OverworldFullPipeline", "UOverworldFullPipeline"),
    ("SkinDefinitionStorage", "USkinDefinitionStorage"),
    ("InGameHudScreen", "UInGameHudScreen"),
    ("ContentTypeRegistry", "UContentTypeRegistry"),
    ("CreatureVisualRigid", "UCreatureVisualRigid"),
    ("WanderActivityAgent", "UWanderActivityAgent"),
    ("CreatureVisualGltf", "UCreatureVisualGltf"),
    ("CreatureIconCache", "UCreatureIconCache"),
    ("CreatureInventory", "UCreatureInventory"),
    ("UiTexturedQuadBatch", "UGuiTexturedQuadBatch"),
    ("GreedyMesher", "UGreedyMesher"),
    ("ShaderManager", "UShaderManager"),
    ("ChunkMeshCache", "UChunkMeshCache"),
    ("MainMenuScreen", "UMainMenuScreen"),
    ("OverworldPipeline", "UOverworldPipeline"),
    ("SettingsScreen", "USettingsScreen"),
    ("CommandRegistry", "UCommandRegistry"),
    ("LoadWorldScreen", "ULoadWorldScreen"),
    ("PrefabIconCache", "UPrefabIconCache"),
    ("AnimationClock", "UAnimationClock"),
    ("BlockRegistry", "UBlockRegistry"),
    ("ChunkStreamer", "UChunkStreamer"),
    ("ChunkManager", "UChunkManager"),
    ("ConsoleScreen", "UConsoleScreen"),
    ("NewWorldScreen", "UNewWorldScreen"),
    ("CreatureVisualFactory", "UCreatureVisualFactory"),
    ("CreatureAppearance", "UCreatureAppearance"),
    ("GuiInputRouter", "UGuiInputRouter"),
    ("GuiScreenBase", "UGuiScreenBase"),
    ("GuiScrollView", "UGuiScrollView"),
    ("GuiTextInput", "UGuiTextInput"),
    ("GuiDialogFrame", "UGuiDialogFrame"),
    ("GuiPopupMenu", "UGuiPopupMenu"),
    ("TextureCubeStorage", "UTextureCubeStorage"),
    ("TextureBaseStorage", "UTextureBaseStorage"),
    ("LegacyHashPipeline", "ULegacyHashPipeline"),
    ("ObjectPrototype", "UObjectPrototype"),
    ("ObjectStorage", "UObjectStorage"),
    ("WorldGenerator", "UWorldGenerator"),
    ("CreatureVisual", "UCreatureVisual"),
    ("GameSession", "UGameSession"),
    ("GuiCheckbox", "UGuiCheckbox"),
    ("GuiListView", "UGuiListView"),
    ("GuiRenderer", "UGuiRenderer"),
    ("GuiTabBar", "UGuiTabBar"),
    ("GuiButton", "UGuiButton"),
    ("GuiContext", "UGuiContext"),
    ("GuiIconSource", "UGuiIconSource"),
    ("PrefabLibrary", "UPrefabLibrary"),
    ("FlatPipeline", "UFlatPipeline"),
    ("BiomeSampler", "UBiomeSampler"),
    ("GlStateScope", "UGlStateScope"),
    ("GuiLayout", "UGuiLayout"),
    ("GuiPanel", "UGuiPanel"),
    ("GuiLabel", "UGuiLabel"),
    ("GuiWindow", "UGuiWindow"),
    ("GuiWidget", "UGuiWidget"),
    ("GuiSlot", "UGuiSlot"),
    ("UiQuadBatch", "UGuiQuadBatch"),
    ("ShaderProgram", "UShaderProgram"),
    ("BlockWorld", "UBlockWorld"),
    ("TextureCube", "UTextureCube"),
    ("TextureBase", "UTextureBase"),
    ("BlockRaycast", "UBlockRaycast"),
    ("PrefabUtil", "UPrefabUtil"),
    ("BlockWorld", "UBlockWorld"),
    ("Creature", "UCreature"),
    ("Camera", "UCamera"),
    ("Player", "UPlayer"),
    ("Chunk", "UChunk"),
    ("CubeGL", "UCubeGL"),
    ("Object", "UObject"),
    ("Cube", "UCube"),
    ("User", "UUser"),
    # ObjectImplementation subclasses
    ("TerrainPlane", "UTerrainPlane"),
    ("SingleCube", "USingleCube"),
    ("Terrain", "UTerrain"),
    ("Person", "UPerson"),
    ("Rect", "URect"),
]

# Members: old_suffix_name -> PascalCase (applied as word-boundary identifier)
MEMBER_RENAMES = [
    ("proceduralSettings_", "ProceduralSettings"),
    ("worldSeed_", "WorldSeed"),
    ("terrainType_", "TerrainType"),
    ("renderDistanceChunks_", "RenderDistanceChunks"),
    ("streamingEnabled_", "StreamingEnabled"),
    ("stepUpEnabled_", "StepUpEnabled"),
    ("entityCollisionEnabled_", "EntityCollisionEnabled"),
    ("renderSettings_", "RenderSettings"),
    ("uiSettings_", "UiSettings"),
    ("exeDir_", "ExeDir"),
    ("prefabs_path_", "PrefabsPath"),
    ("activeWorldFolder_", "ActiveWorldFolder"),
    ("configFilePath_", "ConfigFilePath"),
    ("texture_base_storage_file_name", "TextureBaseStorageFileName"),
    ("texture_cube_storage_file_name", "TextureCubeStorageFileName"),
    ("object_storage_file_name", "ObjectStorageFileName"),
    ("default_world_name", "DefaultWorldName"),
    ("default_user_name", "DefaultUserName"),
    ("blockWorld_", "BlockWorld"),
    ("blockRegistry_", "BlockRegistry"),
    ("blockDefinitions_", "BlockDefinitions"),
    ("blockWorldReady_", "BlockWorldReady"),
    ("worldGen_", "WorldGen"),
    ("cachedBlockCount_", "CachedBlockCount"),
    ("physicsSuspendFrames_", "PhysicsSuspendFrames"),
    ("allowProceduralFill_", "AllowProceduralFill"),
    ("hasPersistedSave_", "HasPersistedSave"),
    ("loadedFromChunkSave_", "LoadedFromChunkSave"),
    ("creatures_", "Creatures"),
    ("nextCreatureId_", "NextCreatureId"),
    ("playerCreatureId_", "PlayerCreatureId"),
    ("controlledCreatureId_", "ControlledCreatureId"),
    ("creatureDefinitions_", "CreatureDefinitions"),
    ("skinDefinitions_", "SkinDefinitions"),
    ("activityDirector_", "ActivityDirector"),
    ("posePresenterRegistry_", "PosePresenterRegistry"),
    ("prefabLibrary_", "PrefabLibrary"),
    ("meshCache_", "MeshCache"),
    ("streamer_", "Streamer"),
    ("modifiedChunks_", "ModifiedChunks"),
    ("worldFolderPath_", "WorldFolderPath"),
    ("hasIntersectionBlock_", "HasIntersectionBlock"),
    ("intersectionBlockPos_", "IntersectionBlockPos"),
    ("hasPlaceTarget_", "HasPlaceTarget"),
    ("placeBlockPos_", "PlaceBlockPos"),
    ("breakSession_", "BreakSession"),
    ("movementDiagnostics_", "MovementDiagnostics"),
    ("lastPlayerY_", "LastPlayerY"),
    ("hasLastPlayerY_", "HasLastPlayerY"),
    ("core_", "Core"),
    ("world_", "World"),
    ("geometry_", "Geometry"),
    ("views_", "Views"),
    ("textRenderer_", "TextRenderer"),
    ("shaderManager_", "ShaderManager"),
    ("blockDefinitions_", "BlockDefinitions"),
    ("guiContext_", "GuiContext"),
    ("gameSession_", "GameSession"),
    ("state_", "State"),
    ("window_", "Window"),
    ("consoleOpen_", "ConsoleOpen"),
    ("paletteOpen_", "PaletteOpen"),
    ("freeCursor_", "FreeCursor"),
    ("suppressConsoleToggleChar_", "SuppressConsoleToggleChar"),
    ("overlayPointerCapture_", "OverlayPointerCapture"),
    ("dragCursorX_", "DragCursorX"),
    ("dragCursorY_", "DragCursorY"),
    ("worldSessionActive_", "WorldSessionActive"),
    ("pendingEnterGame_", "PendingEnterGame"),
    ("pendingQuit_", "PendingQuit"),
    ("pendingMenuAction_", "PendingMenuAction"),
    ("quitRequested_", "QuitRequested"),
    ("iconSource_", "IconSource"),
    ("hudScreen_", "HudScreen"),
    ("consoleScreen_", "ConsoleScreen"),
    ("paletteScreen_", "PaletteScreen"),
    ("clipboard_", "Clipboard"),
    ("overlayPopup_", "OverlayPopup"),
    ("menuSubview_", "MenuSubview"),
    ("mainMenuScreen_", "MainMenuScreen"),
    ("rightLookActive_", "RightLookActive"),
    ("leftDownTime_", "LeftDownTime"),
    ("leftHeld_", "LeftHeld"),
    ("rightDownPos_", "RightDownPos"),
    ("rightPressed_", "RightPressed"),
    ("rightDragExceeded_", "RightDragExceeded"),
    ("glfwWindow", "GlfwWindow"),
    ("keyStates", "KeyStates"),
    ("mousePosition", "MousePosition"),
    ("mouseDelta", "MouseDelta"),
    ("mouseScroll", "MouseScroll"),
    ("keyCallback", "KeyCallback"),
    ("mouseButtonCallback", "MouseButtonCallback"),
    ("mouseMoveCallback", "MouseMoveCallback"),
    ("windowResizeCallback", "WindowResizeCallback"),
    ("mouseScrollCallback", "MouseScrollCallback"),
    ("textShader", "TextShader"),
    ("windowWidth", "WindowWidth"),
    ("windowHeight", "WindowHeight"),
    ("verboseLogging", "VerboseLogging"),
    ("showHud", "ShowHud"),
    ("showPerformance", "ShowPerformance"),
    ("showCrosshair", "ShowCrosshair"),
    ("WorldInstance", "WorldInstance"),  # noop guard
]

# Struct field renames (lowercase -> PascalCase)
STRUCT_FIELD_RENAMES = [
    (".world", ".World"),
    (".geometries", ".Geometries"),
    (".ui", ".Ui"),
    (".window", ".Window"),
    (".app", ".App"),
    ("ctx.world", "ctx.World"),
    ("ctx.geometries", "ctx.Geometries"),
    ("ctx.ui", "ctx.Ui"),
    ("ctx.window", "ctx.Window"),
    ("ctx.app", "ctx.App"),
    ("world:", "World:"),
    ("geometries:", "Geometries:"),
    ("ui:", "Ui:"),
    ("window:", "Window:"),
    ("app:", "App:"),
]


def rename_class(text: str, old: str, new: str) -> str:
    if old == new or f"class {new}" in text:
        return text
    # Skip if already prefixed
    pattern = re.compile(
        r"(?<![A-Za-z0-9_])" + re.escape(old) + r"(?![A-Za-z0-9_])"
    )
    return pattern.sub(new, text)


def rename_member(text: str, old: str, new: str) -> str:
    if old == new:
        return text
    pattern = re.compile(r"\b" + re.escape(old) + r"\b")
    return pattern.sub(new, text)


def process_file(path: Path, do_classes: bool, do_members: bool, do_struct: bool) -> bool:
    if path.name in SKIP_FILES:
        return False
    try:
        original = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return False
    text = original

    if do_classes:
        for old, new in CLASS_RENAMES:
            if old.startswith("U") and old in text:
                continue
            text = rename_class(text, old, new)

    if do_members:
        # longest members first
        members = sorted(MEMBER_RENAMES, key=lambda x: len(x[0]), reverse=True)
        for old, new in members:
            if old.endswith("_") or old != new:
                text = rename_member(text, old, new)

    if do_struct:
        for old, new in STRUCT_FIELD_RENAMES:
            text = text.replace(old, new)

    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")
        return True
    return False


def main():
    do_classes = "--classes" in sys.argv or len(sys.argv) == 1
    do_members = "--members" in sys.argv or len(sys.argv) == 1
    do_struct = "--struct" in sys.argv or len(sys.argv) == 1

    changed = []
    for path in sorted(ROOT.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        if process_file(path, do_classes, do_members, do_struct):
            changed.append(str(path.relative_to(ROOT.parent)))

    print(f"Updated {len(changed)} files")
    for p in changed[:50]:
        print(f"  {p}")
    if len(changed) > 50:
        print(f"  ... and {len(changed) - 50} more")


if __name__ == "__main__":
    main()
