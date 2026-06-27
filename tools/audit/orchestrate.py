#!/usr/bin/env python3
"""Cubatarium code audit orchestrator."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

AUDIT_PKG = Path(__file__).resolve().parent
if str(AUDIT_PKG) not in sys.path:
    sys.path.insert(0, str(AUDIT_PKG))

from merge_findings import main as merge_main  # noqa: E402
from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json  # noqa: E402

SCANNERS = [
    "scan_dead_code.py",
    "scan_duplicates.py",
    "scan_includes.py",
    "scan_perf_hints.py",
    "scan_cmake_sources.py",
    "scan_tools_usage.py",
    "scan_docs_drift.py",
]

GOD_CLASSES = [
    "src/World/Core/World.cpp",
    "src/Render/Engine/GeometryEngine.cpp",
    "src/App/Application.cpp",
]

MODULES = [
    ("world", "src/World"),
    ("render", "src/Render"),
    ("app_gui", "src/App"),
    ("worldgen", "src/WorldGen"),
    ("packs", "src/ResourcePacks"),
    ("creatures", "src/Creatures"),
    ("console", "src/Commands"),
    ("tools", "tools"),
]


def git_commit() -> str:
    try:
        proc = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return proc.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return "unknown"


def run_py(script: str) -> int:
    proc = subprocess.run(
        [sys.executable, str(AUDIT_PKG / script)],
        cwd=REPO_ROOT,
    )
    return proc.returncode


def run_audit_style() -> tuple[int, list[str]]:
    proc = subprocess.run(
        [sys.executable, str(REPO_ROOT / "tools" / "audit_style.py")],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="ignore",
    )
    issues = []
    for line in proc.stdout.splitlines():
        if line.startswith("  "):
            issues.append(line.strip())
    return proc.returncode, issues


def loc_by_module() -> dict[str, int]:
    counts: dict[str, int] = {}
    src = REPO_ROOT / "src"
    for fp in src.rglob("*"):
        if fp.suffix not in (".cpp", ".h") or "ThirdParty" in str(fp):
            continue
        parts = fp.relative_to(src).parts
        mod = parts[0] if parts else "root"
        try:
            n = len(fp.read_text(encoding="utf-8", errors="ignore").splitlines())
        except OSError:
            n = 0
        counts[mod] = counts.get(mod, 0) + n
    return dict(sorted(counts.items(), key=lambda x: -x[1]))


def god_class_sizes() -> dict[str, int]:
    out: dict[str, int] = {}
    for rel in GOD_CLASSES:
        fp = REPO_ROOT / rel
        if fp.exists():
            out[rel] = len(fp.read_text(encoding="utf-8", errors="ignore").splitlines())
    return out


def phase_baseline(commit: str) -> int:
    ensure_audit_dir()
    style_rc, style_issues = run_audit_style()
    baseline = {
        "generated_at": utc_now_iso(),
        "commit": commit,
        "smoke": "skipped",
        "style_violations_count": len(style_issues),
        "style_violations_sample": style_issues[:50],
        "loc_by_module": loc_by_module(),
        "god_class_lines": god_class_sizes(),
    }
    write_json(REPO_ROOT / "audit" / "baseline.json", baseline)

    md_lines = [
        "# Audit Baseline",
        "",
        f"- Commit: `{commit}`",
        f"- Generated: {baseline['generated_at']}",
        f"- Style violations: **{len(style_issues)}**",
        f"- Smoke: {baseline['smoke']} (run doctor-windows separately for full baseline)",
        "",
        "## God-class LOC",
        "",
    ]
    for path, n in baseline["god_class_lines"].items():
        md_lines.append(f"- `{path}`: {n} lines")
    md_lines.extend(["", "## LOC by module (top 10)", ""])
    for mod, n in list(baseline["loc_by_module"].items())[:10]:
        md_lines.append(f"- {mod}: {n}")
    (REPO_ROOT / "docs" / "AUDIT_BASELINE.md").write_text("\n".join(md_lines) + "\n", encoding="utf-8")

    run_py("scan_docs_drift.py")
    print(f"baseline: style={len(style_issues)} violations")
    return 0 if style_rc in (0, 1) else style_rc


def phase_scan() -> int:
    rc = 0
    summary: dict[str, int] = {}
    for script in SCANNERS:
        if run_py(script) != 0:
            rc = 1
        key = script.replace("scan_", "").replace(".py", "")
        out = REPO_ROOT / "audit" / f"{key}.json"
        if out.exists():
            data = json.loads(out.read_text(encoding="utf-8"))
            summary[key] = data.get("count", data.get("drift_count", 0))
    write_json(
        REPO_ROOT / "audit" / "scan_summary.json",
        {"generated_at": utc_now_iso(), "counts": summary},
    )
    return rc


def seed_module_findings() -> None:
    """Seed module JSON from scans when module agents have not run."""
    modules_dir = REPO_ROOT / "audit" / "modules"
    modules_dir.mkdir(parents=True, exist_ok=True)

    seeds: dict[str, list[dict]] = {
        "world": [
            {
                "id": "AUDIT-WORLD-001",
                "category": "duplication",
                "priority": "P1",
                "module": "World",
                "title": "JSON voxel parsing duplicated across LoadBlocks/LoadChunks",
                "files": ["src/World/Core/World.cpp"],
                "evidence": "separate JSON parse loops for blocks and monolithic chunks",
                "action": "extract ULegacyChunkJsonLoader shared parser",
                "risk": "low",
            },
            {
                "id": "AUDIT-WORLD-002",
                "category": "performance",
                "priority": "P1",
                "module": "World",
                "title": "MarkBlockChunkDirty uses RebuildChunkImmediate vs MarkDirty branches",
                "files": ["src/World/Core/World.cpp"],
                "evidence": "dual mesh update paths",
                "action": "unify dirty chunk + neighbor propagation",
                "risk": "medium",
            },
        ],
        "render": [
            {
                "id": "AUDIT-RENDER-001",
                "category": "architecture",
                "priority": "P2",
                "module": "Render",
                "title": "GeometryEngine ~2600 LOC with World+Gui+Creatures coupling",
                "files": ["src/Render/Engine/GeometryEngine.cpp"],
                "evidence": "god-class size from baseline",
                "action": "enforce Pipeline include rules; incremental backend extraction",
                "risk": "medium",
            },
            {
                "id": "AUDIT-RENDER-002",
                "category": "architecture",
                "priority": "P2",
                "module": "Render",
                "title": "StreamingHorizonBlocks deprecated but still used",
                "files": ["src/Render/Engine/DistanceFog.h", "src/World/Core/World.cpp"],
                "evidence": "@deprecated on StreamingHorizonBlocks; UWorld::GetStreamingHorizonBlocks calls it",
                "action": "migrate callers to FogHorizonBlocks/RenderHorizonBlocks",
                "risk": "low",
            },
        ],
        "app_gui": [
            {
                "id": "AUDIT-APP-001",
                "category": "architecture",
                "priority": "P2",
                "module": "App",
                "title": "UApplication ~1800 LOC combines game loop and all screens",
                "files": ["src/App/Application.cpp"],
                "evidence": "god-class from baseline",
                "action": "extract screen transition helpers",
                "risk": "medium",
            },
            {
                "id": "AUDIT-APP-002",
                "category": "architecture",
                "priority": "P2",
                "module": "App",
                "title": "Legacy config shims scattered in Core.cpp",
                "files": ["src/App/Core.cpp"],
                "evidence": "legacy_hud, legacy generator warnings",
                "action": "group in LegacyConfigAdapter",
                "risk": "low",
            },
        ],
        "worldgen": [
            {
                "id": "AUDIT-WG-001",
                "category": "architecture",
                "priority": "P3",
                "module": "WorldGen",
                "title": "ApplyLegacyOverworldProfile shims in ProceduralConfigIO",
                "files": ["src/WorldGen/Core/ProceduralConfigIO.cpp"],
                "evidence": "legacy generator id migration",
                "action": "document; keep for migration",
                "risk": "low",
            },
        ],
        "packs": [
            {
                "id": "AUDIT-PACK-001",
                "category": "performance",
                "priority": "P3",
                "module": "ResourcePacks",
                "title": "TD-002 incremental atlas rebuild deferred",
                "files": ["docs/TECH_DEBT_RESOURCE_PACKS.md"],
                "tech_debt_ref": "TD-002",
                "evidence": "open tech debt item",
                "action": "defer; out of scope for quick wins",
                "risk": "low",
            },
        ],
        "creatures": [
            {
                "id": "AUDIT-CRE-001",
                "category": "architecture",
                "priority": "P3",
                "module": "Creatures",
                "title": "glTF backend stub (TD-CRE-001)",
                "files": ["src/Creatures/Visual/CreatureVisualGltf.cpp"],
                "tech_debt_ref": "TD-CRE-001",
                "evidence": "open in TECH_DEBT_CREATURES.md",
                "action": "defer",
                "risk": "low",
            },
        ],
        "console": [
            {
                "id": "AUDIT-TEST-001",
                "category": "architecture",
                "priority": "P2",
                "module": "Test",
                "title": "chunk_load_priority_test not in CI",
                "files": ["CMakeLists.txt", ".github/workflows/windows-release-smoke.yml"],
                "evidence": "single C++ test target exists but not run in workflow",
                "action": "add to CI in PR-F",
                "risk": "low",
                "auto_fixable": True,
            },
        ],
        "tools": [],
    }

    tools_orphans = REPO_ROOT / "audit" / "tools_orphans.json"
    if tools_orphans.exists():
        orphans = json.loads(tools_orphans.read_text(encoding="utf-8")).get("orphans", [])
        seeds["tools"] = [
            {
                "id": f"AUDIT-TOOL-{i:03d}",
                "category": "architecture",
                "priority": "P3",
                "module": "tools",
                "title": f"Orphan script {o.get('script')}",
                "files": [o.get("script", "")],
                "evidence": o.get("reason", ""),
                "action": "archive or document",
                "risk": "low",
            }
            for i, o in enumerate(orphans[:10], start=1)
        ]

    for mod_id, findings in seeds.items():
        path = modules_dir / f"{mod_id}.json"
        if path.exists():
            continue
        write_json(
            path,
            {
                "module_id": mod_id,
                "generated_at": utc_now_iso(),
                "findings": findings,
            },
        )


def phase_report(commit: str) -> int:
    seed_module_findings()
    return merge_main(commit)


def main() -> int:
    parser = argparse.ArgumentParser(description="Cubatarium code audit orchestrator")
    parser.add_argument(
        "--phase",
        choices=["baseline", "scan", "report", "modules", "all"],
        default="all",
    )
    parser.add_argument("--commit", default="")
    args = parser.parse_args()
    commit = args.commit or git_commit()

    if args.phase in ("baseline", "all"):
        if phase_baseline(commit) != 0:
            return 1
    if args.phase in ("scan", "all"):
        if phase_scan() != 0:
            return 1
    if args.phase in ("modules", "all"):
        seed_module_findings()
    if args.phase in ("report", "all"):
        if phase_report(commit) != 0:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
