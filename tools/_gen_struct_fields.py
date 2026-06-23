#!/usr/bin/env python3
"""Generate struct_field_map.json from struct field declarations."""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"
OUT = Path(__file__).resolve().parent / "struct_field_map.json"

SKIP_NAMES = {
    "namespace", "class", "struct", "enum", "return", "using", "template",
    "public", "private", "protected", "virtual", "override", "const", "static",
    "inline", "friend", "typedef", "if", "for", "while", "switch", "case",
    "break", "continue", "default", "else", "catch", "throw", "new", "delete",
    "true", "false", "nullptr", "operator", "explicit", "noexcept", "mutable",
    "volatile", "extern", "alignas", "alignof", "sizeof", "typeid", "goto",
    "do", "try", "this", "auto", "void", "int", "float", "double", "bool",
    "char", "short", "long", "unsigned", "signed", "size_t", "uint32_t",
    "uint64_t", "int32_t", "int64_t", "uint8_t", "glm", "std", "cutum",
}

# Short names that must not be globally renamed (glm vec components, etc.)
SKIP_FIELDS = {"x", "y", "z", "w", "h", "r", "g", "b", "a"}

EXTRA = [
    # GuiRect / events (x,y,w,h handled separately)
    ("button", "Button"), ("pressed", "Pressed"), ("pointerId", "PointerId"),
    ("keyCode", "KeyCode"), ("action", "Action"), ("mods", "Mods"),
    ("codepoint", "Codepoint"), ("xoffset", "Xoffset"), ("yoffset", "Yoffset"),
    # GuiTheme
    ("panelBackground", "PanelBackground"), ("panelBorder", "PanelBorder"),
    ("buttonNormal", "ButtonNormal"), ("buttonHover", "ButtonHover"),
    ("buttonPressed", "ButtonPressed"), ("buttonDisabled", "ButtonDisabled"),
    ("textPrimary", "TextPrimary"), ("textSecondary", "TextSecondary"),
    ("slotBackground", "SlotBackground"), ("tooltipBackground", "TooltipBackground"),
    ("slotSelected", "SlotSelected"), ("slotSelectedFill", "SlotSelectedFill"),
    ("slotSelectedInner", "SlotSelectedInner"), ("focusRing", "FocusRing"),
    ("focusRingThickness", "FocusRingThickness"),
    ("slotSelectedBorderThickness", "SlotSelectedBorderThickness"),
    ("fontSizeBody", "FontSizeBody"), ("padding", "Padding"),
    ("hotbarSlotSize", "HotbarSlotSize"), ("hotbarSlotGap", "HotbarSlotGap"),
    ("borderThickness", "BorderThickness"),
    # UiSettings / RenderSettings
    ("legacyHud", "LegacyHud"), ("showPerformance", "ShowPerformance"),
    ("consoleKey", "ConsoleKey"), ("paletteKey", "PaletteKey"),
    ("inventoryKey", "InventoryKey"), ("hotbarCount", "HotbarCount"),
    ("controlScheme", "ControlScheme"),
    ("placeClickMaxSeconds", "PlaceClickMaxSeconds"),
    ("breakHoldMinSeconds", "BreakHoldMinSeconds"),
    ("breakDurationSeconds", "BreakDurationSeconds"),
    ("rmbDragThresholdPx", "RmbDragThresholdPx"),
    ("greedyMeshing", "GreedyMeshing"), ("faceQuads", "FaceQuads"),
    ("frustumCulling", "FrustumCulling"), ("batchCache", "BatchCache"),
    ("creatureDebugBounds", "CreatureDebugBounds"),
    ("creatureTexturedParts", "CreatureTexturedParts"),
    ("creatureWireframeOverlay", "CreatureWireframeOverlay"),
    # ProceduralSettings
    ("generator", "Generator"), ("vertical", "Vertical"), ("seed", "Seed"),
    ("seaLevel", "SeaLevel"), ("maxHeight", "MaxHeight"),
    ("bedrockTopY", "BedrockTopY"), ("enableCaves", "EnableCaves"),
    ("enableTrees", "EnableTrees"), ("flatSurfaceY", "FlatSurfaceY"),
    ("fillWater", "FillWater"), ("fillLava", "FillLava"), ("fillFire", "FillFire"),
    # BlockDefinition
    ("frameCount", "FrameCount"), ("frametimeTicks", "FrametimeTicks"),
    ("interpolate", "Interpolate"), ("occupancy", "Occupancy"),
    ("dragHorizontal", "DragHorizontal"), ("dragVertical", "DragVertical"),
    ("sinkSpeed", "SinkSpeed"), ("riseSpeed", "RiseSpeed"),
    ("damageOnContact", "DamageOnContact"), ("movement", "Movement"),
    ("transparent", "Transparent"), ("doubleSided", "DoubleSided"),
    ("style", "Style"), ("fluidView", "FluidView"),
    ("fogColor", "FogColor"), ("fogStart", "FogStart"), ("fogEnd", "FogEnd"),
    ("fogMinBlend", "FogMinBlend"), ("overlayColor", "OverlayColor"),
    ("overlayAlpha", "OverlayAlpha"), ("animation", "Animation"),
    ("physics", "Physics"), ("render", "Render"), ("types", "Types"),
    ("name", "Name"), ("id", "Id"),
    # Touch
    ("active", "Active"), ("gamePointer", "GamePointer"),
    ("tapCandidate", "TapCandidate"), ("lookZoneTouch", "LookZoneTouch"),
    ("lookDrag", "LookDrag"), ("startPos", "StartPos"),
    ("lastLookPos", "LastLookPos"), ("downTime", "DownTime"),
    # Creature locomotion
    ("moveForward", "MoveForward"), ("moveBack", "MoveBack"),
    ("moveLeft", "MoveLeft"), ("moveRight", "MoveRight"),
    ("jump", "Jump"), ("sneak", "Sneak"), ("sprint", "Sprint"),
    ("lookYawDelta", "LookYawDelta"), ("lookPitchDelta", "LookPitchDelta"),
]

fields = {}
for old, new in EXTRA:
    if old != new:
        fields[old] = new

for fp in ROOT.rglob("*.h"):
    if "ThirdParty" in str(fp):
        continue
    text = fp.read_text(encoding="utf-8", errors="ignore")
    in_struct = False
    brace = 0
    for line in text.splitlines():
        stripped = line.strip()
        if re.match(r"struct\s+\w+", stripped):
            in_struct = True
            brace = stripped.count("{") - stripped.count("}")
            continue
        if in_struct:
            brace += stripped.count("{") - stripped.count("}")
            if brace <= 0:
                in_struct = False
                continue
            m = re.match(r"([a-z][a-zA-Z0-9]*)\s*[\[{;]", stripped)
            if m:
                name = m.group(1)
                if name not in SKIP_NAMES and name not in SKIP_FIELDS:
                    pascal = name[0].upper() + name[1:]
                    if name != pascal:
                        fields[name] = pascal

pairs = sorted((o, n) for o, n in fields.items())
OUT.write_text(json.dumps({"fields": pairs}, indent=2), encoding="utf-8")
print(f"Wrote {len(pairs)} field mappings to {OUT}")
