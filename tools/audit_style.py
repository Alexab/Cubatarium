#!/usr/bin/env python3
"""Audit coding style violations in src/. Exit 1 if any found."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

# Legacy creature/visual classes pending U-prefix rename (TD-CRE-029).
LEGACY_CLASS_FILE_PREFIXES = (
    "src/Creatures/Visual/",
    "src/Pose/BoneSkeleton/",
    "src/Gui/Layout/DockedOverlayLayout.h",
)

SKIP_INCLUDES = {
    "Version.h",
    "ThirdParty/stb_image.h",
    "stb_image.h",
    "egl_context.h",
    "android_jni.h",
    "android_soft_keyboard.h",
}

CLASS_RE = re.compile(r"^class\s+([A-Z][a-z][A-Za-z0-9]*)", re.M)
MEMBER_RE = re.compile(r"\b[a-z][a-zA-Z0-9]*_\b")
BARE_INCLUDE_RE = re.compile(r'#include\s+"([^"/\\]+\.h)"')
STRUCT_FIELD_RE = re.compile(r"^\s+([a-z][a-zA-Z0-9]*)\s*[\[{;]", re.M)
STRUCT_BLOCK_RE = re.compile(r"\bstruct\s+\w+[^{]*\{", re.M)

# Long-function gate (TD-AUD-033). Production functions must stay <= 300
# lines; functions already over the limit at gate introduction are
# grandfathered at their recorded length. Growing a grandfathered function
# beyond its recorded length, or adding a new function over the limit, fails
# the audit. Shrink a function and refresh its entry (or delete it once it
# is under the limit).
MAX_FUNCTION_LINES = 300
FUNC_SIG_RE = re.compile(r"^([A-Za-z_][\w:<>,&*\s]*?[\w:>])\s*\(")
FUNC_CTRL_RE = re.compile(r"^\s*(if|for|while|switch|catch|return|else)\b")
GRANDFATHERED_LONG_FUNCTIONS = {
    "src/World/Streaming/ChunkEmergeCoordinator.cpp::UChunkEmergeCoordinator::TickMeshEmerge": 6345,
    "src/World/Streaming/WorldStreaming.cpp::UWorldStreaming::RefreshStreamingPressure": 2673,
    "src/World/Diagnostics/FramePerfMonitor.cpp::UFramePerfMonitor::Shutdown": 2158,
    "src/World/Streaming/WorldStreaming.cpp::UWorldStreaming::TickAsyncChunkSystems": 2066,
    "src/World/Core/WorldCooperativeOps.cpp::UWorldCooperativeSession::Tick": 1625,
    "src/World/Persistence/WorldPersistence.cpp::UWorldPersistence::DrainRelightQueues": 1569,
    "src/World/Diagnostics/FramePerfMonitor.cpp::WriteJsonl": 1458,
    "src/App/Platform/AppRunner.cpp::RunFlightSim": 1410,
    "src/World/Core/World.cpp::UWorld::AdmitUnfinishedVisualDemand": 1291,
    "src/World/Diagnostics/FramePerfMonitor.cpp::Compute": 1182,
    "src/World/Core/World.cpp::UWorld::DrainAsyncRelightResults": 1051,
    "src/Render/Mesh/ChunkMeshCache.cpp::UChunkMeshCache::ApplyMeshResult": 1030,
    "src/Commands/WorldCommands.cpp::RegisterWorldCommands": 996,
    "src/World/Streaming/ChunkEmergeCoordinator.cpp::UChunkEmergeCoordinator::InstallSoftDeferCallbacks": 951,
    "src/World/Diagnostics/MeshAuditDiagnostics.cpp::UMeshAuditDiagnostics::Run": 930,
    "src/World/Core/World.cpp::UWorld::CountUnfinishedVisualNear": 907,
    "src/Render/Engine/GreedyGpuPublication.cpp::UGreedyGpuBackend::ApplyPublicationDelta": 726,
    "src/World/Core/WorldViewBinding.cpp::UWorld::TickWorldStreamingPhase": 636,
    "src/World/Core/World.cpp::UWorld::RecoverUnlitFocusMeshes": 618,
    "src/World/Streaming/MeshWorkAdmission.h::ComputeMeshWorkAdmission": 607,
    "src/WorldGen/Features/ObjectFeaturePlacer.cpp::TryPlaceProceduralTree": 538,
    "src/World/Streaming/WorldStreaming.cpp::UWorldStreaming::InitStreamerCallbacks": 498,
    "src/Render/Mesh/ChunkMeshCache.cpp::UChunkMeshCache::RebuildChunk": 468,
    "src/World/Core/MarkRelitInstall.cpp::UWorld::ExecuteLitApplyPlan": 456,
    "src/Render/Engine/GeometryEngine.cpp::UGeometryEngine::DrawCubeGeometry": 451,
    "src/World/Persistence/WorldPersistence.cpp::UWorldPersistence::TickAsyncChunkIo": 447,
    "src/App/Core.cpp::UCore::LoadConfig": 427,
    "src/World/Persistence/WorldPersistence.cpp::UWorldPersistence::FinalizeAsyncTerrainColumnLoad": 421,
    "src/App/Input/InputRouter.cpp::UInputRouter::RouteKey": 411,
    "src/World/Core/World.cpp::UWorld::CountVisibleBlackFocusMeshes": 407,
    "src/World/Core/RuntimeTuning.cpp::URuntimeTuning::LoadStreamingTuneFile": 405,
    "src/WorldGen/Core/PipelineChunkPopulator.cpp::UPipelineChunkPopulator::Populate": 383,
    "src/App/Platform/AppRunner.cpp::RunAutoloadLastWorld": 380,
    "src/World/Core/WorldViewBinding.cpp::UWorld::RunLegacyPhysicsFrame": 372,
    "src/World/Streaming/WorldStreaming.cpp::UWorldStreaming::InitChunkScheduler": 359,
    "src/World/Diagnostics/EnterLitDiagnostics.cpp::UEnterLitDiagnostics::Sample": 337,
    "src/Gui/Screens/SettingsScreen.cpp::USettingsScreen::Build": 334,
    "src/main.cpp::main": 334,
    "src/App/Platform/WindowManager.cpp::UWindowManager::Run": 330,
    "src/App/WorldOperationRunner.cpp::UWorldOperationRunner::AdvanceEnterGameGpuWarmup": 322,
    "src/World/Persistence/WorldPersistence.cpp::UWorldPersistence::AdmitDeferredFarRelightColumns": 315,
    "src/World/Core/World.cpp::UWorld::GetColumnRenderableState": 303,
}


def long_function_violations(fp: Path, text: str) -> list[str]:
    lines = text.split("\n")
    n = len(lines)
    issues: list[str] = []
    i = 0
    while i < n:
        line = lines[i]
        m = FUNC_SIG_RE.match(line)
        if m and not FUNC_CTRL_RE.match(line) and not line.lstrip().startswith("//"):
            j = i
            found = False
            while j < min(i + 4, n):
                if ";" in lines[j] and "{" not in lines[j]:
                    break
                if "{" in lines[j]:
                    found = True
                    break
                j += 1
            if found:
                name = m.group(1).split()[-1]
                depth = 0
                end = None
                for k in range(j, n):
                    for ch in lines[k]:
                        if ch == "{":
                            depth += 1
                        elif ch == "}":
                            depth -= 1
                            if depth == 0:
                                end = k
                                break
                    if end is not None:
                        break
                if end is not None:
                    length = end - i + 1
                    if length > MAX_FUNCTION_LINES:
                        rel = "src/" + fp.relative_to(ROOT).as_posix()
                        key = f"{rel}::{name}"
                        allowed = GRANDFATHERED_LONG_FUNCTIONS.get(key)
                        if allowed is None:
                            issues.append(
                                f"{rel}:{i + 1}: function {name} is {length} "
                                f"lines (> {MAX_FUNCTION_LINES}); split it "
                                f"(new functions must stay under the limit)")
                        elif length > allowed:
                            issues.append(
                                f"{rel}:{i + 1}: function {name} grew to "
                                f"{length} lines (baseline {allowed}); split "
                                f"it or refresh the baseline entry in "
                                f"tools/audit_style.py")
                    i = end + 1
                    continue
        i += 1
    return issues



def struct_field_violations(text: str) -> list[str]:
    issues = []
    skip_names = {
        "namespace", "class", "struct", "enum", "return", "using", "template",
        "public", "private", "protected", "virtual", "override", "const",
        "static", "inline", "friend", "typedef", "if", "for", "while",
        "continue", "break", "return",
    }
    for sm in STRUCT_BLOCK_RE.finditer(text):
        start = sm.end()
        depth = 1
        i = start
        while i < len(text) and depth > 0:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        block = text[start : i - 1]
        for m in STRUCT_FIELD_RE.finditer(block):
            field = m.group(1)
            if field in skip_names:
                continue
            pascal = field[0].upper() + field[1:]
            if field != pascal:
                issues.append(field)
    return issues


def audit_file(fp: Path) -> list[str]:
    if fp.name == "stb_image.h" or "ThirdParty" in str(fp):
        return []
    text = fp.read_text(encoding="utf-8", errors="ignore")
    rel = fp.relative_to(ROOT.parent)
    rel_posix = rel.as_posix()
    issues: list[str] = []

    skip_class_prefix = any(
        rel_posix.startswith(prefix) or rel_posix == prefix
        for prefix in LEGACY_CLASS_FILE_PREFIXES
    )

    if not skip_class_prefix:
        for m in CLASS_RE.finditer(text):
            name = m.group(1)
            if name.startswith("I") or name.startswith("U"):
                continue
            issues.append(f"{rel}: class without U/I prefix: {name}")

    if fp.suffix == ".h":
        for m in MEMBER_RE.finditer(text):
            issues.append(f"{rel}: trailing_ member: {m.group(0)}")

        for field in struct_field_violations(text):
            issues.append(f"{rel}: struct field not PascalCase: {field}")

    for m in BARE_INCLUDE_RE.finditer(text):
        inc = m.group(1).replace("\\", "/")
        if inc not in SKIP_INCLUDES:
            issues.append(f"{rel}: bare include: {inc}")

    if not rel_posix.startswith("src/Test/"):
        issues.extend(long_function_violations(fp, text))

    return issues


def main() -> int:
    all_issues: list[str] = []
    for fp in sorted(ROOT.rglob("*")):
        if fp.suffix not in (".h", ".cpp"):
            continue
        all_issues.extend(audit_file(fp))

    if not all_issues:
        print("No style violations found.")
        return 0

    print(f"Found {len(all_issues)} violation(s):")
    for issue in all_issues[:100]:
        print(f"  {issue}")
    if len(all_issues) > 100:
        print(f"  ... and {len(all_issues) - 100} more")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
