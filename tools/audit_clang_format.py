#!/usr/bin/env python3
"""Check clang-format on changed or specified C++ files under src/. Exit 1 on drift."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
SKIP_PARTS = ("ThirdParty",)


def git_changed_files(base: str = "HEAD") -> list[Path]:
    proc = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=ACMR", base],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        return []
    files: list[Path] = []
    for line in proc.stdout.splitlines():
        p = ROOT / line.strip()
        if p.suffix in (".cpp", ".h") and (SRC in p.parents or p.is_relative_to(SRC)):
            if any(part in SKIP_PARTS for part in p.parts):
                continue
            if p.exists():
                files.append(p)
    return sorted(set(files))


def collect_cpp_files(paths: list[str]) -> list[Path]:
    if not paths:
        return git_changed_files()
    out: list[Path] = []
    for raw in paths:
        p = Path(raw)
        if not p.is_absolute():
            p = ROOT / p
        if p.is_dir():
            for fp in p.rglob("*"):
                if fp.suffix in (".cpp", ".h") and not any(s in fp.parts for s in SKIP_PARTS):
                    out.append(fp)
        elif p.suffix in (".cpp", ".h") and p.exists():
            if not any(s in p.parts for s in SKIP_PARTS):
                out.append(p)
    return sorted(set(out))


def check_format(files: list[Path], clang_format: str) -> list[str]:
    issues: list[str] = []
    for fp in files:
        proc = subprocess.run(
            [clang_format, "--dry-run", "-Werror", str(fp)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="ignore",
        )
        if proc.returncode != 0:
            detail = (proc.stderr or proc.stdout or "format drift").strip().splitlines()
            msg = detail[0] if detail else "format drift"
            rel = fp.relative_to(ROOT)
            issues.append(f"{rel}: {msg}")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description="clang-format check for Cubatarium src/")
    parser.add_argument(
        "paths",
        nargs="*",
        help="Files or dirs (default: git-changed src/**/*.cpp|*.h)",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Check all src/**/*.cpp and src/**/*.h (slow; local only)",
    )
    args = parser.parse_args()

    clang_format = shutil.which("clang-format")
    if not clang_format:
        print("clang-format not found in PATH; skipping format check.", file=sys.stderr)
        return 0

    if args.all:
        files = collect_cpp_files([str(SRC)])
    else:
        files = collect_cpp_files(args.paths)

    if not files:
        print("clang-format: no C++ files to check (pass).")
        return 0

    issues = check_format(files, clang_format)
    if issues:
        print(f"clang-format: {len(issues)} file(s) need formatting:", file=sys.stderr)
        for line in issues[:30]:
            print(f"  {line}", file=sys.stderr)
        if len(issues) > 30:
            print(f"  ... and {len(issues) - 30} more", file=sys.stderr)
        return 1

    print(f"clang-format: OK ({len(files)} file(s)).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
