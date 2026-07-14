#!/usr/bin/env python3
"""Find likely dead C++ symbols (0 callers outside definition)."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

SRC = REPO_ROOT / "src"

# Keep migration/load/CLI/Android entrypoints.
WHITELIST_PREFIXES = (
    "Load",
    "Migrate",
    "Save",
    "Run",
    "main",
    "WinMain",
    "JNI_",
    "android_",
)
WHITELIST_NAMES: set[str] = set()


def rg_count(pattern: str, glob: str = "*.{cpp,h}") -> int:
    try:
        proc = subprocess.run(
            ["rg", "-F", pattern, "--glob", glob, "src", "platforms"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="ignore",
        )
    except FileNotFoundError:
        return -1
    if proc.returncode not in (0, 1):
        return -1
    lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
    return len(lines)


METHOD_RE = re.compile(
    r"^(?:void|bool|int|float|double|std::\w+[\w:<>,\s*&]*|"
    r"glm::\w+[\w:<>,\s*&]*|[A-Z]\w*[\w:<>,\s*&]*)\s+"
    r"([A-Z][A-Za-z0-9_:~]*)\s*::\s*([A-Z][A-Za-z0-9_~]+)\s*\(",
    re.M,
)
FREE_FN_RE = re.compile(
    r"^(?:void|bool|int|float|double|std::\w+[\w:<>,\s*&]*|"
    r"glm::\w+[\w:<>,\s*&]*|[A-Z]\w*[\w:<>,\s*&]*)\s+"
    r"([A-Z][A-Za-z0-9_~]+)\s*\([^;]*\)\s*(?:const)?\s*\{",
    re.M,
)


def is_whitelisted(name: str) -> bool:
    if name in WHITELIST_NAMES:
        return True
    return any(name.startswith(p) for p in WHITELIST_PREFIXES)


def scan_file(path: Path) -> list[dict]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    rel = path.relative_to(REPO_ROOT).as_posix()
    if "WorldGeneratorRegistry.cpp" in rel:
        return []
    hits: list[dict] = []

    for m in METHOD_RE.finditer(text):
        cls, method = m.group(1), m.group(2)
        if method.startswith("~") or is_whitelisted(method):
            continue
        pattern = f"{method}("
        count = rg_count(pattern)
        if count == 1:
            line = text[: m.start()].count("\n") + 1
            hits.append(
                {
                    "symbol": f"{cls}::{method}",
                    "file": rel,
                    "line": line,
                    "callers": count,
                    "kind": "method",
                }
            )

    if path.suffix == ".cpp" and "ThirdParty" not in rel:
        for m in FREE_FN_RE.finditer(text):
            name = m.group(1)
            if is_whitelisted(name) or name.endswith("Test"):
                continue
            if "::" in text[max(0, m.start() - 20) : m.start()]:
                continue
            pattern = f"{name}("
            count = rg_count(pattern)
            if count == 1:
                line = text[: m.start()].count("\n") + 1
                hits.append(
                    {
                        "symbol": name,
                        "file": rel,
                        "line": line,
                        "callers": count,
                        "kind": "function",
                    }
                )
    return hits


def main() -> int:
    ensure_audit_dir()
    candidates: list[dict] = []
    for fp in sorted(SRC.rglob("*")):
        if fp.suffix not in (".cpp", ".h") or "ThirdParty" in str(fp):
            continue
        candidates.extend(scan_file(fp))

    # Known high-confidence dead symbols can be appended here after manual review.
    known: list[dict] = []
    for k in known:
        if not any(c["symbol"] == k["symbol"] for c in candidates):
            candidates.append(k)

    out = {
        "generated_at": utc_now_iso(),
        "count": len(candidates),
        "candidates": candidates,
    }
    write_json(REPO_ROOT / "audit" / "dead_code.json", out)
    print(f"dead_code: {len(candidates)} candidate(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
