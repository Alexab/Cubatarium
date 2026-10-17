#!/usr/bin/env python3
"""Segment medians ENTER/MOVING/STAND/LATE for SRBR telemetry pack.

Usage:
  python bin/research_srbr_20260826/scripts/segment_medians.py
  python bin/research_srbr_20260826/scripts/segment_medians.py path1.jsonl ...
"""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LOGS = ROOT / "bin" / "logs"

DEFAULTS = [
    ("P13", LOGS / "perf_20260825-170807_29680.jsonl"),
    ("P17bad", LOGS / "perf_20260826-100413_20156.jsonl"),
    ("CUR", LOGS / "perf_20260826-112418_14164.jsonl"),
]

MED_KEYS = [
    "wall_ms",
    "stream_ms",
    "mesh_emerge_ms",
    "mesh_dirty_tick_ms",
    "unfinished_visual",
    "column_loaded_no_mesh_n",
    "chunk_meshed_unlit_hidden",
    "visible_black_focus_n",
    "visible_black_no_ticket_n",
    "pending_light_focus",
    "focus_missing_mesh",
    "dirty_fm_n",
    "dirty_remesh_n",
    "mesh_dirty_schedule_skip_orphan_n",
    "opaque_refs_cpu_vis",
    "keep_cols",
]

RATE_KEYS = [
    "mesh_discarded_late",
    "softdefer_capture_floor_hits",
    "softdefer_capture_retarget_n",
]


def load(p: Path):
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return round(st.median(xs), 3) if xs else None


def rate(rows, key):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)
    return round(d / max(1, len(rows) - 1), 4)


def moving(rows):
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


def longest_stand(rows):
    if not rows:
        return []
    best = (0, 0)
    i = 0
    while i < len(rows):
        x, z = rows[i].get("player_x"), rows[i].get("player_z")
        j = i + 1
        while j < len(rows) and (rows[j].get("player_x"), rows[j].get("player_z")) == (x, z):
            j += 1
        if j - i > best[1] - best[0]:
            best = (i, j)
        i = j
    return rows[best[0] : best[1]]


def land_stand(rows):
    land = [r for r in rows if int(r.get("keep_cols") or 0) >= 160]
    if len(land) < 15:
        return longest_stand(rows)
    best = land
    for i in range(len(land) - 2, -1, -1):
        if (land[i].get("player_x"), land[i].get("player_z")) != (
            land[i + 1].get("player_x"),
            land[i + 1].get("player_z"),
        ):
            best = land[i + 1 :]
            break
    return best if len(best) >= 15 else land


def dump_seg(lab, rows):
    print(f"  --- {lab} n={len(rows)} ---")
    if not rows:
        print("    empty")
        return
    for k in MED_KEYS:
        xs = [r.get(k) for r in rows]
        if all(x is None for x in xs):
            print(f"    {k:36s} med=N/A")
        else:
            print(f"    {k:36s} med={med(xs)}")
    for k in RATE_KEYS:
        print(f"    {k:36s} /spike={rate(rows, k)}")


def analyze(label: str, path: Path):
    u = load(path)
    print(f"\n=== {label} {path.name} spikes={len(u)} ===")
    if not u:
        print("  EMPTY")
        return
    mv = moving(u)
    stand = land_stand(u)
    late = stand[-40:] if len(stand) >= 40 else stand
    enter = u[:80]
    dump_seg("ENTER first80", enter)
    dump_seg("MOVING", mv)
    dump_seg("STAND land keep>=160 trailing", stand)
    dump_seg("LATE stand last40", late)


def main():
    if len(sys.argv) > 1:
        for a in sys.argv[1:]:
            analyze(Path(a).stem, Path(a))
    else:
        for lab, p in DEFAULTS:
            if p.is_file():
                analyze(lab, p)
            else:
                print(f"MISSING {lab} {p}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
