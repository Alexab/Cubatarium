#!/usr/bin/env python3
"""Regenerate creature assets: import Luanti textures when sources exist."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
IMPORT_SCRIPT = ROOT / "tools" / "import_luanti_creature_textures.py"
CATALOG_SCRIPT = ROOT / "tools" / "generate_luanti_creature_catalog.py"


def main() -> None:
    if RESEARCH.is_dir() and (RESEARCH / "mobs_animal").is_dir():
        subprocess.run([sys.executable, str(IMPORT_SCRIPT)], check=True)
    else:
        print(
            "Luanti sources not found; regenerating procedural placeholders.\n"
            f"Clone mods into {RESEARCH} and run:\n"
            f"  python {IMPORT_SCRIPT.name} --download",
            file=sys.stderr,
        )
        subprocess.run([sys.executable, str(CATALOG_SCRIPT)], check=True)


if __name__ == "__main__":
    main()
