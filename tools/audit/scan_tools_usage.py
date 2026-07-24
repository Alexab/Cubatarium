#!/usr/bin/env python3
"""Find tools/*.py scripts with no references in CI/docs/other tools."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

TOOLS = REPO_ROOT / "tools"
SEARCH_ROOTS = [
    REPO_ROOT / ".github",
    REPO_ROOT / "docs",
    REPO_ROOT / "scripts",
    REPO_ROOT / "README.md",
    REPO_ROOT / "CMakeLists.txt",
    REPO_ROOT / "tools" / "README.md",
]

INTERNAL_LIBS = {
    "schema.py",
    "worldgen_metrics_lib.py",
    "resource_pack_lib.py",
    "canonical_blocks.yaml",
}

IMPORT_RE = re.compile(
    r"^\s*(?:from\s+([\w.]+)\s+import|import\s+([\w.]+))",
    re.MULTILINE,
)


def script_referenced(name: str, stem: str) -> bool:
    try:
        proc = subprocess.run(
            ["rg", "-l", re.escape(name), str(REPO_ROOT)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="ignore",
        )
    except FileNotFoundError:
        return True
    files = [Path(p) for p in proc.stdout.splitlines() if p.strip()]
    for fp in files:
        rel = fp.relative_to(REPO_ROOT).as_posix()
        if rel.startswith("tools/audit/"):
            continue
        if rel.startswith("tools/") and fp.name == name:
            continue
        if rel.startswith("audit/"):
            continue
        if rel == "tools/archive/README.md":
            continue
        return True

    # Also treat Python imports of this module stem as references.
    for fp in TOOLS.rglob("*.py"):
        if fp.name == name or "archive" in fp.parts:
            continue
        text = fp.read_text(encoding="utf-8", errors="ignore")
        for m in IMPORT_RE.finditer(text):
            mod = m.group(1) or m.group(2) or ""
            mod = mod.split(".")[0]
            if mod == stem:
                return True
    return False


def main() -> int:
    ensure_audit_dir()
    orphans: list[dict] = []
    for fp in sorted(TOOLS.rglob("*.py")):
        if "archive" in fp.parts or fp.parts[-2:] == ("audit", fp.name):
            continue
        name = fp.name
        if name.startswith("__"):
            continue
        if name in INTERNAL_LIBS:
            continue
        if not script_referenced(name, fp.stem):
            orphans.append(
                {
                    "script": fp.relative_to(REPO_ROOT).as_posix(),
                    "reason": "no references in CI/docs/scripts/README or import graph",
                }
            )

    out = {
        "generated_at": utc_now_iso(),
        "count": len(orphans),
        "orphans": orphans,
    }
    write_json(REPO_ROOT / "audit" / "tools_orphans.json", out)
    print(f"tools_orphans: {len(orphans)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
