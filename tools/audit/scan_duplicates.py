#!/usr/bin/env python3
"""Find duplicated code blocks in src/ via normalized line hashing."""

from __future__ import annotations

import hashlib
from collections import defaultdict
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

SRC = REPO_ROOT / "src"
MIN_LINES = 15


def normalize_line(line: str) -> str | None:
    s = line.strip()
    if not s or s.startswith("//") or s.startswith("*") or s.startswith("#"):
        return None
    return s


def block_hash(lines: list[str]) -> str:
    payload = "\n".join(lines)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()[:16]


def scan_file(path: Path) -> dict[str, list[tuple[int, str]]]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    raw = text.splitlines()
    norm: list[tuple[int, str]] = []
    for i, line in enumerate(raw, start=1):
        n = normalize_line(line)
        if n:
            norm.append((i, n))

    buckets: dict[str, list[tuple[int, str]]] = defaultdict(list)
    if len(norm) < MIN_LINES:
        return buckets

    for start in range(0, len(norm) - MIN_LINES + 1):
        chunk = norm[start : start + MIN_LINES]
        h = block_hash([c[1] for c in chunk])
        rel = path.relative_to(REPO_ROOT).as_posix()
        buckets[h].append((chunk[0][0], rel))
    return buckets


def main() -> int:
    ensure_audit_dir()
    global_map: dict[str, list[tuple[int, str]]] = defaultdict(list)

    for fp in sorted(SRC.rglob("*")):
        if fp.suffix not in (".cpp", ".h") or "ThirdParty" in str(fp):
            continue
        for h, entries in scan_file(fp).items():
            global_map[h].extend(entries)

    clusters = []
    for h, entries in global_map.items():
        files = sorted({e[1] for e in entries})
        if len(files) < 2:
            continue
        clusters.append(
            {
                "hash": h,
                "line_count": MIN_LINES,
                "occurrences": [{"file": f, "line": ln} for ln, f in entries[:6]],
                "files": files,
            }
        )

    clusters.sort(key=lambda c: (-len(c["files"]), c["hash"]))
    out = {
        "generated_at": utc_now_iso(),
        "min_lines": MIN_LINES,
        "count": len(clusters),
        "clusters": clusters[:100],
    }
    write_json(REPO_ROOT / "audit" / "duplicates.json", out)
    print(f"duplicates: {len(clusters)} cluster(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
