#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

CTX_REPLACEMENTS = [
    ("ctx.world", "ctx.World"),
    ("ctx.registry", "ctx.Registry"),
    ("ctx.settings", "ctx.Settings"),
    ("ctx.prefabs", "ctx.Prefabs"),
    ("ctx.meshCache", "ctx.MeshCache"),
    ("ctx.bedrock", "ctx.Bedrock"),
    ("ctx.stone", "ctx.Stone"),
    ("ctx.dirt", "ctx.Dirt"),
    ("ctx.grass", "ctx.Grass"),
    ("ctx.sand", "ctx.Sand"),
    ("ctx.sandstone", "ctx.Sandstone"),
    ("ctx.wood", "ctx.Wood"),
    ("ctx.gravel", "ctx.Gravel"),
    ("ctx.snow", "ctx.Snow"),
    ("ctx.clay", "ctx.Clay"),
    ("ctx.ice", "ctx.Ice"),
    ("ctx.hellrock", "ctx.Hellrock"),
    ("ctx.water", "ctx.Water"),
    ("ctx.lava", "ctx.Lava"),
    ("ctx.fire", "ctx.Fire"),
]

OTHER = [
    ("ProceduralSettings,", "ProceduralTemplate,"),
    ("ProceduralSettings}", "ProceduralTemplate}"),
    ("WriteProceduralSettings(world_data, ProceduralSettings)", "WriteProceduralSettings(world_data, ProceduralTemplate)"),
    ("MovementDiagnostics = MovementDiagnostics{}", "MovementDiag = MovementDiagnostics{}"),
    ("return MovementDiagnostics;", "return MovementDiag;"),
    ("MovementDiagnostics.", "MovementDiag."),
    ("GetMovementDiagnostics() const { return MovementDiagnostics; }", "GetMovementDiagnostics() const { return MovementDiag; }"),
    ("MovementDiagnostics MovementDiagnostics;", "MovementDiagnostics MovementDiag;"),
]


def main():
    n = 0
    for fp in ROOT.rglob("*"):
        if fp.suffix not in (".h", ".cpp"):
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text
        for a, b in CTX_REPLACEMENTS + OTHER:
            text = text.replace(a, b)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(n)


if __name__ == "__main__":
    main()
