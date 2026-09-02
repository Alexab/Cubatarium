#!/usr/bin/env python3
"""M4 ownership lint: FirstMesh MarkDirty should flow via ColumnFlow (grep audit)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "src"

# Direct MarkDirtyPriority outside mesh service / cache is suspicious on hot paths.
FORBIDDEN = [
    (re.compile(r"MarkDirtyPriority\s*\("), "MarkDirtyPriority direct call"),
]

ALLOWLIST = {
    "WorldMeshService.cpp",
    "ChunkMeshCache.cpp",
    "ChunkDirtySet.cpp",
    "World.cpp",  # dig/edit immediate paths reviewed separately
    "MarkRelitInstall.cpp",
    "ChunkEmergeCoordinator.cpp",
    "WorldChunkDirtyService.cpp",
    "PlayerRelightMeshBurst.cpp",
}

violations: list[str] = []
for path in SRC.rglob("*.cpp"):
    if path.name in ALLOWLIST:
        continue
    text = path.read_text(encoding="utf-8", errors="replace")
    for pat, label in FORBIDDEN:
        if pat.search(text):
            violations.append(f"{path.relative_to(ROOT)}: {label}")

if violations:
    print("M4 ownership lint FAIL:")
    for v in violations[:40]:
        print(" ", v)
    if len(violations) > 40:
        print(f"  ... and {len(violations) - 40} more")
    sys.exit(1)

print("M4 ownership lint PASS")
sys.exit(0)
