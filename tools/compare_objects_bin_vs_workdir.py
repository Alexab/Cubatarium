#!/usr/bin/env python3
"""Compare object JSONs in objects/ and bin/objects/."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKDIR_OBJECTS = ROOT / "objects"
BIN_OBJECTS = ROOT / "bin" / "objects"


def json_files(base: Path) -> dict[str, Path]:
    if not base.is_dir():
        return {}
    out: dict[str, Path] = {}
    for path in sorted(base.rglob("*.json")):
        rel = path.relative_to(base).as_posix()
        out[rel] = path
    return out


def normalized_hash(path: Path) -> str:
    # Normalize json formatting so comparison reflects semantic content.
    data = json.loads(path.read_text(encoding="utf-8"))
    normalized = json.dumps(
        data, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(normalized).hexdigest()


def build_report() -> dict:
    work = json_files(WORKDIR_OBJECTS)
    runtime = json_files(BIN_OBJECTS)
    work_keys = set(work)
    runtime_keys = set(runtime)

    only_in_bin = sorted(runtime_keys - work_keys)
    only_in_workdir = sorted(work_keys - runtime_keys)

    changed_in_both: list[dict[str, str]] = []
    same_in_both: list[str] = []
    for rel in sorted(work_keys & runtime_keys):
        work_hash = normalized_hash(work[rel])
        bin_hash = normalized_hash(runtime[rel])
        if work_hash == bin_hash:
            same_in_both.append(rel)
        else:
            changed_in_both.append(
                {
                    "path": rel,
                    "workdir_hash": work_hash,
                    "bin_hash": bin_hash,
                }
            )

    return {
        "workdir_root": str(WORKDIR_OBJECTS),
        "bin_root": str(BIN_OBJECTS),
        "workdir_count": len(work),
        "bin_count": len(runtime),
        "only_in_bin": only_in_bin,
        "only_in_workdir": only_in_workdir,
        "changed_in_both": changed_in_both,
        "same_in_both_count": len(same_in_both),
    }


def print_summary(report: dict) -> None:
    print(f"workdir_count: {report['workdir_count']}")
    print(f"bin_count: {report['bin_count']}")
    print(f"only_in_bin: {len(report['only_in_bin'])}")
    print(f"only_in_workdir: {len(report['only_in_workdir'])}")
    print(f"changed_in_both: {len(report['changed_in_both'])}")
    print(f"same_in_both_count: {report['same_in_both_count']}")

    if report["only_in_bin"]:
        print("\n[only_in_bin]")
        for rel in report["only_in_bin"]:
            print(rel)
    if report["only_in_workdir"]:
        print("\n[only_in_workdir]")
        for rel in report["only_in_workdir"]:
            print(rel)
    if report["changed_in_both"]:
        print("\n[changed_in_both]")
        for item in report["changed_in_both"]:
            print(item["path"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        help="Optional path to write full JSON report.",
    )
    args = parser.parse_args()

    report = build_report()
    print_summary(report)

    if args.out:
        out_path = Path(args.out)
        if not out_path.is_absolute():
            out_path = ROOT / out_path
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"\nreport_written: {out_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
