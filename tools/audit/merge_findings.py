#!/usr/bin/env python3
"""Merge scan + module JSON into audit/findings.json and AUDIT_REPORT markdown."""

from __future__ import annotations

import json
from pathlib import Path

from schema import (
    AUDIT_DIR,
    Finding,
    FindingsDocument,
    REPO_ROOT,
    ensure_audit_dir,
    summarize_priorities,
    utc_now_iso,
    write_json,
)

SCAN_FINDING_MAP = {
    "dead_code.json": ("dead_code", "P0"),
    "include_violations.json": ("architecture", "P1"),
    "perf_hints.json": ("performance", "P1"),
    "docs_drift.json": ("docs", "P1"),
    "cmake_orphans.json": ("architecture", "P2"),
    "tools_orphans.json": ("architecture", "P3"),
    "duplicates.json": ("duplication", "P2"),
}


def load_module_findings() -> list[Finding]:
    findings: list[Finding] = []
    modules_dir = AUDIT_DIR / "modules"
    if not modules_dir.exists():
        return findings
    for fp in sorted(modules_dir.glob("*.json")):
        data = json.loads(fp.read_text(encoding="utf-8"))
        for raw in data.get("findings", []):
            findings.append(Finding(**raw))
    return findings


def is_registry_factory_false_positive(candidate: dict) -> bool:
    file = candidate.get("file", "")
    sym = candidate.get("symbol", "")
    if "WorldGeneratorRegistry.cpp" not in file:
        return False
    return sym.startswith(("Apply", "Create")) and (
        sym.endswith("Defaults") or sym.startswith("Create")
    )


def findings_from_dead_code(data: dict) -> list[Finding]:
    out: list[Finding] = []
    for i, c in enumerate(data.get("candidates", []), start=1):
        sym = c.get("symbol", "")
        if "SaveBlocks" in sym or "SaveChunks" in sym:
            out.append(
                Finding(
                    id=f"AUDIT-DEAD-{i:03d}",
                    category="dead_code",
                    priority="P0",
                    module="World",
                    title=f"{sym} has no callers",
                    files=[c.get("file", "")],
                    lines=[c.get("line", 0)],
                    evidence=c.get("note", "rg shows definition only"),
                    action="remove unused save methods; keep Load/Migrate paths",
                    risk="low",
                    auto_fixable=True,
                    verification=["doctor-windows.ps1", "integration_test_worldgen.py"],
                )
            )
        elif c.get("callers", 99) <= 1:
            if is_registry_factory_false_positive(c):
                continue
            if sym in ("HasChunkJsonFiles", "ResolveMovementAxisEye"):
                continue
            out.append(
                Finding(
                    id=f"AUDIT-DEAD-{i:03d}",
                    category="dead_code",
                    priority="P2",
                    module="unknown",
                    title=f"Possible dead symbol {sym}",
                    files=[c.get("file", "")],
                    lines=[c.get("line", 0)],
                    evidence=f"callers={c.get('callers')}",
                    action="manual verify before removal",
                    risk="medium",
                )
            )
    return out


def perf_hint_resolved(h: dict) -> bool:
    rel = h.get("file", "")
    if "GreedyMesher.cpp" not in rel:
        return False
    fp = REPO_ROOT / rel.replace("/", "\\") if "\\" not in rel else REPO_ROOT / rel
    if not fp.exists():
        fp = REPO_ROOT / rel
    if not fp.exists():
        return False
    return "quads.reserve(512)" in fp.read_text(encoding="utf-8", errors="ignore")


def findings_from_scans() -> list[Finding]:
    findings: list[Finding] = []
    seq = 1
    dead_path = AUDIT_DIR / "dead_code.json"
    if dead_path.exists():
        findings.extend(findings_from_dead_code(json.loads(dead_path.read_text(encoding="utf-8"))))

    drift_path = AUDIT_DIR / "docs_drift.json"
    if drift_path.exists():
        drift = json.loads(drift_path.read_text(encoding="utf-8")).get("drift", [])
        for d in drift:
            findings.append(
                Finding(
                    id=f"AUDIT-DOC-{seq:03d}",
                    category="docs",
                    priority="P1",
                    module="docs",
                    title=d.get("issue") or d.get("note", "documentation drift"),
                    files=[d.get("doc", "docs/")],
                    evidence=str(d),
                    action="update documentation",
                    risk="low",
                    auto_fixable=True,
                    verification=["manual review"],
                )
            )
            seq += 1

    perf_path = AUDIT_DIR / "perf_hints.json"
    if perf_path.exists():
        for h in json.loads(perf_path.read_text(encoding="utf-8")).get("hints", [])[:20]:
            if perf_hint_resolved(h):
                continue
            findings.append(
                Finding(
                    id=f"AUDIT-PERF-{seq:03d}",
                    category="performance",
                    priority="P1",
                    module="Render",
                    title=h.get("hint", "perf hint"),
                    files=[h.get("file", "")],
                    lines=[h.get("line", 0)],
                    evidence=h.get("hint", ""),
                    action="add reserve() or unify dirty path",
                    risk="low",
                )
            )
            seq += 1

    return findings


