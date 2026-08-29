#!/usr/bin/env python3
"""Segment medians for flight perf research (adapted from SRBR)."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "bin/logs/perf_20260829-081522_22660.jsonl"

MED_KEYS = [
    "stream_ms", "mesh_emerge_ms", "mesh_dirty_tick_ms", "relight_drain_ms",
    "unfinished_visual", "visible_black_focus_n", "mesh_dirty_schedule_ok_n",
    "softdefer_capture_retarget_n", "relight_completed_n",
    "mesh_dirty_schedule_skip_orphan_n", "dark_face_stale_near_n",
    "dark_face_void_near_n", "pending_light_focus",
]


def load(p: Path) -> list[dict]:
    return [
        json.loads(line)
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines()
        if line.strip() and json.loads(line).get("kind") == "spike"
    ]


def med(xs: list) -> float | None:
    vals = [float(x) for x in xs if x is not None]
    return round(st.median(vals), 3) if vals else None


def segment(rows: list[dict], name: str) -> list[dict]:
    if name == "ENTER":
        return [r for r in rows if float(r.get("player_y") or 0) <= 10][:30]
    if name == "CRUISE":
        return [r for r in rows if float(r.get("player_y") or 0) > 10]
    return rows


def main() -> int:
    paths = [Path(a) for a in sys.argv[1:]] if len(sys.argv) > 1 else [DEFAULT]
    for p in paths:
        rows = load(p)
        print(f"=== {p.name} spikes={len(rows)} ===")
        for seg in ("ENTER", "CRUISE", "ALL"):
            s = segment(rows, seg)
            print(f"\n## {seg} n={len(s)}")
            for k in MED_KEYS:
                vals = [r.get(k) for r in s]
                print(f"  {k}: med={med(vals)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
