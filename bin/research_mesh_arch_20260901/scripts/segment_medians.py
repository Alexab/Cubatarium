#!/usr/bin/env python3
"""Segment medians FLY / STOP / ENTER for mesh arch research pack."""

from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "bin" / "logs"

DEFAULTS = [
    ("H0-manual", LOGS / "perf_20260901-124859_22296.jsonl"),
    ("H0-autofly", LOGS / "perf_20260901-182316_13160.jsonl"),
]

MED_KEYS = [
    "wall_ms",
    "stream_ms",
    "mesh_emerge_ms",
    "mesh_snapshot_ms",
    "mesh_dirty_schedule_ms",
    "mesh_dirty_drain_ms",
    "mesh_dirty_gpu_ms",
    "mesh_immediate_ms",
    "unfinished_visual",
    "dirty_fm_n",
    "mesh_dirty_schedule_ok_n",
    "pool_unsync_uploads",
]


def load(p: Path) -> list[dict]:
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs: list) -> float | None:
    vals = [float(x) for x in xs if x is not None]
    return round(st.median(vals), 3) if vals else None


def segment(rows: list[dict], name: str) -> list[dict]:
    if name == "fly":
        out = []
        for i, r in enumerate(rows):
            if i == 0:
                continue
            if (r.get("player_x"), r.get("player_z")) != (
                rows[i - 1].get("player_x"),
                rows[i - 1].get("player_z"),
            ):
                out.append(r)
        return out
    if name == "stop":
        out = []
        for i, r in enumerate(rows):
            if i == 0:
                continue
            if (r.get("player_x"), r.get("player_z")) == (
                rows[i - 1].get("player_x"),
                rows[i - 1].get("player_z"),
            ):
                out.append(r)
        return out
    if name == "enter":
        return rows[: min(12, len(rows))]
    return rows


def report(label: str, path: Path) -> None:
    if not path.is_file():
        print(f"SKIP {label}: missing {path}")
        return
    rows = load(path)
    print(f"\n=== {label} ({path.name}) periods={len(rows)} ===")
    for seg in ("enter", "fly", "stop"):
        seg_rows = segment(rows, seg)
        print(f"  [{seg}] n={len(seg_rows)}")
        for k in MED_KEYS:
            v = med([r.get(k) for r in seg_rows])
            if v is not None:
                print(f"    {k}: {v}")


def main() -> int:
    paths = sys.argv[1:] if len(sys.argv) > 1 else None
    if paths:
        for p in paths:
            report(Path(p).stem, Path(p))
        return 0
    for label, p in DEFAULTS:
        report(label, p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
