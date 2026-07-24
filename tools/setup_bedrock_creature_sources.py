#!/usr/bin/env python3
"""Download Bedrock geo.json files from Mojang/bedrock-samples into _sources."""

from __future__ import annotations

import argparse
import shutil
import sys
import urllib.request
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
SOURCES = ROOT / "models" / "creatures" / "_sources" / "bedrock_geo"
CATALOG = TOOLS / "bedrock_geo_catalog.yaml"
BASE_URL = (
    "https://raw.githubusercontent.com/Mojang/bedrock-samples/main/"
    "resource_pack/models/entity"
)

try:
    import yaml
except ImportError:
    yaml = None


def load_catalog() -> dict:
    if yaml is None:
        raise SystemExit("PyYAML required: pip install pyyaml")
    data = yaml.safe_load(CATALOG.read_text(encoding="utf-8"))
    return data.get("species", {})


def download_geo(filename: str, dest: Path) -> bool:
    url = f"{BASE_URL}/{filename}"
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            dest.write_bytes(resp.read())
        return True
    except Exception as exc:
        print(f"FAIL download {filename}: {exc}")
        return False


def copy_vmv_geo(filename: str, dest: Path) -> bool:
    vmv = Path(r"E:/Work/Home/Vanilla-Mob-Variants/models/entity") / filename
    if vmv.is_file():
        shutil.copy2(vmv, dest)
        print(f"OK VMV {filename}")
        return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--species", action="append", help="Only these species")
    args = parser.parse_args()

    SOURCES.mkdir(parents=True, exist_ok=True)
    catalog = load_catalog()
    wanted = set(args.species) if args.species else set(catalog)

    ok = 0
    for species_id, entry in catalog.items():
        if species_id not in wanted:
            continue
        src_name = entry.get("geometry_source", f"{species_id}.geo.json")
        dest = SOURCES / src_name
        if dest.is_file():
            print(f"SKIP exists {src_name}")
            ok += 1
            continue
        if download_geo(src_name, dest):
            print(f"OK bedrock-samples {src_name}")
            ok += 1
        elif copy_vmv_geo(src_name, dest):
            ok += 1
        else:
            alt = src_name.replace(".geo.json", ".v1.8.geo.json")
            if download_geo(alt, dest):
                print(f"OK bedrock-samples {alt}")
                ok += 1

    print(f"setup_bedrock_creature_sources: {ok}/{len(wanted)} geo files ready")
    return 0 if ok == len(wanted) else 1


if __name__ == "__main__":
    sys.exit(main())
