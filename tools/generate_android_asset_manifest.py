#!/usr/bin/env python3
"""Generate Android asset_manifest.json with sha256 for critical game assets."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ASSETS = ROOT / "platforms" / "android" / "app" / "src" / "main" / "assets"

CRITICAL_RELATIVE_PATHS = (
    "fonts/Roboto-Regular.ttf",
    "shaders/gles/vshader_2d.glsl",
    "content/types.json",
    "config.json",
)


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(8192), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=DEFAULT_ASSETS,
        help="Android assets root (default: platforms/android/.../assets)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output manifest path (default: <assets-dir>/asset_manifest.json)",
    )
    args = parser.parse_args()
    assets_dir: Path = args.assets_dir
    output: Path = args.output or (assets_dir / "asset_manifest.json")

    files: dict[str, str] = {}
    missing: list[str] = []
    for relative in CRITICAL_RELATIVE_PATHS:
        path = assets_dir / relative
        if not path.is_file():
            missing.append(relative)
            continue
        files[relative.replace("\\", "/")] = sha256_hex(path)

    if missing:
        print("WARN missing critical assets:")
        for item in missing:
            print(f"  - {item}")

    manifest = {"files": files}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"OK wrote {len(files)} entries -> {output}")
    return 0 if not missing else 1


if __name__ == "__main__":
    raise SystemExit(main())
