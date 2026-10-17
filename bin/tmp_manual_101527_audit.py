#!/usr/bin/env python3
"""Segment analysis for manual west flight (rim/streaming lag)."""
from __future__ import annotations
import json
import statistics as st
from pathlib import Path

path = Path("bin/logs/perf_20260917-101527_16664.jsonl")
rows = []
for line in path.read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    o = json.loads(line)
    if o.get("kind") == "period":
        rows.append(o)

def med(xs):
    xs = [x for x in xs if x is not None]
    return st.median(xs) if xs else None

def g(r, *keys, default=None):
    for k in keys:
        if k in r and r[k] is not None:
            return r[k]
    return default

print(f"periods={len(rows)}")
if not rows:
    raise SystemExit(1)

focus = [(g(r, "focus_cx"), g(r, "focus_cz"), g(r, "player_y"), g(r, "movement_speed")) for r in rows]
print(f"focus_start=({focus[0][0]},{focus[0][1]}) y={focus[0][2]} spd={focus[0][3]}")
print(f"focus_end=({focus[-1][0]},{focus[-1][1]}) y={focus[-1][2]} spd={focus[-1][3]}")
cxs = [f[0] for f in focus if f[0] is not None]
print(f"focus_cx_min={min(cxs)} focus_cx_max={max(cxs)}")

# Split: early land, mid, west rim (cx<=-3), stop (speed<=2)
def seg(name, pred):
    s = [r for r in rows if pred(r)]
    keys = [
        ("unfinished_visual", "unfinished_visual", "unfinished"),
        ("post_load_ring_not_ready", "post_load_ring_not_ready", "spawn_ring_not_ready"),
        ("column_loaded_no_mesh_n", "column_loaded_no_mesh_n"),
        ("focus_missing_mesh", "focus_missing_mesh", "miss"),
        ("visible_black_focus_n", "visible_black_focus", "visible_black_focus_n"),
        ("dark_face_void_near_n", "dark_face_void_near_n", "void_near"),
        ("near_focus_holes", "near_focus_holes"),
        ("visual_holes", "visual_holes"),
        ("fully_dark_stalled_near_n", "fully_dark_stalled_near_n", "mid_fully_dark_stalled"),
        ("stream_ms", "stream_ms", "world_streaming_phase_ms"),
        ("wall_ms", "wall_ms"),
        ("dirty", "dirty", "dirty_n"),
        ("player_y", "player_y"),
        ("focus_cx", "focus_cx"),
        ("movement_speed", "movement_speed"),
        ("chunk_not_ready", "chunk_not_ready", "chunk_not_ready_med"),
        ("opaque_gpu_packed_n", "opaque_gpu_packed_n"),
        ("publication_incomplete_material_n", "publication_incomplete_material_n"),
    ]
    print(f"\n=== {name} n={len(s)} ===")
    if not s:
        return
    for label, *aliases in keys:
        vals = [g(r, *aliases) for r in s]
        nums = [v for v in vals if isinstance(v, (int, float))]
        if not nums:
            continue
        print(f"  {label}: med={med(nums)} max={max(nums)} last={nums[-1]}")

seg("all", lambda r: True)
seg("moving", lambda r: (g(r, "movement_speed") or 0) > 2)
seg("mid_cx_2_5", lambda r: 2 <= (g(r, "focus_cx") or 99) <= 5 and (g(r, "movement_speed") or 0) > 2)
seg("west_cx_le_m3_moving", lambda r: (g(r, "focus_cx") or 99) <= -3 and (g(r, "movement_speed") or 0) > 2)
seg("west_cx_le_m3_all", lambda r: (g(r, "focus_cx") or 99) <= -3)
seg("tail_last_8", lambda r: True)  # override below

tail = rows[-8:]
print(f"\n=== tail_last_8 ===")
for r in tail:
    print(
        f"  cx={g(r,'focus_cx')} y={g(r,'player_y')} spd={g(r,'movement_speed')} "
        f"unf={g(r,'unfinished_visual','unfinished')} ring={g(r,'post_load_ring_not_ready')} "
        f"noloadmesh={g(r,'column_loaded_no_mesh_n')} miss={g(r,'focus_missing_mesh','miss')} "
        f"vb={g(r,'visible_black_focus','visible_black_focus_n')} void={g(r,'dark_face_void_near_n')} "
        f"stream={g(r,'stream_ms','world_streaming_phase_ms')}"
    )

# Timeline of unfinished/ring vs cx
print("\n=== timeline (cx,unf,ring,noload,miss,vb) every period ===")
for r in rows:
    print(
        f"cx={g(r,'focus_cx'):>5} y={g(r,'player_y')} spd={g(r,'movement_speed')} "
        f"unf={g(r,'unfinished_visual','unfinished')} ring={g(r,'post_load_ring_not_ready')} "
        f"nlm={g(r,'column_loaded_no_mesh_n')} miss={g(r,'focus_missing_mesh','miss')} "
        f"vb={g(r,'visible_black_focus','visible_black_focus_n')}"
    )
