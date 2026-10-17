#!/usr/bin/env python3
import json
from pathlib import Path
from collections import Counter

p = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260816-193933_23600.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
cruise = [r for r in rows if (r.get("chunk_count") or 0) >= 80]
print("n", len(cruise))
print(
    "focus",
    (cruise[0].get("focus_cx"), cruise[0].get("focus_cz")),
    "->",
    (cruise[-1].get("focus_cx"), cruise[-1].get("focus_cz")),
)


def stats(key):
    vals = [r[k] for r in cruise if isinstance(r.get(k), (int, float))]
    if not vals:
        print(key, "MISSING")
        return
    vals = sorted(vals)
    med = vals[len(vals) // 2]
    p90 = vals[int((len(vals) - 1) * 0.9)]
    print(f"{key}: med={med} p90={p90} max={vals[-1]} min={vals[0]}")


for k in [
    "prep_softdefer_setup_ms",
    "softdefer_empty_scan_ms",
    "softdefer_empty_own_ms",
    "wall_ms",
    "opaque_cmd_on",
    "dirty_fm_n",
    "dirty_remesh_n",
    "mesh_dirty_schedule_ok_n",
    "visible_black_focus_n",
    "stream_loads",
    "column_absent_in_rd_n",
    "column_loaded_no_mesh_n",
    "mesh_apply_stale",
    "mesh_discarded_late",
    "remesh_after_apply_n",
    "gpu_pending_n",
    "softdefer_empty_near_n",
    "softdefer_held_n",
    "chunk_count",
    "keep_cols",
    "visual_cols",
]:
    stats(k)

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

# opaque frame-to-frame churn
opaque = [r.get("opaque_cmd_on") or 0 for r in cruise]
od = [abs(opaque[i] - opaque[i - 1]) for i in range(1, len(opaque))]
print(
    "opaque_delta med/p90/max",
    sorted(od)[len(od) // 2],
    sorted(od)[int(0.9 * (len(od) - 1))],
    max(od),
)
print("frames opaque<200", sum(1 for o in opaque if o < 200), "/", len(opaque))

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
        f"absent={r.get('column_absent_in_rd_n')} no_mesh={r.get('column_loaded_no_mesh_n')} "
        f"loads={r.get('stream_loads')} uf={r.get('underfeet_has_mesh')}/{r.get('underfeet_reason')} "
        f"stale={r.get('mesh_apply_stale')} raa={r.get('remesh_after_apply_n')} "
        f"held={r.get('softdefer_held_n')} empty_near={r.get('softdefer_empty_near_n')}"
    )
