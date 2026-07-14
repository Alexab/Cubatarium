#!/usr/bin/env python3
"""Heuristic performance hints (no profiling)."""

from __future__ import annotations

import re
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

SRC = REPO_ROOT / "src"
HOT_FILES = [
    "src/Render/Mesh/GreedyMesher.cpp",
    "src/Render/Mesh/ChunkMeshCache.cpp",
    "src/World/Core/World.cpp",
    "src/WorldGen/Sampling/BiomeSampler.cpp",
]

PUSH_BACK_RE = re.compile(r"\.push_back\s*\(")
RESERVE_RE = re.compile(r"\.reserve\s*\(")
REBUILD_RE = re.compile(r"RebuildChunkImmediate")
MARK_DIRTY_RE = re.compile(r"MarkDirty")


def scan_push_back_without_reserve(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    rel = path.relative_to(REPO_ROOT).as_posix()
    hints: list[dict] = []
    lines = text.splitlines()
    for i, line in enumerate(lines):
        if not PUSH_BACK_RE.search(line):
            continue
        window = "\n".join(lines[max(0, i - 25) : i + 1])
        if RESERVE_RE.search(window):
            continue
        if "for " in window or "while " in window:
            hints.append(
                {
                    "file": rel,
                    "line": i + 1,
                    "hint": "push_back in loop without nearby reserve()",
                    "category": "performance",
                }
            )
    return hints


def scan_dual_mesh_path(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    rel = path.relative_to(REPO_ROOT).as_posix()
    if REBUILD_RE.search(text) and MARK_DIRTY_RE.search(text):
        return [
            {
                "file": rel,
                "line": 0,
                "hint": "both RebuildChunkImmediate and MarkDirty paths present",
                "category": "performance",
            }
        ]
    return []


def main() -> int:
    ensure_audit_dir()
    hints: list[dict] = []
    targets = [REPO_ROOT / p for p in HOT_FILES if (REPO_ROOT / p).exists()]
    for fp in targets:
        hints.extend(scan_push_back_without_reserve(fp))
        hints.extend(scan_dual_mesh_path(fp))

    out = {
        "generated_at": utc_now_iso(),
        "count": len(hints),
        "hints": hints[:200],
    }
    write_json(REPO_ROOT / "audit" / "perf_hints.json", out)
    print(f"perf_hints: {len(hints)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
