#!/usr/bin/env python3
"""Compare src/**/*.cpp against CMakeLists.txt sources."""

from __future__ import annotations

import re
from pathlib import Path

from schema import REPO_ROOT, ensure_audit_dir, utc_now_iso, write_json

CMAKE = REPO_ROOT / "CMakeLists.txt"
SRC = REPO_ROOT / "src"


def cmake_cpp_paths() -> set[str]:
    text = CMAKE.read_text(encoding="utf-8", errors="ignore")
    paths = set(re.findall(r"src/[A-Za-z0-9_./-]+\.cpp", text))
    return {p.replace("\\", "/") for p in paths}


def main() -> int:
    ensure_audit_dir()
    in_cmake = cmake_cpp_paths()
    all_cpp = {
        p.relative_to(REPO_ROOT).as_posix()
        for p in SRC.rglob("*.cpp")
        if "ThirdParty" not in str(p)
    }
    orphans = sorted(all_cpp - in_cmake)
    missing = sorted(in_cmake - all_cpp)

    out = {
        "generated_at": utc_now_iso(),
        "cmake_count": len(in_cmake),
        "src_count": len(all_cpp),
        "orphans_not_in_cmake": orphans,
        "cmake_missing_on_disk": missing,
    }
    write_json(REPO_ROOT / "audit" / "cmake_orphans.json", out)
    print(f"cmake_orphans: {len(orphans)} orphan(s), {len(missing)} missing on disk")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
