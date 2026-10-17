#!/usr/bin/env python3
"""Score A41 manual perf vs baselines."""
import json
import statistics as st
from pathlib import Path

KEYS = [
    "near_focus_holes",
    "unfinished_visual",
    "chunk_meshed_unlit",
    "visible_black_focus_n",
    "visible_black_legal_dark_n",
    "pending_light",
    "pending_light_focus",
    "demand_unsat_geom",
    "demand_unsat_light",
    "demand_stop_converged",
    "focus_missing_mesh",
    "missing_mesh",
    "focus_not_render_ready",
    "relight_fifo_n",
    "mark_relit_prefer_kick_n",
    "mark_relit_force_stale_n",
    "mark_relit_schedule_n",
    "soft_defer_held_n",
    "soft_defer_empty_placeholder_n",
    "movement_speed",
    "focus_cx",
    "focus_cz",
    "dirty_dropped",
    "mesh_dirty_schedule_ok_n",
    "column_loaded_no_mesh_n",
    "draw_oracle_legal_dark_n",
]


def load(p: Path):
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        try:
            o = json.loads(line)
        except json.JSONDecodeError:
            continue
        if o.get("kind") == "period":
            rows.append(o)
    return rows


def med(xs):
    xs = [x for x in xs if x is not None]
    return None if not xs else st.median(xs)


def stats(rows, key):
    vals = []
    for r in rows:
        v = r.get(key)
        if v is None:
            continue
        try:
            vals.append(float(v))
        except (TypeError, ValueError):
            pass
    if not vals:
        return {"n": 0}
    return {
        "n": len(vals),
        "med": med(vals),
        "last": vals[-1],
        "max": max(vals),
        "gt0": sum(1 for v in vals if v > 0),
    }


def main():
    root = Path(__file__).resolve().parents[2] / "logs"
    runs = {
        "132520": root / "perf_20260924-132520_44704.jsonl",
        "103228": root / "perf_20260924-103228_43036.jsonl",
        "102349": root / "perf_20260924-102349_22712.jsonl",
    }
    out = {}
    for name, p in runs.items():
        rows = load(p)
        entry = {
            "periods": len(rows),
            "cx": [rows[0].get("focus_cx"), rows[-1].get("focus_cx")] if rows else None,
            "cz": [rows[0].get("focus_cz"), rows[-1].get("focus_cz")] if rows else None,
            "metrics": {},
        }
        print(f"=== {name} periods={len(rows)} path={p.name} ===")
        if rows:
            print(
                f"  cx {rows[0].get('focus_cx')} -> {rows[-1].get('focus_cx')} "
                f"cz {rows[0].get('focus_cz')} -> {rows[-1].get('focus_cz')}"
            )
        for k in KEYS:
            s = stats(rows, k)
            if s.get("n", 0) == 0:
                continue
            entry["metrics"][k] = s
            print(
                f"  {k}: med={s['med']} last={s['last']} max={s['max']} "
                f"gt0={s['gt0']}/{s['n']}"
            )
        print()
        out[name] = entry

    # Trajectory dump for 132520: periods with holes or high unlit
    rows = load(runs["132520"])
    print("--- 132520 periods with near_focus_holes>0 or unlit>15 ---")
    for i, r in enumerate(rows):
        h = float(r.get("near_focus_holes") or 0)
        u = float(r.get("chunk_meshed_unlit") or 0)
        if h > 0 or u > 15:
            print(
                f"  [{i}] cx={r.get('focus_cx')} spd={r.get('movement_speed')} "
                f"holes={h} unlit={u} vb={r.get('visible_black_focus_n')} "
                f"pl={r.get('pending_light')} unfin={r.get('unfinished_visual')} "
                f"unsat_g={r.get('demand_unsat_geom')} stop={r.get('demand_stop_converged')} "
                f"sched={r.get('mark_relit_schedule_n')} kick={r.get('mark_relit_prefer_kick_n')}"
            )

    dest = Path(__file__).resolve().parent / "manual_132520_score.json"
    dest.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(f"wrote {dest}")


if __name__ == "__main__":
    main()
