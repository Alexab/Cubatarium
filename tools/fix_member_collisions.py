#!/usr/bin/env python3
"""Fix member/struct field renames after type/member collisions."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

REPLACEMENTS = [
    # Core/Application members (avoid replacing type names in declarations carefully)
    ("ProceduralSettings = ProceduralSettings", "ProceduralTemplate = ProceduralSettings"),
    ("ProceduralSettings.", "ProceduralTemplate."),
    ("ProceduralSettings =", "ProceduralTemplate ="),
    ("return ProceduralSettings;", "return ProceduralTemplate;"),
    ("ResolveProceduralDefaults(ProceduralSettings)", "ResolveProceduralDefaults(ProceduralTemplate)"),
    ("ApplyGeneratorTierDefaults(ProceduralSettings)", "ApplyGeneratorTierDefaults(ProceduralTemplate)"),
    ("RenderSettings = RenderSettings", "Render = RenderSettings"),
    ("RenderSettings.", "Render."),
    ("RenderSettings =", "Render ="),
    ("return RenderSettings;", "return Render;"),
    ("SetRenderSettings(RenderSettings)", "SetRenderSettings(Render)"),
    ("UiSettings.", "Ui."),
    ("UiSettings =", "Ui ="),
    ("return UiSettings;", "return Ui;"),
    # AppSettingsSnapshot fields
    ("settings.defaultUser", "settings.DefaultUser"),
    ("settings.defaultWorld", "settings.DefaultWorld"),
    ("settings.renderDistanceChunks", "settings.RenderDistanceChunks"),
    ("settings.streamingEnabled", "settings.StreamingEnabled"),
    ("settings.stepUpEnabled", "settings.StepUpEnabled"),
    ("settings.entityCollisionEnabled", "settings.EntityCollisionEnabled"),
    ("settings.render", "settings.Render"),
    ("snapshot.defaultUser", "snapshot.DefaultUser"),
    ("snapshot.defaultWorld", "snapshot.DefaultWorld"),
    ("snapshot.renderDistanceChunks", "snapshot.RenderDistanceChunks"),
    ("snapshot.streamingEnabled", "snapshot.StreamingEnabled"),
    ("snapshot.stepUpEnabled", "snapshot.StepUpEnabled"),
    ("snapshot.entityCollisionEnabled", "snapshot.EntityCollisionEnabled"),
    ("snapshot.render", "snapshot.Render"),
    # BlockInputContext
    ("ctx.world", "ctx.World"),
    ("ctx.geometries", "ctx.Geometries"),
    ("ctx.ui", "ctx.Ui"),
    ("ctx.window", "ctx.Window"),
    ("ctx.app", "ctx.App"),
]


def main():
    n = 0
    for fp in ROOT.rglob("*"):
        if fp.suffix not in (".h", ".cpp"):
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text
        for old, new in REPLACEMENTS:
            text = text.replace(old, new)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(f"Updated {n} files")


if __name__ == "__main__":
    main()
