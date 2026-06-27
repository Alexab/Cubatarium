#!/usr/bin/env python3
"""Detect documentation drift vs codebase."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

DOCS = [
    REPO_ROOT / "PERFORMANCE_OPTIMIZATION.md",
    REPO_ROOT / "docs" / "ARCHITECTURE.md",
    REPO_ROOT / "README.md",
]

KNOWN_CHECKS = [
    {
        "doc": "PERFORMANCE_OPTIMIZATION.md",
        "symbol": "Octree",
        "expect_in_src": False,
        "note": "Doc mentions Octree but symbol absent from src/",
    },
    {
        "doc": "PERFORMANCE_OPTIMIZATION.md",
        "symbol": "GetObjectsInRadius",
        "expect_in_src": False,
        "note": "Doc mentions spatial index API not present in src/",
    },
    {
        "doc": "README.md",
        "symbol": "## Architecture",
        "expect_in_src": None,
        "note": "README Architecture section empty or stub",
    },
]


def rg_exists(pattern: str, path: str = "src") -> bool:
    try:
        proc = subprocess.run(
            ["rg", "-l", pattern, path],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        return False
    return proc.returncode == 0 and bool(proc.stdout.strip())


def check_readme_architecture() -> dict | None:
    readme = REPO_ROOT / "README.md"
    if not readme.exists():
        return None
    text = readme.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"## Architecture\s*\n(.*?)($|\n## )", text, re.S)
    if not m:
        return {"doc": "README.md", "issue": "missing ## Architecture section"}
    body = m.group(1).strip()
    if len(body) < 20:
        return {"doc": "README.md", "issue": "Architecture section empty or stub", "body": body}
    return None


def main() -> int:
    ensure_audit_dir()
    drift: list[dict] = []

    for check in KNOWN_CHECKS:
        doc_path = REPO_ROOT / check["doc"]
        if not doc_path.exists():
            continue
        text = doc_path.read_text(encoding="utf-8", errors="ignore")
        if check["symbol"] not in text:
            continue
        if check["expect_in_src"] is False and not rg_exists(check["symbol"]):
            drift.append({**check, "drift": True})
        elif check["expect_in_src"] is True and not rg_exists(check["symbol"]):
            drift.append({**check, "drift": True})

    readme_issue = check_readme_architecture()
    if readme_issue:
        drift.append(readme_issue)

    # ARCHITECTURE.md duplicate table rows (frustum_culling listed twice)
    arch = REPO_ROOT / "docs" / "ARCHITECTURE.md"
    if arch.exists():
        text = arch.read_text(encoding="utf-8", errors="ignore")
        if text.count("| `frustum_culling` |") > 1:
            drift.append(
                {
                    "doc": "docs/ARCHITECTURE.md",
                    "issue": "duplicate frustum_culling rows in render flags table",
                }
            )

    md_files = sorted(REPO_ROOT.rglob("*.md"))
    inventory = [
        {
            "path": p.relative_to(REPO_ROOT).as_posix(),
            "bytes": p.stat().st_size,
        }
        for p in md_files
        if ".git" not in p.parts and "build" not in p.parts
    ]

    out = {
        "generated_at": utc_now_iso(),
        "drift_count": len(drift),
        "drift": drift,
        "markdown_count": len(inventory),
        "markdown_files": inventory,
    }
    write_json(REPO_ROOT / "audit" / "docs_inventory.json", out)
    write_json(REPO_ROOT / "audit" / "docs_drift.json", {"generated_at": utc_now_iso(), "drift": drift})
    print(f"docs_drift: {len(drift)} issue(s), {len(inventory)} markdown file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
