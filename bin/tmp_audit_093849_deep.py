#!/usr/bin/env python3
"""Deep dive 093849 R7.2 regression owners."""
from __future__ import annotations

import json
import statistics as st
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CUR = ROOT / "bin/logs/perf_20260910-093849_26976.jsonl"
BASE = ROOT / "bin/logs/perf_20260909-172957_8616.jsonl"


def load(path: Path):
    rows = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            if '"kind":"period"' not in line and '"kind": "period"' not in line:
                continue
            r = json.loads(line)
            if r.get("kind") == "period":
                rows.append(r)
    return rows


def med(xs):
    return st.median(xs) if xs else None


def series(rows, key):
    out = []
    for r in rows:
        v = r.get(key)
        if v is not None:
            out.append(float(v))
    return out


def cruise(rows):
    return [r for r in rows if float(r.get("movement_speed") or 0) > 2.0]


def stand(rows):
    return [r for r in rows if float(r.get("movement_speed") or 0) <= 1.5]


def timeline(tag, rows):
    print(f"\n--- timeline {tag} ---")
    for i, r in enumerate(rows):
        print(
            f"[{i:02d}] spd={float(r.get('movement_speed') or 0):4.1f} "
            f"wall={float(r.get('wall_ms') or 0):6.1f} "
            f"ok={r.get('mesh_dirty_schedule_ok_n')} "
            f"fm={r.get('dirty_fm_n')} "
            f"holes={r.get('visual_holes')} "
            f"sd={r.get('softdefer_empty_stuck_n')} "
            f"unfin={r.get('unfinished_visual')} "
            f"meshing={r.get('column_meshing_n')} "
            f"fifo={r.get('relight_fifo_n')} "
            f"cull={float(r.get('scene_opaque_cull_ms') or 0):5.1f} "
            f"scene={float(r.get('scene_ms') or 0):5.1f} "
            f"emerge={float(r.get('mesh_emerge_ms') or 0):5.1f} "
            f"cxz=({r.get('focus_cx')},{r.get('focus_cz')})"
        )


def compare_keys(a, b, keys):
    print("\n--- med deltas cruise 093849 - 172957 ---")
    for k in keys:
        va, vb = med(series(a, k)), med(series(b, k))
        d = None if va is None or vb is None else va - vb
        print(f"  {k:32s} cur={va} base={vb} d={d}")


def main():
    cur = load(CUR)
    base = load(BASE)
    print(f"cur periods={len(cur)} cruise={len(cruise(cur))} stand={len(stand(cur))}")
    print(f"base periods={len(base)} cruise={len(cruise(base))} stand={len(stand(base))}")
    timeline("093849", cur)
    cc, cb = cruise(cur), cruise(base)
    compare_keys(
        cc,
        cb,
        [
            "wall_ms",
            "scene_ms",
            "scene_opaque_cull_ms",
            "scene_opaque_draw_ms",
            "scene_transparent_ms",
            "mesh_emerge_ms",
            "stream_ms",
            "mesh_dirty_schedule_ok_n",
            "dirty_fm_n",
            "dirty_remesh_n",
            "dirty_n",
            "mesh_dirty_schedule_skip_n",
            "softdefer_empty_stuck_n",
            "softdefer_held_n",
            "softdefer_empty_near_n",
            "visual_holes",
            "unfinished_visual",
            "column_meshing_n",
            "column_lighting_n",
            "column_loaded_no_mesh_n",
            "column_render_ready_n",
            "relight_fifo_n",
            "pending_light_focus_n",
            "gpu_kick_n",
            "gpu_pending_n",
            "mesh_discarded_late",
            "miss_horiz",
            "visible_black_focus_n",
            "ingress_debt",
            "empty_backlog",
            "focus_missing_mesh",
            "opaque_cull_skipped",
            "prep_schedule_policy_ms",
            "mesh_dirty_tick_ms",
            "mesh_dirty_schedule_ms",
            "mesh_dirty_drain_ms",
            "relight_drain_ms",
            "async_io_ms",
        ],
    )
    # schedule_ok==0 / ==1 frac
    for tag, rows in [("093849", cc), ("172957", cb)]:
        oks = series(rows, "mesh_dirty_schedule_ok_n")
        fms = series(rows, "dirty_fm_n")
        print(
            f"\n{tag} sched_ok==0 frac={sum(1 for v in oks if v==0)/len(oks):.2f} "
            f"<=1 frac={sum(1 for v in oks if v<=1)/len(oks):.2f} "
            f"fm==0 frac={sum(1 for v in fms if v==0)/len(fms):.2f} "
            f"fm<=2 frac={sum(1 for v in fms if v<=2)/len(fms):.2f}"
        )
    # stand holes detail
    for tag, rows in [("093849", stand(cur)), ("172957", stand(base))]:
        holes = series(rows, "visual_holes")
        hf = sum(1 for v in holes if v > 0) / len(holes) if holes else None
        print(
            f"stand {tag}: n={len(rows)} holes_frac={hf} "
            f"unfin={med(series(rows,'unfinished_visual'))} "
            f"ok={med(series(rows,'mesh_dirty_schedule_ok_n'))} "
            f"fm={med(series(rows,'dirty_fm_n'))} "
            f"meshing={med(series(rows,'column_meshing_n'))}"
        )


if __name__ == "__main__":
    main()
