#!/usr/bin/env python3
"""Module boundary include rules (World must not include Render headers).

Allowlist (temporary — remove entries as refactors land):
  .h legacy headers that still pull Render types (M15).
  .cpp World→Render includes pending adapter extraction (M15).
New World→Render includes outside World/Mesh adapter and allowlist fail CI.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
WORLD_SRC = REPO_ROOT / "src" / "World"
MESH_ADAPTER = WORLD_SRC / "Mesh"
INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')

# Legacy .h violations; remove as refactor PRs land.
ALLOWLIST_H: set[tuple[str, str]] = {
    ("src/World/Core/BlockWorld.h", "Render/Mesh/ChunkMeshSnapshot.h"),
    ("src/World/Core/BlockWorld.h", "Render/Mesh/MeshCaptureToken.h"),
    ("src/World/Lighting/LightingSeedBackendFactory.h", "Render/Backend/RenderBackendCaps.h"),
    ("src/World/View/WorldViewSettings.h", "Render/Camera/IsoViewPreset.h"),
}

# Legacy .cpp violations; remove as adapter/facade PRs land.
ALLOWLIST_CPP: set[tuple[str, str]] = {
    ("src/World/Collision/WorldCollision.cpp", "Render/Primitives/Cube.h"),
    ("src/World/Core/MarkRelitInstall.cpp", "Render/Mesh/MeshCaptureWorker.h"),
    ("src/World/Core/World.cpp", "Render/Backend/RenderBackendCaps.h"),
    ("src/World/Core/World.cpp", "Render/Backend/RenderBackendFactory.h"),
    ("src/World/Core/World.cpp", "Render/Camera/Camera.h"),
    ("src/World/Core/World.cpp", "Render/Mesh/MeshApplyPolicy.h"),
    ("src/World/Diagnostics/BlockInspectDiagnostics.cpp", "Render/Camera/Camera.h"),
    ("src/World/Diagnostics/BlockInspectDiagnostics.cpp", "Render/Engine/GeometryEngine.h"),
    ("src/World/Diagnostics/EnterLitDiagnostics.cpp", "Render/Camera/Camera.h"),
    ("src/World/Diagnostics/FramePerfMonitor.cpp", "Render/Backend/GpuHotPathFallback.h"),
    ("src/World/Diagnostics/FramePerfMonitor.cpp", "Render/Engine/MdiVertexPoolStore.h"),
    ("src/World/Diagnostics/FramePerfMonitor.cpp", "Render/Mesh/GpuFluidColumnScan.h"),
    ("src/World/Diagnostics/FramePerfMonitor.cpp", "Render/Mesh/GpuGreedyMesher.h"),
    ("src/World/Diagnostics/FramePerfMonitor.cpp", "Render/Mesh/GpuGreedyOpaqueEmit.h"),
    ("src/World/Diagnostics/FramePerfMonitor.cpp", "Render/Pipeline/GpuTransparentSort.h"),
    ("src/World/Diagnostics/MovementDiagnosticsRecorder.cpp", "Render/Camera/Camera.h"),
    ("src/World/Diagnostics/MovementDiagnosticsRecorder.cpp", "Render/Engine/ViewEngine.h"),
    ("src/World/Environment/WorldEnvironment.cpp", "Render/Camera/Camera.h"),
    ("src/World/Environment/WorldEnvironment.cpp", "Render/Primitives/Cube.h"),
    ("src/World/Interaction/BlockPlacementService.cpp", "Render/Camera/Camera.h"),
    ("src/World/Lighting/GpuBlocklightFlood.cpp", "Render/GlIncludes.h"),
    ("src/World/Lighting/GpuSkylightColumnSeed.cpp", "Render/GlIncludes.h"),
    ("src/World/Mesh/WorldMeshService.cpp", "Render/Camera/Camera.h"),
    ("src/World/Mesh/WorldMeshService.cpp", "Render/Camera/Frustum.h"),
    ("src/World/Persistence/WorldPersistence.cpp", "Render/Camera/Camera.h"),
    ("src/World/Streaming/ChunkEmergeCoordinator.cpp", "Render/Camera/Camera.h"),
    ("src/World/Streaming/ChunkEmergeCoordinator.cpp", "Render/Mesh/GpuMeshPipeline.h"),
    ("src/World/Streaming/ChunkEmergeCoordinator.cpp", "Render/Mesh/MeshApplyPolicy.h"),
    ("src/World/Streaming/ChunkEmergeCoordinator.cpp", "Render/Mesh/MeshCaptureWorker.h"),
    ("src/World/Streaming/ColumnFlowExecutor.cpp", "Render/Camera/Camera.h"),
    ("src/World/Streaming/WorldStreaming.cpp", "Render/Backend/RenderBackendCaps.h"),
    ("src/World/Streaming/WorldStreaming.cpp", "Render/Camera/Camera.h"),
    ("src/World/Streaming/WorldStreaming.cpp", "Render/Mesh/MeshApplyPolicy.h"),
    ("src/World/View/WorldViewSettings.cpp", "Render/Camera/IsoViewPreset.h"),
    ("src/World/Core/WorldViewBinding.cpp", "Render/Camera/Camera.h"),
    ("src/World/Core/WorldViewBinding.cpp", "Render/Engine/ViewEngine.h"),
}


def is_mesh_adapter(rel_posix: str) -> bool:
    return rel_posix.startswith("src/World/Mesh/")


def scan_world_render_includes() -> list[dict[str, str | int]]:
    violations: list[dict[str, str | int]] = []
    for fp in sorted(WORLD_SRC.rglob("*")):
        if fp.suffix not in (".h", ".cpp"):
            continue
        rel = fp.relative_to(REPO_ROOT).as_posix()
        if is_mesh_adapter(rel):
            continue
        allowlist = ALLOWLIST_H if fp.suffix == ".h" else ALLOWLIST_CPP
        for line_no, line in enumerate(
            fp.read_text(encoding="utf-8", errors="ignore").splitlines(), start=1
        ):
            match = INCLUDE_RE.search(line)
            if not match:
                continue
            inc = match.group(1)
            if not inc.startswith("Render/"):
                continue
            key = (rel, inc)
            if key in allowlist:
                continue
            violations.append(
                {
                    "file": rel,
                    "line": line_no,
                    "include": inc,
                    "kind": fp.suffix,
                }
            )
    return violations


def main() -> int:
    violations = scan_world_render_includes()
    if violations:
        print(f"check_include_rules: {len(violations)} NEW violation(s) (World -> Render):")
        for v in violations:
            print(
                f"  {v['file']}:{v['line']}: #include \"{v['include']}\" ({v['kind']})"
            )
        return 1
    allow_count = len(ALLOWLIST_H) + len(ALLOWLIST_CPP)
    print(
        f"check_include_rules: ok (World/Mesh adapter exempt; "
        f"{allow_count} allowlisted legacy include(s))"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