def dedupe_findings(findings: list[Finding]) -> list[Finding]:
    """Last occurrence wins so module agent JSON overrides auto-scan seeds."""
    by_id: dict[str, Finding] = {}
    for f in findings:
        by_id[f.id] = f
    return list(by_id.values())


def load_previous_findings() -> dict[str, Finding]:
    path = AUDIT_DIR / "findings.json"
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    return {raw["id"]: Finding(**raw) for raw in data.get("findings", [])}


def has_duplicate_chunk_storage_include() -> bool:
    world_cpp = REPO_ROOT / "src/World/Core/World.cpp"
    if not world_cpp.exists():
        return False
    includes = [
        line.strip()
        for line in world_cpp.read_text(encoding="utf-8", errors="ignore").splitlines()
        if "ChunkStorageService.h" in line and line.strip().startswith("#include")
    ]
    return len(includes) > 1


def auto_resolved_ids() -> dict[str, str]:
    """Map finding id -> resolution note when fix is detected in tree."""
    resolved: dict[str, str] = {}
    world_cpp = REPO_ROOT / "src/World/Core/World.cpp"
    world_text = (
        world_cpp.read_text(encoding="utf-8", errors="ignore") if world_cpp.exists() else ""
    )

    if not has_duplicate_chunk_storage_include():
        resolved["AUDIT-DUP-001"] = "duplicate ChunkStorageService include removed"

    if "ULegacyChunkJsonLoader" in world_text:
        resolved["AUDIT-WORLD-001"] = "extracted to src/World/IO/LegacyChunkJsonLoader.*"

    fog_h = REPO_ROOT / "src/Render/Engine/DistanceFog.h"
    if fog_h.exists() and "StreamingHorizonBlocks" not in fog_h.read_text(
        encoding="utf-8", errors="ignore"
    ):
        resolved["AUDIT-RENDER-002"] = "StreamingHorizonBlocks removed"

    ci = REPO_ROOT / ".github/workflows/windows-release-smoke.yml"
    if ci.exists() and "chunk_load_priority_test" in ci.read_text(encoding="utf-8", errors="ignore"):
        resolved["AUDIT-TEST-001"] = "chunk_load_priority_test in windows-release-smoke CI"

    style_proc = __import__("subprocess").run(
        [__import__("sys").executable, str(REPO_ROOT / "tools" / "audit_style.py")],
        cwd=REPO_ROOT,
        capture_output=True,
    )
    if style_proc.returncode == 0:
        resolved["AUDIT-STYLE-001"] = "audit_style.py reports 0 violations"

    return resolved


def apply_status(findings: list[Finding], commit: str) -> tuple[list[Finding], str, list[str]]:
    previous = load_previous_findings()
    auto_done = auto_resolved_ids()
    doc_status = "pending_review"
    approved_ids: list[str] = []

    prev_path = AUDIT_DIR / "findings.json"
    if prev_path.exists():
        prev_doc = json.loads(prev_path.read_text(encoding="utf-8"))
        doc_status = prev_doc.get("status", doc_status)
        approved_ids = list(prev_doc.get("approved_ids", []))

    for f in findings:
        if f.status in ("done", "rejected"):
            continue
        prev = previous.get(f.id)
        if prev and prev.status == "done":
            f.status = "done"
            f.implemented_in = prev.implemented_in or f.implemented_in
            continue
        if prev and prev.status == "rejected":
            f.status = "rejected"
            continue
        if f.id in auto_done:
            f.status = "done"
            f.implemented_in = commit or f.implemented_in
            f.evidence = f"{f.evidence}; resolved: {auto_done[f.id]}".strip("; ")

    return findings, doc_status, approved_ids


def summarize_open(findings: list[Finding]) -> dict[str, int]:
    summary = {"P0": 0, "P1": 0, "P2": 0, "P3": 0}
    for f in findings:
        if f.status in ("done", "rejected"):
            continue
        summary[f.priority] = summary.get(f.priority, 0) + 1
    return summary


