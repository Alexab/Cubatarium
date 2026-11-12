#!/usr/bin/env python3
"""Launcher for the creaturegen package.

The package lives in tools/creaturegen/. This file stays a script so
`python tools/creaturegen.py` does not shadow the package import.
"""

from __future__ import annotations

import runpy
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
# Drop this script's name from the import path by running the package main
# through its file after the package directory is the one Python binds.
sys.path.insert(0, str(TOOLS / "creaturegen"))


if __name__ == "__main__":
    # Import the package explicitly from its directory before this filename.
    sys.modules.pop("creaturegen", None)
    runpy.run_path(str(TOOLS / "creaturegen" / "__main__.py"), run_name="__main__")
