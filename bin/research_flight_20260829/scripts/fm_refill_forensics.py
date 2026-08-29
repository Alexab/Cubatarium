#!/usr/bin/env python3
"""Correlate FM queue refill vs schedule starvation on cruise spikes."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "bin/logs/perf_20260829-142846_30244.jsonl"

KEYS = [
    "dirty_fm_n",
    "dirty_n",
    "mesh_dirty_schedule_ok_n",
    "mark_relit_schedule_n",
    "column_loaded_no_mesh_n",
    "first_mesh_schedule_cap",
    "remesh_schedule_cap",
    "mesh_admission_mode",
    "mesh_dirty_schedule_skip_n",
    "mesh_dirty_schedule_skip_locked_n",
    "mesh_dirty_schedule_skip_softdefer_n",
    "mesh_dirty_schedule_skip_pipeline_n",
    "relight_apply_final_n",
    "fm_dirty_enqueue_n",
]


def load(path: Path) -> list[dict]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines()
        if line.strip() and json.loads(line).get("kind") == "spike"
    ]


def med(xs: list) -> float | None:
    vals = [float(x) for x in xs if x is not None]
    return round(st.median(vals), 3) if vals else None


def classify_blocker(r: dict) -> str:
    ok = float(r.get("mesh_dirty_schedule_ok_n") or 0)
    if ok > 0:
        return "scheduled"
    dirty_fm = float(r.get("dirty_fm_n") or 0)
    if dirty_fm <= 0:
        return "empty_fm_queue"
    cap = float(r.get("first_mesh_schedule_cap") or 0)
    if cap <= 0:
        return "zero_fm_cap"
    skips = {
        "skip_locked": float(r.get("mesh_dirty_schedule_skip_locked_n") or 0),
        "skip_softdefer": float(r.get("mesh_dirty_schedule_skip_softdefer_n") or 0),
        "skip_pipeline": float(r.get("mesh_dirty_schedule_skip_pipeline_n") or 0),
        "skip_other": float(r.get("mesh_dirty_schedule_skip_n") or 0),
    }
    top = max(skips, key=skips.get)
    if skips[top] > 0:
        return top
    return "unknown_no_skip"


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    rows = [r for r in load(path) if float(r.get("player_y") or 0) > 10]
    print(f"=== FM refill forensics: {path.name} cruise n={len(rows)} ===\n")
    for k in KEYS:
        vals = [r.get(k) for r in rows if r.get(k) is not None]
        if vals:
            print(f"  {k}: med={med(vals)}")
    sched0 = [r for r in rows if float(r.get("mesh_dirty_schedule_ok_n") or 0) == 0]
    print(f"\nschedule_ok=0 frames: {len(sched0)} ({100*len(sched0)/max(1,len(rows)):.1f}%)")
    blockers: dict[str, int] = {}
    for r in sched0:
        b = classify_blocker(r)
        blockers[b] = blockers.get(b, 0) + 1
    print("dominant blocker when schedule_ok=0:")
    for k, v in sorted(blockers.items(), key=lambda x: -x[1]):
        print(f"  {k}: {v}")
    mark_pos = sum(
        1
        for r in rows
        if float(r.get("mark_relit_schedule_n") or 0) > 0
        and float(r.get("dirty_fm_n") or 0) == 0
    )
    print(f"\nmark_relit_schedule>0 AND dirty_fm_n=0: {mark_pos} frames")
    loaded = sum(
        1
        for r in rows
        if float(r.get("column_loaded_no_mesh_n") or 0) >= 3
        and float(r.get("dirty_fm_n") or 0) == 0
    )
    print(f"column_loaded_no_mesh>=3 AND dirty_fm_n=0: {loaded} frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