def render_report(doc: FindingsDocument) -> str:
    lines = [
        "# Cubatarium Audit Report 2026",
        "",
        f"- **Commit:** `{doc.commit}`",
        f"- **Generated:** {doc.generated_at}",
        f"- **Status:** {doc.status}",
        "",
        "## Executive Summary",
        "",
        "Open findings (done excluded):",
        "",
        "| Priority | Open |",
        "|----------|------|",
    ]
    done_count = sum(1 for f in doc.findings if f.status == "done")
    rejected_count = sum(1 for f in doc.findings if f.status == "rejected")
    for p in ("P0", "P1", "P2", "P3"):
        lines.append(f"| {p} | {doc.summary.get(p, 0)} |")
    lines.append(
        f"\nClosed: **{done_count}** | Rejected (false positive): **{rejected_count}**.\n"
    )
    lines.extend(["## Open Findings by Priority", ""])

    for priority in ("P0", "P1", "P2", "P3"):
        group = [
            f
            for f in doc.findings
            if f.priority == priority and f.status not in ("done", "rejected")
        ]
        if not group:
            continue
        lines.append(f"### {priority}")
        lines.append("")
        for f in group:
            files = ", ".join(f.files) if f.files else "n/a"
            lines.append(f"- **{f.id}** [{f.category}] {f.title}")
            lines.append(f"  - Module: {f.module}; Files: {files}")
            lines.append(f"  - Action: {f.action or 'review'}")
            if f.evidence:
                lines.append(f"  - Evidence: {f.evidence[:200]}")
        lines.append("")

    closed = [f for f in doc.findings if f.status == "done"]
    if closed:
        lines.extend(["## Closed Findings", ""])
        for f in sorted(closed, key=lambda x: x.id):
            note = f.implemented_in or "local"
            lines.append(f"- **{f.id}** — {f.title} (`{note}`)")
        lines.append("")

    rejected = [f for f in doc.findings if f.status == "rejected"]
    if rejected:
        lines.extend(["## Rejected Findings (scan false positives)", ""])
        for f in sorted(rejected, key=lambda x: x.id):
            lines.append(f"- **{f.id}** — {f.title}")
        lines.append("")

    lines.extend(
        [
            "## Recommended PR Sequence",
            "",
            "1. **PR-A:** P0 dead code + duplicate includes + auto-fixable style",
            "2. **PR-B:** World/IO — legacy JSON loader extract, chunk dirty helper",
            "3. **PR-C:** Render — fog API cleanup, include hygiene",
            "4. **PR-D:** App/Gui — incremental Application extractions",
            "5. **PR-E:** Perf micro-optimizations with smoke metrics",
            "6. **PR-F:** Documentation sync + CI style gate",
            "",
            "## Human Gate",
            "",
            "Set `audit/findings.json` → `\"status\": \"approved\"` and optional `approved_ids` before Fix agents run.",
            "",
        ]
    )
    return "\n".join(lines)


def main(commit: str = "") -> int:
    ensure_audit_dir()
    findings = dedupe_findings(load_module_findings() + findings_from_scans())

    # Ensure known architectural findings if modules not run yet.
    if not any(f.id == "AUDIT-ARCH-001" for f in findings):
        findings.append(
            Finding(
                id="AUDIT-ARCH-001",
                category="architecture",
                priority="P2",
                module="World",
                title="UWorld combines persistence, streaming, mesh, creatures",
                files=["src/World/Core/World.cpp", "src/World/Core/World.h"],
                evidence="~3500 LOC god class",
                action="incremental extract UWorldPersistence / UWorldStreaming facades",
                risk="medium",
            )
        )
    if has_duplicate_chunk_storage_include() and not any(
        f.id == "AUDIT-DUP-001" for f in findings
    ):
        findings.append(
            Finding(
                id="AUDIT-DUP-001",
                category="duplication",
                priority="P1",
                module="World",
                title="Duplicate #include ChunkStorageService.h in World.cpp",
                files=["src/World/Core/World.cpp"],
                lines=[34, 35],
                evidence="two consecutive identical includes",
                action="remove duplicate include",
                risk="low",
                auto_fixable=True,
                verification=["doctor-windows.ps1"],
            )
        )

    findings, doc_status, approved_ids = apply_status(findings, commit)
    open_count = sum(1 for f in findings if f.status != "done")
    done_count = sum(1 for f in findings if f.status == "done")

    doc = FindingsDocument(
        status=doc_status,
        commit=commit,
        generated_at=utc_now_iso(),
        summary=summarize_open(findings),
        approved_ids=approved_ids,
        findings=sorted(findings, key=lambda f: (f.priority, f.id)),
    )
    write_json(AUDIT_DIR / "findings.json", doc.to_dict())
    report = render_report(doc)
    (REPO_ROOT / "docs" / "AUDIT_REPORT_2026.md").write_text(report, encoding="utf-8")
    print(
        f"findings: {len(findings)} total ({open_count} open, {done_count} done); "
        "report written to docs/AUDIT_REPORT_2026.md"
    )
    return 0


if __name__ == "__main__":
    import sys

    commit = sys.argv[1] if len(sys.argv) > 1 else ""
    raise SystemExit(main(commit))
