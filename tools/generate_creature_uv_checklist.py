#!/usr/bin/env python3
"""Generate docs/CREATURE_UV_CHECKLIST.yaml for all 51 species."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
DOCS = ROOT / "docs"
CHECKLIST_PATH = DOCS / "CREATURE_UV_CHECKLIST.yaml"
sys.path.insert(0, str(TOOLS))

from creature_tier_a import TIER_A_MOBS
from creature_uv_common import (
    GATE_IDS,
    WAVE_MAP,
    gate_skips,
    has_snout_part,
    is_placeholder,
    list_all_species,
    load_audit,
    load_creature,
    load_sources,
    parts_count,
    threshold_profile,
)


def build_entry(species_id: str, existing: dict | None = None) -> dict[str, Any]:
    creature = load_creature(species_id)
    visual = creature.get("visual", {})
    sources = load_sources()
    spec = (sources.get("species") or {}).get(species_id) or {}
    audit = (load_audit().get("species") or {}).get(species_id) or {}
    wave = WAVE_MAP.get(species_id, "?")
    tier = "A" if species_id in TIER_A_MOBS else ("0" if species_id == "human" else "B")
    skips = gate_skips(species_id, creature)
    g09 = next(
        (p["id"] for p in visual.get("parts", []) if p.get("id") in ("snout", "beak")),
        None,
    )
    pre_steps: list[str] = []
    if is_placeholder(species_id):
        pre_steps.append("G01_import")
    if "composite" in spec:
        pre_steps.append("composite_import")

    gates: dict[str, Any] = {}
    prev = (existing or {}).get("gates") or {}
    for gid in GATE_IDS:
        if gid in prev and isinstance(prev[gid], dict):
            gates[gid] = prev[gid]
        else:
            gates[gid] = {"status": "pending"}

    entry: dict[str, Any] = {
        "wave": wave,
        "tier": tier,
        "archetype": creature.get("locomotion_archetype") or audit.get("archetype", ""),
        "layout_current": visual.get("texture_layout", ""),
        "layout_target": "player_skin_atlas" if species_id == "human" else "box_uv",
        "b3d": spec.get("model"),
        "placeholder": is_placeholder(species_id),
        "parts_count": parts_count(creature),
        "threshold_profile": threshold_profile(species_id, creature),
        "gate_skips": skips,
        "g09_part": g09,
        "pre_steps": pre_steps,
        "gates": gates,
        "command": f"python tools/run_creature_uv_wave.py --species {species_id} --fail-fast",
    }
    if existing and existing.get("notes"):
        entry["notes"] = existing["notes"]
    if existing and existing.get("escalation"):
        entry["escalation"] = existing["escalation"]
    return entry


def summary(checklist: dict) -> dict[str, Any]:
    species = {k: v for k, v in checklist.items() if k != "summary" and isinstance(v, dict)}
    done = sum(1 for v in species.values() if (v.get("gates") or {}).get("G13", {}).get("status") == "pass")
    by_wave: dict[str, list[int]] = {}
    for sid, entry in species.items():
        w = entry.get("wave", "?")
        by_wave.setdefault(w, [0, 0])
        by_wave[w][1] += 1
        if (entry.get("gates") or {}).get("G13", {}).get("status") == "pass":
            by_wave[w][0] += 1
    return {
        "total": len(species),
        "done": done,
        "pending": len(species) - done,
        "by_wave": {w: {"done": d, "total": t} for w, (d, t) in sorted(by_wave.items())},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", action="store_true", help="Print progress only")
    parser.add_argument("--out", type=Path, default=CHECKLIST_PATH)
    args = parser.parse_args()

    existing: dict = {}
    if args.out.is_file():
        existing = yaml.safe_load(args.out.read_text(encoding="utf-8")) or {}

    checklist: dict[str, Any] = {}
    for sid in list_all_species():
        checklist[sid] = build_entry(sid, existing.get(sid))

    checklist["summary"] = summary(checklist)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(
        yaml.safe_dump(checklist, sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )
    print(f"wrote {args.out}")
    if args.summary or True:
        s = checklist["summary"]
        print(f"progress: {s['done']}/{s['total']} done")
        for w, info in s.get("by_wave", {}).items():
            print(f"  {w}: {info['done']}/{info['total']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
