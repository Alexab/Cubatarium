#!/usr/bin/env python3
"""Check Render/Pipeline include rules."""

from __future__ import annotations

import re
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

PIPELINE = REPO_ROOT / "src" / "Render" / "Pipeline"
FORBIDDEN = [
    re.compile(r'#include\s+"GeometryEngine\.h"'),
    re.compile(r'#include\s+"Gui/'),
    re.compile(r'#include\s+"App/Application\.h"'),
]
INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')


def main() -> int:
    ensure_audit_dir()
    violations: list[dict] = []
    for fp in sorted(PIPELINE.rglob("*")):
        if fp.suffix not in (".cpp", ".h"):
            continue
        text = fp.read_text(encoding="utf-8", errors="ignore")
        rel = fp.relative_to(REPO_ROOT).as_posix()
        for i, line in enumerate(text.splitlines(), start=1):
            for pat in FORBIDDEN:
                if pat.search(line):
                    violations.append(
                        {
                            "file": rel,
                            "line": i,
                            "include": INCLUDE_RE.search(line).group(1) if INCLUDE_RE.search(line) else line.strip(),
                            "rule": "Render/Pipeline README forbidden include",
                        }
                    )
    out = {
        "generated_at": utc_now_iso(),
        "count": len(violations),
        "violations": violations,
    }
    write_json(REPO_ROOT / "audit" / "include_violations.json", out)
    print(f"include_violations: {len(violations)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
