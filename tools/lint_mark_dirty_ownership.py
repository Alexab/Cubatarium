#!/usr/bin/env python3
"""Lint: MarkDirty outside ColumnFlow + commit paths (FP-C1 smoke)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

ALLOWED = {
    "src/World/Core/MarkRelitInstall.cpp",
    "src/World/Core/World.cpp",
    "src/World/Core/WorldFluidFacade.cpp",
    "src/Render/Mesh/ChunkMeshCache.cpp",
    "src/Render/Mesh/ChunkDirtySet.cpp",
    "src/World/Streaming/ColumnFlowExecutor.cpp",
    "src/World/Streaming/ChunkEmergeCoordinator.cpp",
    "src/World/Streaming/WorldStreaming.cpp",
    "src/World/Streaming/ChunkEmergeCoordinator.cpp",
    "src/World/Mesh/WorldMeshService.cpp",
    "src/World/Chunks/ChunkLoadScheduler.cpp",
    "src/World/Physics/IUChunkDirtyService.cpp",
    "src/World/Physics/WorldChunkDirtyService.cpp",
}

PATTERN = re.compile(r"\bMarkDirty(?:Priority)?\s*\(")


def main() -> int:
    violations: list[str] = []
    for path in SRC.rglob("*.cpp"):
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith("src/Test/"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if not PATTERN.search(text):
            continue
        if rel not in ALLOWED:
            violations.append(rel)
    if violations:
        print("MarkDirty ownership violations:")
        for v in sorted(violations):
            print(f"  {v}")
        return 1
    print("MarkDirty ownership lint OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
