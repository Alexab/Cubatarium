#!/usr/bin/env python3
"""Orchestrate derive → bake → validate → checklist for creature UV rollout."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from creature_uv_common import (
    RESEARCH_DEFAULT,
    WAVE_SPECIES,
    gate_skips,
    is_placeholder,
    list_all_species,
    load_creature,
    upstream_exists,
)
from update_creature_uv_checklist import apply_validation
from validate_creature_uv import validate_species

import yaml

from generate_creature_uv_checklist import build_entry, summary


def run_cmd(cmd: list[str], *, cwd: Path = ROOT, check: bool = True) -> subprocess.CompletedProcess:
    print("+", " ".join(cmd))
    return subprocess.run(cmd, cwd=cwd, check=check)


def gate_g01(species_id: str, research: Path) -> tuple[bool, str]:
    if upstream_exists(species_id, research):
        return True, "pass"
    if is_placeholder(species_id):
        run_cmd(
            [sys.executable, str(TOOLS / "import_luanti_creature_textures.py"), "--species", species_id],
            check=False,
        )
        if upstream_exists(species_id, research):
            return True, "pass"
    return False, "fail"


def gate_g03(species_id: str, max_retries: int) -> tuple[bool, str]:
    if "G03" in gate_skips(species_id):
        return True, "skip"
    for attempt in range(max_retries + 1):
        proc = run_cmd(
            [
                sys.executable,
                str(TOOLS / "derive_rigid_parts_v2.py"),
                "--species",
                species_id,
                "--compare",
            ],
            check=False,
        )
        if proc.returncode == 0:
            return True, "pass"
        if attempt < max_retries:
            run_cmd(
                [
                    sys.executable,
                    str(TOOLS / "derive_rigid_parts_v2.py"),
                    "--species",
                    species_id,
                    "--write",
                ],
                check=False,
            )
    return False, "fail"


def gate_g04(species_id: str) -> tuple[bool, str]:
    if "G04" in gate_skips(species_id):
        return True, "skip"
    creature = load_creature(species_id)
    layout = creature.get("visual", {}).get("texture_layout", "")
    return layout == "box_uv", "pass" if layout == "box_uv" else "fail"


def gate_g05(species_id: str, research: Path) -> tuple[bool, str]:
    if "G05" in gate_skips(species_id):
        return True, "skip"
    log_path = ROOT / "bin" / "bake_log.txt"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8") as log:
        proc = subprocess.run(
            [
                sys.executable,
                str(TOOLS / "bake_rigid_creature_textures.py"),
                "--species",
                species_id,
                "--research",
                str(research),
            ],
            cwd=ROOT,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    if proc.returncode != 0:
        return False, "fail"
    if "legacy fallback" in text:
        return False, "fail"
    return True, "pass"


def gate_g10(species_id: str) -> tuple[bool, str]:
    lic = ROOT / "models" / "creatures" / species_id / "LICENSE.txt"
    if not lic.is_file():
        return False, "fail"
    if "Placeholder procedural" in lic.read_text(encoding="utf-8", errors="ignore"):
        return False, "fail"
    return True, "pass"


def gate_g11(species_id: str) -> tuple[bool, str]:
    proc = run_cmd([sys.executable, str(TOOLS / "audit_creature_catalog.py")], check=False)
  # audit all; check species icon via re-validate
    from audit_creature_catalog import icon_quality

    iq = icon_quality(ROOT / "models" / "creatures" / species_id)
    ok = iq not in ("solid_color", "missing")
    return ok, "pass" if ok else "fail"


def gate_g12(species_id: str, skip_preview: bool) -> tuple[bool, str]:
    if skip_preview or "G12" in gate_skips(species_id):
        return True, "skip"
    exe = ROOT / "bin" / "Cubatarium.exe"
    if not exe.is_file():
        return True, "skip"
    out = ROOT / "bin" / "uv_preview" / species_id
    proc = run_cmd(
        [str(exe), "--creature-preview-smoke", "--species", species_id, "--out-dir", str(out.parent)],
        check=False,
    )
    if proc.returncode != 0:
        return False, "fail"
    proc2 = run_cmd(
        [
            sys.executable,
            str(TOOLS / "compare_creature_preview.py"),
            "--species",
            species_id,
            "--fail-on-diff",
        ],
        check=False,
    )
    return proc2.returncode == 0, "pass" if proc2.returncode == 0 else "fail"


def run_species(
    species_id: str,
    *,
    research: Path,
    fail_fast: bool,
    skip_preview: bool,
    max_retries: int,
) -> bool:
    print(f"\n=== {species_id} ===")
    if species_id == "human":
        print("skip human (separate track)")
        return True

    ok, _ = gate_g01(species_id, research)
    if not ok:
        print(f"FAIL G01 {species_id}")
        return False

    ok, _ = gate_g03(species_id, max_retries)
    if not ok and fail_fast:
        print(f"FAIL G03 {species_id}")
        return False

    ok, _ = gate_g04(species_id)
    if not ok:
        print(f"FAIL G04 {species_id} (needs box_uv migration)")
        if fail_fast:
            return False

    ok, _ = gate_g05(species_id, research)
    if not ok:
        print(f"FAIL G05 {species_id}")
        if fail_fast:
            return False

    result = validate_species(species_id, research)
    if not result["pass"] and fail_fast:
        print(f"FAIL G06 {species_id}: {result['failures']}")
        return False

    ok, _ = gate_g10(species_id)
    if not ok and is_placeholder(species_id):
        print(f"FAIL G10 placeholder {species_id}")
        if fail_fast:
            return False

    ok, _ = gate_g11(species_id)
    if not ok and fail_fast:
        print(f"FAIL G11 {species_id}")
        return False

    gate_g12(species_id, skip_preview)

    result = validate_species(species_id, research)

    checklist_path = ROOT / "docs" / "CREATURE_UV_CHECKLIST.yaml"
    checklist: dict = {}
    if checklist_path.is_file():
        checklist = yaml.safe_load(checklist_path.read_text(encoding="utf-8")) or {}
    entry = build_entry(species_id, checklist.get(species_id))
    apply_validation(entry, result)
    if result["pass"] and gate_g10(species_id)[0] and gate_g11(species_id)[0]:
        entry["gates"]["G13"] = {"status": "pass", "at": entry["gates"].get("G06", {}).get("at", "")}
    checklist[species_id] = entry
    checklist["summary"] = summary(checklist)
    checklist_path.write_text(yaml.safe_dump(checklist, sort_keys=False, allow_unicode=True), encoding="utf-8")
    passed = entry["gates"].get("G13", {}).get("status") == "pass"
    print(f"{'DONE' if passed else 'INCOMPLETE'} {species_id}")
    return passed


def git_commit(message: str, paths: list[str]) -> None:
    exclude = ("bin/", "*.log")
    run_cmd(["git", "status", "--short"], check=False)
    add_args = ["git", "add"]
    for p in paths:
        add_args.append(p)
    run_cmd(add_args, check=False)
    proc = run_cmd(["git", "commit", "-m", message], check=False)
    if proc.returncode == 0:
        print(f"committed: {message}")
    else:
        print("commit skipped (nothing to commit or hook failed)")


def commit_wave(wave: str, species_done: list[str]) -> None:
    names = ", ".join(species_done)
    if wave in ("W1", "W2", "W3"):
        subject = f"fix(textures): tier A wave {wave} box_uv rollout ({names})"
    else:
        subject = f"fix(textures): creature UV wave {wave} box_uv rollout ({names})"
    git_commit(
        subject,
        [
            "models/creatures",
            "docs/CREATURE_UV_CHECKLIST.yaml",
            "tools/uv_quality_report.yaml",
            "tools/creature_rigid_parts.yaml",
        ],
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", nargs="+")
    parser.add_argument("--wave")
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--skip-preview", action="store_true")
    parser.add_argument("--max-retries", type=int, default=2)
    parser.add_argument("--commit", action="store_true")
    parser.add_argument("--commit-wave", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.wave:
        species_list = WAVE_SPECIES.get(args.wave, [])
        if not species_list:
            raise SystemExit(f"unknown wave {args.wave}")
    elif args.species:
        species_list = args.species
    else:
        raise SystemExit("Specify --species or --wave")

    if args.dry_run:
        print("would run:", species_list)
        return 0

    done: list[str] = []
    failures = 0
    for sid in species_list:
        if sid == "human":
            continue
        ok = run_species(
            sid,
            research=args.research,
            fail_fast=args.fail_fast,
            skip_preview=args.skip_preview,
            max_retries=args.max_retries,
        )
        if ok:
            done.append(sid)
            if args.commit:
                git_commit(
                    f"fix(textures): {sid} box_uv stems and baked unfold",
                    [f"models/creatures/{sid}", "docs/CREATURE_UV_CHECKLIST.yaml"],
                )
        else:
            failures += 1
            if args.fail_fast:
                break

    if args.commit_wave and args.wave and done:
        commit_wave(args.wave, done)

    run_cmd(
        [sys.executable, str(TOOLS / "validate_creature_uv.py"), "--write-report"]
        + (["--species"] + species_list if args.species else ["--wave", args.wave] if args.wave else ["--all"]),
        check=False,
    )

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
