#!/usr/bin/env python3
"""M4 audit: grep parallel MarkDirty from emerge Admit paths vs ColumnFlow enqueue."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EMERGE = ROOT / "src" / "World" / "Streaming" / "ChunkEmergeCoordinator.cpp"

MARK = len(re.findall(r"mesh_service\.MarkDirty", EMERGE.read_text(encoding="utf-8")))
FM_ENQ = len(re.findall(r"ColumnWorkKind::FirstMesh", EMERGE.read_text(encoding="utf-8")))
GUARD = "BlockParallelMarkDirtyForColumnFlow" in EMERGE.read_text(encoding="utf-8")

print(f"ChunkEmergeCoordinator MarkDirty calls: {MARK}")
print(f"ColumnFlow FirstMesh enqueues: {FM_ENQ}")
print(f"ColumnFlowMeshOwnership guard wired: {GUARD}")
sys.exit(0 if GUARD else 1)
