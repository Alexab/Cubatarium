#!/usr/bin/env python3
"""Sync and validate creature_resolution_log.yaml; emit CREATURE_RESOLUTION_LOG.md."""

from __future__ import annotations

import argparse
import json
import sys
from datetime import date
from pathlib import Path

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
MODELS = ROOT / "models" / "creatures"
LOG_PATH = TOOLS / "creature_resolution_log.yaml"
DOCS = ROOT / "docs"
REPORT_PATH = DOCS / "CREATURE_RESOLUTION_LOG.md"
DIAGNOSIS_PATH = TOOLS / "creature_spawn_diagnosis.yaml"

DEFAULT_CHECK = {
    "status": "pending",
    "scenario": "",
    "verified_at": None,
    "note": "",
}


def load_creature_ids() -> list[str]:
    return sorted(
        p.name
        for p in MODELS.iterdir()
        if p.is_dir() and (p / "creature.json").is_file()
    )


def default_entry(species_id: str) -> dict:
    creature = json.loads((MODELS / species_id / "creature.json").read_text(encoding="utf-8"))
    habitat = creature.get("habitat", "terrestrial")
    spawnable = bool(creature.get("catalog", {}).get("spawnable", False))
    scenario = {
        "terrestrial": "terrestrial_grass",
        "aquatic": "ocean_deep",
        "aerial": "open_sky",
        "amphibious": "beach_or_water",
        "lava": "lava_pool",
    }.get(habitat, "")
    entry = {
        "spawnable": spawnable,
        "habitat": habitat,
        "spawn": {**DEFAULT_CHECK, "scenario": scenario},
        "wander": {**DEFAULT_CHECK, "scenario": scenario},
        "silhouette": {**DEFAULT_CHECK, "score": None},
        "icon": {**DEFAULT_CHECK},
        "issues_open": [],
        "issues_closed": [],
        "last_updated": None,
    }
    return entry


def init_log() -> dict:
    return {"species": {sid: default_entry(sid) for sid in load_creature_ids()}}


def load_log() -> dict:
    if not LOG_PATH.is_file():
        return init_log()
    data = yaml.safe_load(LOG_PATH.read_text(encoding="utf-8")) or {}
    species = data.get("species") or {}
    for sid in load_creature_ids():
        if sid not in species:
            species[sid] = default_entry(sid)
        else:
            for key in ("spawn", "wander", "silhouette", "icon"):
                species[sid].setdefault(key, {**DEFAULT_CHECK})
            species[sid].setdefault("issues_open", [])
            species[sid].setdefault("issues_closed", [])
    data["species"] = species
    return data


def merge_diagnosis(log: dict) -> None:
    if not DIAGNOSIS_PATH.is_file():
        return
    diag = yaml.safe_load(DIAGNOSIS_PATH.read_text(encoding="utf-8")) or {}
    for sid, row in (diag.get("species") or {}).items():
        entry = log["species"].get(sid)
        if not entry or not entry.get("spawnable"):
            continue
        spawn = entry.get("spawn", {})
        if spawn.get("status") in ("pass", "fail"):
            continue
        for issue in row.get("issues_suggested") or []:
            if issue not in entry["issues_open"]:
                entry["issues_open"].append(issue)


def write_report(log: dict) -> None:
    species = log["species"]
    spawnable = [s for s, e in species.items() if e.get("spawnable")]
    counts = {"spawn": {}, "wander": {}, "silhouette": {}, "icon": {}}
    for field in counts:
        for sid in spawnable:
            st = species[sid].get(field, {}).get("status", "pending")
            counts[field][st] = counts[field].get(st, 0) + 1

    lines = [
        "# Creature resolution log",
        "",
        f"Source: `tools/creature_resolution_log.yaml` (updated {date.today().isoformat()})",
        "",
        "## Summary (spawnable only)",
        "",
        f"- Species: **{len(spawnable)}**",
    ]
    for field, c in counts.items():
        lines.append(f"- {field}: " + ", ".join(f"{k}={v}" for k, v in sorted(c.items())))

    open_issues = [
        (sid, e["issues_open"])
        for sid, e in species.items()
        if e.get("spawnable") and e.get("issues_open")
    ]
    lines.extend(["", "## Open issues", ""])
    if not open_issues:
        lines.append("None.")
    else:
        for sid, issues in sorted(open_issues):
            lines.append(f"- **{sid}**: {', '.join(issues)}")

    lines.extend(
        [
            "",
            "## Per species",
            "",
            "| id | spawn | wander | silhouette | icon | open |",
            "|----|-------|--------|------------|------|------|",
        ]
    )
    for sid in sorted(species.keys()):
        e = species[sid]
        if not e.get("spawnable"):
            continue
        lines.append(
            f"| {sid} | {e['spawn'].get('status')} | {e['wander'].get('status')} "
            f"| {e['silhouette'].get('status')} | {e['icon'].get('status')} "
            f"| {len(e.get('issues_open') or [])} |"
        )
    REPORT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def validate(log: dict, require_spawn_pass: bool) -> int:
    errors = []
    for sid, entry in log["species"].items():
        if not entry.get("spawnable"):
            continue
        if require_spawn_pass and entry.get("spawn", {}).get("status") != "pass":
            errors.append(f"{sid}: spawn={entry.get('spawn', {}).get('status')}")
        if entry.get("issues_open"):
            errors.append(f"{sid}: open={entry['issues_open']}")
    if errors:
        print("Resolution gate FAILED:")
        for e in errors:
            print(f"  {e}")
        return 1
    print("Resolution gate OK")
    return 0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--init", action="store_true", help="Rewrite log from catalog")
    parser.add_argument("--require-spawn-pass", action="store_true")
    parser.add_argument(
        "--mark-spawn-pass",
        nargs="*",
        metavar="SPECIES",
        help="Mark spawn pass for species (runtime fix applied)",
    )
    args = parser.parse_args()

    if args.init or not LOG_PATH.is_file():
        log = init_log()
    else:
        log = load_log()

    merge_diagnosis(log)

    if args.mark_spawn_pass:
        today = date.today().isoformat()
        for sid in args.mark_spawn_pass:
            if sid not in log["species"]:
                print(f"WARN unknown species {sid}")
                continue
            e = log["species"][sid]
            e["spawn"]["status"] = "pass"
            e["spawn"]["verified_at"] = today
            closed = {c.get("id") for c in e.get("issues_closed") or [] if isinstance(c, dict)}
            remaining = []
            for issue in e.get("issues_open") or []:
                if issue in ("spawn_probe", "spawn_habitat", "aquatic_depth", "aerial_lift"):
                    if issue not in closed:
                        e.setdefault("issues_closed", []).append(
                            {"id": issue, "closed_at": today, "note": "runtime spawn fix"}
                        )
                else:
                    remaining.append(issue)
            e["issues_open"] = remaining
            e["last_updated"] = today

    LOG_PATH.write_text(
        yaml.safe_dump(log, sort_keys=False, allow_unicode=True),
        encoding="utf-8",
    )
    write_report(log)
    print(f"Wrote {LOG_PATH.name} and {REPORT_PATH.name}")
    sys.exit(validate(log, args.require_spawn_pass))


if __name__ == "__main__":
    main()
