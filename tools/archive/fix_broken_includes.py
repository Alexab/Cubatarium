#!/usr/bin/env python3
"""Fix include paths corrupted by overly aggressive U-stripping."""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"

FIXES = {
    '"iSettings.h"': '"UiSettings.h"',
    '"tils.h"': '"Utils.h"',
    '"ser.h"': '"User.h"',
    '"iQuadBatch.h"': '"UiQuadBatch.h"',
    '"iTexturedQuadBatch.h"': '"UiTexturedQuadBatch.h"',
    '"Gui/iQuadBatch.h"': '"Gui/UiQuadBatch.h"',
    '"Gui/iTexturedQuadBatch.h"': '"Gui/UiTexturedQuadBatch.h"',
}


def main():
    n = 0
    for fp in ROOT.rglob("*"):
        if fp.suffix not in (".h", ".cpp"):
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text
        for old, new in FIXES.items():
            text = text.replace(f"#include {old}", f"#include {new}")
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(f"Fixed {n} files")


if __name__ == "__main__":
    main()
