#!/usr/bin/env python3
"""Module boundary include rules (World must not include Render headers)."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
WORLD_SRC = REPO_ROOT / "src" / "World"
MESH_ADAPTER = WORLD_SRC / "Mesh"
INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')

# Legacy .h violations; remove as refactor PRs land.
ALLOWLIST: set[tuple[str, str]] = set()


def is_mesh_adapter(rel_posix: str) -> bool:
    return rel_posix.startswith("src/World/Mesh/")


def scan_world_render_includes() -> list[dict[str, str | int]]:
    violations: list[dict[str, str | int]] = []
    for fp in sorted(WORLD_SRC.rglob("*")):
        if fp.suffix != ".h":
            continue
        rel = fp.relative_to(REPO_ROOT).as_posix()
        if is_mesh_adapter(rel):
            continue
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
            if key in ALLOWLIST:
                continue
            violations.append({"file": rel, "line": line_no, "include": inc})
    return violations


def main() -> int:
    violations = scan_world_render_includes()
    if violations:
        print(f"check_include_rules: {len(violations)} violation(s) (World -> Render):")
        for v in violations:
            print(f"  {v['file']}:{v['line']}: #include \"{v['include']}\"")
        return 1
    allow_count = len(ALLOWLIST)
    if allow_count:
        print(
            f"check_include_rules: ok (World/Mesh adapter exempt; "
            f"{allow_count} allowlisted legacy include(s))"
        )
    else:
        print("check_include_rules: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
