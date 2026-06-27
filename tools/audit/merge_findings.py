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
    seen: set[str] = set()
    out: list[Finding] = []
    for f in findings:
        key = f.id
        if key in seen:
            continue
        seen.add(key)
        out.append(f)
    return out


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
        "| Priority | Count |",
        "|----------|-------|",
    ]
    for p in ("P0", "P1", "P2", "P3"):
        lines.append(f"| {p} | {doc.summary.get(p, 0)} |")
    lines.extend(["", "## Findings by Priority", ""])

    for priority in ("P0", "P1", "P2", "P3"):
        group = [f for f in doc.findings if f.priority == priority]
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
    findings = dedupe_findings(findings_from_scans() + load_module_findings())

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
    if not any(f.id == "AUDIT-DUP-001" for f in findings):
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

    doc = FindingsDocument(
        status="pending_review",
        commit=commit,
        generated_at=utc_now_iso(),
        summary=summarize_priorities(findings),
        findings=sorted(findings, key=lambda f: (f.priority, f.id)),
    )
    write_json(AUDIT_DIR / "findings.json", doc.to_dict())
    report = render_report(doc)
    (REPO_ROOT / "docs" / "AUDIT_REPORT_2026.md").write_text(report, encoding="utf-8")
    print(f"findings: {len(findings)} total; report written to docs/AUDIT_REPORT_2026.md")
    return 0


if __name__ == "__main__":
    import sys

    commit = sys.argv[1] if len(sys.argv) > 1 else ""
    raise SystemExit(main(commit))
