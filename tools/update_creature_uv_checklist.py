#!/usr/bin/env python3
"""Update gate status in docs/CREATURE_UV_CHECKLIST.yaml from validation results."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
CHECKLIST_PATH = ROOT / "docs" / "CREATURE_UV_CHECKLIST.yaml"
sys.path.insert(0, str(TOOLS))

from creature_uv_common import GATE_IDS, now_iso
from generate_creature_uv_checklist import build_entry, summary
from validate_creature_uv import validate_species


def apply_validation(entry: dict[str, Any], result: dict[str, Any]) -> None:
    gates = entry.setdefault("gates", {})
    derived = result.get("gates") or {}
    for gid in GATE_IDS:
        if gid == "G13":
            continue
        status = derived.get(gid, "pending")
        if status in ("pass", "fail", "skip"):
            gates[gid] = {"status": status, "at": now_iso()}
    g13 = derived.get("G13", "pending")
    if g13 == "pass":
        gates["G13"] = {"status": "pass", "at": now_iso()}
    elif all(
        gates.get(g, {}).get("status") in ("pass", "skip")
        for g in GATE_IDS
        if g != "G13" and (g != "G12" or gates.get("G12", {}).get("status") == "skip")
    ):
        gates["G13"] = {"status": "pending"}
    if result.get("failures"):
        entry["last_failures"] = result["failures"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", nargs="+", required=True)
    parser.add_argument("--checklist", type=Path, default=CHECKLIST_PATH)
    parser.add_argument("--research", type=Path, default=Path(r"E:/Work/Home/CubatariumTextureResearch"))
    args = parser.parse_args()

    checklist: dict[str, Any] = {}
    if args.checklist.is_file():
        checklist = yaml.safe_load(args.checklist.read_text(encoding="utf-8")) or {}

    for sid in args.species:
        result = validate_species(sid, args.research)
        entry = build_entry(sid, checklist.get(sid))
        apply_validation(entry, result)
        checklist[sid] = entry
        print(f"updated {sid}: G13={entry['gates'].get('G13', {}).get('status')}")

    checklist["summary"] = summary(checklist)
    args.checklist.write_text(
        yaml.safe_dump(checklist, sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )
    print(f"wrote {args.checklist}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
