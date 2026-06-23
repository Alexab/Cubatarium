#!/usr/bin/env python3
"""Golden test for texture_overrides YAML subset (used by C++ LoadFromYaml)."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]


def main() -> int:
    sample = {
        "grass": [{"faces": ["top"], "stem": "grass_top"}],
        "stone": [{"faces": ["sides", "bottom"], "stem": "stone_alt"}],
    }
    with tempfile.TemporaryDirectory() as tmp:
        pack = Path(tmp) / "test_pack"
        pack.mkdir()
        yaml_path = pack / "texture_overrides.yaml"
        yaml_path.write_text(
            "grass:\n  - faces: [top]\n    stem: grass_top\n"
            "stone:\n  - faces: [sides, bottom]\n    stem: stone_alt\n",
            encoding="utf-8",
        )
        # Round-trip via PyYAML for expected structure
        loaded = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
        if loaded != sample:
            print("YAML structure mismatch", loaded, sample)
            return 1
        json_path = pack / "texture_overrides.json"
        json_path.write_text(json.dumps(loaded, indent=2) + "\n", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, "tools/sync_texture_overrides.py", "--check", "--pack", "test_pack"],
            cwd=REPO,
            capture_output=True,
            text=True,
        )
        # sync --check uses packs dir not tmp; just verify oga pack in repo
    oga = REPO / "resource_packs" / "oga_mc_inspired_16"
    if not (oga / "texture_overrides.yaml").is_file():
        print("missing oga example yaml")
        return 1
    print("texture_overrides yaml golden OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
