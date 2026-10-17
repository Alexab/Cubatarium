#!/usr/bin/env python3
"""Why hold_n never accumulates on 215411."""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

p = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260817-215411_29916.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
cruise = [r for r in rows if (r.get("chunk_count") or 0) >= 80 and r.get("kind") != "period"]
# kind filter: many lines are spike+period duplicates? audit used chunk_count>=80 including period
# Use unique by... actually audit included period rows which duplicate spikes.
# Filter kind in ('frame','') or not period.

frames = [r for r in rows if r.get("kind") not in ("period",)]
cruise = [r for r in frames if (r.get("chunk_count") or 0) >= 80]
print("frames", len(cruise), "kinds", Counter(r.get("kind") for r in cruise))

nh2 = [r for r in cruise if (r.get("miss_horiz") or 99) <= 2 and r.get("focus_missing_mesh")]
print("miss nh<=2", len(nh2), "/", sum(1 for r in cruise if r.get("focus_missing_mesh")))
print("  hold", Counter(r.get("relight_witness_hold_n") for r in nh2))
print("  pending_light_n>0", sum(1 for r in nh2 if (r.get("pending_light_n") or 0) > 0))
print("  pending_light_focus", Counter(r.get("pending_light_focus") for r in nh2).most_common(8))
print("  cap_h vs miss_h same", sum(1 for r in nh2 if r.get("relight_capture_col_horiz") == r.get("miss_horiz")))
print("  cap_h==miss column approx: capture horiz vs miss horiz",
      Counter((r.get("relight_capture_col_horiz"), r.get("miss_horiz")) for r in nh2).most_common(10))
print("  finalize on nh2 miss", sum(1 for r in nh2 if r.get("relight_capture_finalize")))
print("  apply on nh2", Counter(r.get("relight_apply_n") for r in nh2).most_common())

# consecutive same miss
hold_run = 0
max_run = 0
prev = None
for r in cruise:
    key = (r.get("miss_cx"), r.get("miss_cz")) if r.get("focus_missing_mesh") else None
    if key and key == prev:
        hold_run += 1
    else:
        max_run = max(max_run, hold_run)
        hold_run = 1 if key else 0
    prev = key
max_run = max(max_run, hold_run)
print("max consecutive same miss cx/cz", max_run)

print("admission_mode", Counter(r.get("mesh_admission_mode") for r in cruise).most_common())
print("first_mesh_schedule_cap", Counter(r.get("first_mesh_schedule_cap") for r in cruise).most_common())
print("schedule_ok vs skip on nh2", 
      "ok med", sorted(r["mesh_dirty_schedule_ok_n"] for r in nh2 if isinstance(r.get("mesh_dirty_schedule_ok_n"), int))[len(nh2)//2] if nh2 else None)

# capture horiz 4 with finalize 1
bad = [r for r in cruise if (r.get("relight_capture_col_horiz") or -1) >= 3 and r.get("relight_capture_finalize") == 1]
print("finalize=1 AND cap_h>=3", len(bad), "horiz", Counter(r.get("relight_capture_col_horiz") for r in bad))

print("visible_black med", sorted(r["visible_black_focus_n"] for r in cruise if isinstance(r.get("visible_black_focus_n"), int))[len(cruise)//2])
print("chunk_meshed_unlit_hidden med", sorted((r.get("chunk_meshed_unlit_hidden") or 0) for r in cruise)[len(cruise)//2])
print("column_meshing med", sorted((r.get("column_meshing_n") or 0) for r in cruise)[len(cruise)//2])
print("column_lighting med", sorted((r.get("column_lighting_n") or 0) for r in cruise)[len(cruise)//2])
