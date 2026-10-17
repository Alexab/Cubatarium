#!/usr/bin/env python3
import json
from pathlib import Path
from collections import Counter

p = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260816-201626_4992.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
cruise = [r for r in rows if (r.get("chunk_count") or 0) >= 80]
print("n", len(cruise))
print(
    "focus",
    (cruise[0].get("focus_cx"), cruise[0].get("focus_cz")),
    "->",
    (cruise[-1].get("focus_cx"), cruise[-1].get("focus_cz")),
)


def med(key):
    vals = sorted(r[key] for r in cruise if isinstance(r.get(key), (int, float)))
    if not vals:
        return None
    return vals[len(vals) // 2]


def p90(key):
    vals = sorted(r[key] for r in cruise if isinstance(r.get(key), (int, float)))
    if not vals:
        return None
    return vals[int((len(vals) - 1) * 0.9)]


for k in [
    "wall_ms",
    "mesh_emerge_ms",
    "mesh_emerge_prep_ms",
    "prep_softdefer_setup_ms",
    "softdefer_empty_scan_ms",
    "softdefer_empty_own_ms",
    "opaque_cmd_on",
    "dirty_fm_n",
    "dirty_remesh_n",
    "mesh_dirty_schedule_ok_n",
    "visible_black_focus_n",
    "column_loaded_no_mesh_n",
    "mesh_discarded_late",
    "chunk_count",
    "visual_cols",
    "keep_cols",
    "underfeet_has_mesh",
]:
    vals = [r[k] for r in cruise if isinstance(r.get(k), (int, float))]
    if not vals:
        print(k, "MISSING")
        continue
    vals = sorted(vals)
    print(
        f"{k}: med={vals[len(vals)//2]} p90={vals[int(0.9*(len(vals)-1))]} "
        f"min={vals[0]} max={vals[-1]}"
    )

print(
    "miss%",
    round(100 * sum(1 for r in cruise if r.get("focus_missing_mesh")) / len(cruise), 1),
)
print(
    "uf miss%",
    round(
        100 * sum(1 for r in cruise if not r.get("underfeet_has_mesh")) / len(cruise),
        1,
    ),
)
print("uf reason", Counter(r.get("underfeet_reason") for r in cruise).most_common())
print(
    "frames opaque<200",
    sum(1 for r in cruise if (r.get("opaque_cmd_on") or 0) < 200),
    "/",
    len(cruise),
)
late = [r for r in cruise if (r.get("focus_cz") or 0) >= 55]
if late:
    o = sorted(r.get("opaque_cmd_on") or 0 for r in late)
    print("late cz>=55 n", len(late), "opaque med/min", o[len(o) // 2], o[0])

opaque = [r.get("opaque_cmd_on") or 0 for r in cruise]
swings = [
    (i, opaque[i - 1], opaque[i])
    for i in range(1, len(opaque))
    if abs(opaque[i] - opaque[i - 1]) >= 40
]
print("opaque swings>=40:", len(swings))
for t in sorted(swings, key=lambda x: -abs(x[2] - x[1]))[:8]:
    print(" ", t)

print("--- timeline ---")
idxs = list(range(0, len(cruise), max(1, len(cruise) // 12)))
if idxs[-1] != len(cruise) - 1:
    idxs.append(len(cruise) - 1)
for i in idxs:
    r = cruise[i]
    print(
        f"[{i:3d}] f=({r.get('focus_cx')},{r.get('focus_cz')}) "
        f"wall={r.get('wall_ms'):.0f} setup={r.get('prep_softdefer_setup_ms')} "
        f"scan={r.get('softdefer_empty_scan_ms')} dirty={r.get('dirty')} "
        f"fm={r.get('dirty_fm_n')} rm={r.get('dirty_remesh_n')} "
        f"ok={r.get('mesh_dirty_schedule_ok_n')} miss={r.get('focus_missing_mesh')} "
        f"vb={r.get('visible_black_focus_n')} opaque={r.get('opaque_cmd_on')} "
        f"no_mesh={r.get('column_loaded_no_mesh_n')} "
        f"uf={r.get('underfeet_has_mesh')}/{r.get('underfeet_reason')} "
        f"disc={r.get('mesh_discarded_late')}"
    )
