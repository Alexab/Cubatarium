#!/usr/bin/env python3
"""Generate content/worldgen_refs.json from tools/canonical_blocks.yaml."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
CANONICAL_PATH = REPO / "tools" / "canonical_blocks.yaml"
DEFAULT_OUT = REPO / "content" / "worldgen_refs.json"

# Mirrors legacy fallbacks in WorldGenContext::ResolveBlockIds.
SLOT_FALLBACKS: dict[str, str] = {
    "gravel": "stone",
    "snow": "stone",
    "sand": "sandstone",
    "dirt": "stone",
}


def load_canonical(path: Path) -> dict:
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def build_slots(canonical: dict) -> dict:
    slots: dict[str, dict] = {}
    tier_a = canonical.get("tier_a", {})
    for block_name, spec in tier_a.items():
        refs = spec.get("worldgen_refs") or [block_name]
        if isinstance(refs, str):
            refs = [refs]
        entry: dict = {"block_names": list(refs)}
        fallback = SLOT_FALLBACKS.get(block_name)
        if fallback:
            entry["fallback_slot"] = fallback
        slots[block_name] = entry
    return slots


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate worldgen_refs.json")
    parser.add_argument("--canonical", type=Path, default=CANONICAL_PATH)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    canonical = load_canonical(args.canonical.resolve())
    doc = {
        "schema_version": 1,
        "slots": build_slots(canonical),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.output} ({len(doc['slots'])} slots)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
