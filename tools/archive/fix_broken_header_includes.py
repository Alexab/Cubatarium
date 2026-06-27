#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "src"
FIXES = {
    '"Ui.h"': '"UiSettings.h"',
    '"Render.h"': '"RenderSettings.h"',
    '"ProceduralTemplate.h"': '"ProceduralSettings.h"',
}

def main():
    n = 0
    for fp in ROOT.rglob("*"):
        if fp.suffix not in (".h", ".cpp"):
            continue
        text = fp.read_text(encoding="utf-8")
        orig = text
        for a, b in FIXES.items():
            text = text.replace(f"#include {a}", f"#include {b}")
        if text != orig:
            fp.write_text(text, encoding="utf-8", newline="\n")
            n += 1
    print(n)

if __name__ == "__main__":
    main()
