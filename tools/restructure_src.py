#!/usr/bin/env python3
"""Restructure src/ layout: move files, fix #include paths, update CMakeLists.txt."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
MAPPING_FILE = Path(__file__).resolve().parent / "src_restructure_mapping.json"
CMAKE = ROOT / "CMakeLists.txt"

# Case-only folder names break git mv on Windows; use temp names first.
CASE_FOLDERS = {
    "activity": "_rs_activity",
    "pose": "_rs_pose",
    "render": "_rs_render",
    "worldgen": "_rs_worldgen",
}

INCLUDE_RE = re.compile(r'(#include\s+")([^"]+)(")')


def load_moves() -> dict[str, str]:
    data = json.loads(MAPPING_FILE.read_text(encoding="utf-8"))
    moves = data["moves"]
    adjusted: dict[str, str] = {}
    for old_rel, new_rel in moves.items():
        old_fwd = old_rel.replace("\\", "/")
        for low, tmp in CASE_FOLDERS.items():
            if old_fwd.startswith(low + "/"):
                old_fwd = tmp + old_fwd[len(low) :]
                break
            if old_fwd == low:
                old_fwd = tmp
                break
        adjusted[old_fwd] = new_rel
    return adjusted


def load_include_moves() -> dict[str, str]:
    data = json.loads(MAPPING_FILE.read_text(encoding="utf-8"))
    return data["moves"]


def prepare_case_folders() -> None:
    for low, tmp in CASE_FOLDERS.items():
        src = SRC / low
        mid = SRC / tmp
        if src.exists() and not mid.exists():
            git_mv(src, mid)


def git_mv(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not src.exists():
        if dst.exists():
            return
        raise FileNotFoundError(src)
    if dst.exists():
        raise FileExistsError(dst)
    result = subprocess.run(
        ["git", "mv", str(src), str(dst)], cwd=ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        # Fall back for untracked intermediates on Windows.
        import shutil

        shutil.move(str(src), str(dst))
        subprocess.run(["git", "add", str(dst)], cwd=ROOT, check=True)
        if src.exists():
            subprocess.run(["git", "add", "-u", str(src)], cwd=ROOT, check=False)


def move_files(moves: dict[str, str]) -> None:
    # Longest source paths first so nested dirs move before parents empty.
    for old_rel, new_rel in sorted(moves.items(), key=lambda kv: len(kv[0]), reverse=True):
        src = SRC / old_rel.replace("/", "\\") if "\\" in str(SRC) else SRC / old_rel
        dst = SRC / new_rel
        git_mv(src, dst)


def build_include_map(moves: dict[str, str]) -> dict[str, str]:
    inc_map: dict[str, str] = {}
    for old_rel, new_rel in moves.items():
        old_fwd = old_rel.replace("\\", "/")
        new_fwd = new_rel.replace("\\", "/")
        inc_map[old_fwd] = new_fwd
        old_base = Path(old_fwd).name
        # Basename mapping; later entries may override — prefer longer path keys applied first.
        if old_base not in inc_map or "/" not in inc_map.get(old_base, ""):
            inc_map[old_base] = new_fwd
    return inc_map


def fix_includes(moves: dict[str, str]) -> int:
    inc_map = build_include_map(moves)
    # Sort keys by length descending for path replacements.
    path_keys = sorted(
        [k for k in inc_map if "/" in k or k.endswith(".h")],
        key=len,
        reverse=True,
    )
    changed = 0
    for fp in SRC.rglob("*"):
        if fp.suffix not in (".h", ".cpp", ".md"):
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text

        def repl(m: re.Match) -> str:
            path = m.group(2).replace("\\", "/")
            if path in inc_map:
                return m.group(1) + inc_map[path] + m.group(3)
            base = Path(path).name
            if base in inc_map and path == base:
                return m.group(1) + inc_map[base] + m.group(3)
            return m.group(0)

        text = INCLUDE_RE.sub(repl, text)
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            changed += 1
    return changed


def update_cmake(moves: dict[str, str]) -> None:
    text = CMAKE.read_text(encoding="utf-8")
    orig = text
    # Longest paths first.
    for old_rel, new_rel in sorted(moves.items(), key=lambda kv: len(kv[0]), reverse=True):
        old_path = "src/" + old_rel.replace("\\", "/")
        new_path = "src/" + new_rel.replace("\\", "/")
        text = text.replace(old_path, new_path)
    # stb special
    text = text.replace("src/stb_image.h", "src/ThirdParty/stb_image.h")
    if text != orig:
        CMAKE.write_text(text, encoding="utf-8", newline="\n")


def cleanup_empty_dirs() -> None:
    for _ in range(10):
        removed = False
        for d in sorted(SRC.rglob("*"), key=lambda p: len(p.parts), reverse=True):
            if d.is_dir() and d != SRC:
                try:
                    if not any(d.iterdir()):
                        d.rmdir()
                        removed = True
                except OSError:
                    pass
        if not removed:
            break


def main() -> int:
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    moves = load_moves()

    if cmd in ("move", "all"):
        print("Preparing case-sensitive folder renames...")
        prepare_case_folders()
        print(f"Moving {len(moves)} files...")
        move_files(moves)
        cleanup_empty_dirs()
        print("Moves done.")

    if cmd in ("includes", "all"):
        n = fix_includes(load_include_moves())
        print(f"Fixed includes in {n} files.")

    if cmd in ("cmake", "all"):
        update_cmake(load_include_moves())
        print("CMakeLists.txt updated.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
