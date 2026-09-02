#!/usr/bin/env python3
"""M4 ownership lint: FirstMesh MarkDirty should flow via ColumnFlow (grep audit)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "src"

FORBIDDEN = [
    (re.compile(r"MarkDirtyPriority\s*\("), "MarkDirtyPriority direct call"),
    (
        re.compile(r"MarkMissingSlicesDirtyPriority\s*\("),
        "MarkMissingSlicesDirtyPriority direct call",
    ),
]

ALLOWLIST = {
    "WorldMeshService.cpp",
    "ChunkMeshCache.cpp",
    "ChunkDirtySet.cpp",
    "World.cpp",
    "MarkRelitInstall.cpp",
    "ChunkEmergeCoordinator.cpp",
    "WorldChunkDirtyService.cpp",
    "PlayerRelightMeshBurst.cpp",
    "ColumnFlowExecutor.cpp",
}

TEST_ALLOWLIST = {
    "MeshScheduleRetryTest.cpp",
}

MARK_MISSING_ALLOWLIST = {
    "WorldMeshService.cpp",
    "ChunkMeshCache.cpp",
    "ColumnFlowExecutor.cpp",
    "World.cpp",  # dig/edit immediate paths reviewed separately
}

violations: list[str] = []
mark_missing_violations: list[str] = []
warn_mark_dirty: list[str] = []

for path in SRC.rglob("*.cpp"):
    text = path.read_text(encoding="utf-8", errors="replace")
    rel = path.relative_to(ROOT)
    if path.name not in ALLOWLIST and path.name not in TEST_ALLOWLIST:
        for pat, label in FORBIDDEN:
            if pat.pattern.startswith("MarkDirtyPriority") and pat.search(text):
                violations.append(f"{rel}: {label}")
    if path.name not in MARK_MISSING_ALLOWLIST:
        if re.search(r"MarkMissingSlicesDirtyPriority\s*\(", text):
            mark_missing_violations.append(
                f"{rel}: MarkMissingSlicesDirtyPriority outside ColumnFlow owner"
            )
    if path.name == "ChunkEmergeCoordinator.cpp":
        if re.search(r"mesh_service\.MarkDirty\s*\(", text):
            if "BlockParallelMarkDirtyForColumnFlow" not in text:
                warn_mark_dirty.append(
                    f"{rel}: mesh_service.MarkDirty without ColumnFlow guard"
                )

if violations or mark_missing_violations:
    print("M4 ownership lint FAIL:")
    for v in violations + mark_missing_violations:
        print(" ", v)
    sys.exit(1)

if warn_mark_dirty:
    print("M4 ownership lint WARN:")
    for w in warn_mark_dirty[:10]:
        print(" ", w)

print("M4 ownership lint PASS")
sys.exit(0)
