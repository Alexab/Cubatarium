#!/usr/bin/env python3
"""Update creature_resolution_log.yaml after backend audit (checklist reps)."""

from __future__ import annotations

from datetime import date
from pathlib import Path

import yaml

TOOLS = Path(__file__).resolve().parent
LOG = TOOLS / "creature_resolution_log.yaml"

CHECKLIST_BACKEND = {
    "chicken": "bedrock_geo audit rep",
    "sheep": "bedrock_geo audit rep",
    "wolf": "bedrock_geo audit rep",
    "dolphin": "bedrock_geo audit rep",
    "oerkki": "gltf_skinned audit rep",
    "badger": "gltf_skinned audit rep",
    "penguin": "gltf_skinned audit rep",
    "rat": "gltf_skinned audit rep",
    "whale": "gltf_skinned audit rep",
    "fire_spirit": "gltf_sprite_policy rep",
}

TODAY = date.today().isoformat()


def main() -> int:
    data = yaml.safe_load(LOG.read_text(encoding="utf-8")) or {}
    species = data.setdefault("species", {})
    for sid, note in CHECKLIST_BACKEND.items():
        entry = species.setdefault(sid, {})
        entry["backend_audit"] = {
            "status": "tooling_pass",
            "verified_at": TODAY,
            "note": note,
        }
        entry["last_updated"] = TODAY
    LOG.write_text(
        yaml.safe_dump(data, allow_unicode=True, sort_keys=False),
        encoding="utf-8",
    )
    print(f"updated backend_audit for {len(CHECKLIST_BACKEND)} checklist reps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
