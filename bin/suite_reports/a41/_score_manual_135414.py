#!/usr/bin/env python3
"""Deep score A41 manual 135414 vs prior + DoD."""
import json
import statistics as st
from pathlib import Path

LOGS = Path(r"e:\Work\Home\Cubatarium\bin\logs")
RUNS = {
    "135414": LOGS / "perf_20260924-135414_29876.jsonl",
    "132520": LOGS / "perf_20260924-132520_44704.jsonl",
    "134902_af": LOGS / "perf_20260924-134902_23828.jsonl",
    "103228": LOGS / "perf_20260924-103228_43036.jsonl",
}

KEYS = [
    "near_focus_holes",
    "unfinished_visual",
    "chunk_meshed_unlit",
    "chunk_meshed_unlit_hidden",
    "visible_black_focus_n",
    "visible_black_legal_dark_n",
    "visible_black_fully_dark_repair_n",
    "visible_black_fully_dark_stalled_n",
    "visible_black_no_ticket_n",
    "pending_light",
    "pending_light_focus",
    "demand_unsat_geom",
    "demand_unsat_light",
    "demand_stop_converged",
    "focus_missing_mesh",
    "focus_not_render_ready",
    "relight_fifo_n",
    "relight_apply_n",
    "mark_relit_prefer_kick_n",
    "mark_relit_force_stale_n",
    "mark_relit_schedule_n",
    "softdefer_held_n",
    "softdefer_empty_placeholder_n",
    "dirty_dropped",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_skip_snapshot_n",
    "mesh_dirty_schedule_skip_n",
    "dirty",
    "draw_oracle_fully_dark_debt_n",
    "draw_oracle_stale_vertex_light_n",
    "draw_oracle_legal_dark_n",
    "fully_dark_census_n",
    "dark_face_stale_near_n",
    "oldest_stale_vertex_light_age_frames",
    "column_render_ready_n",
    "column_job_render_ready_n",
    "column_job_pending_light_n",
    "movement_speed",
    "focus_cx",
]


def load_periods(p: Path):
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
        return None
    return {
        "n": len(vals),
        "med": st.median(vals),
        "last": vals[-1],
        "max": max(vals),
        "gt0": sum(1 for v in vals if v > 0),
    }


def main():
    out = {}
    for name, path in RUNS.items():
        rows = load_periods(path)
        print(f"=== {name} periods={len(rows)} {path.name} ===")
        if rows:
            print(
                f"  cx {rows[0].get('focus_cx')}->{rows[-1].get('focus_cx')} "
                f"cz {rows[0].get('focus_cz')}->{rows[-1].get('focus_cz')}"
            )
        entry = {"periods": len(rows), "metrics": {}}
        for k in KEYS:
            s = stats(rows, k)
            if not s:
                continue
            entry["metrics"][k] = s
            print(
                f"  {k}: med={s['med']} last={s['last']} max={s['max']} "
                f"gt0={s['gt0']}/{s['n']}"
            )
        print()
        out[name] = entry

    rows = load_periods(RUNS["135414"])
    print("--- 135414 full trajectory ---")
    for i, r in enumerate(rows):
        print(
            f"[{i:02d}] cx={r.get('focus_cx'):>3} "
            f"spd={float(r.get('movement_speed') or 0):7.2f} "
            f"holes={r.get('near_focus_holes')} unlit={r.get('chunk_meshed_unlit')} "
            f"vb={r.get('visible_black_focus_n')} pl={r.get('pending_light')} "
            f"plf={r.get('pending_light_focus')} unfin={r.get('unfinished_visual')} "
            f"miss={r.get('focus_missing_mesh')} unsat_g={r.get('demand_unsat_geom')} "
            f"unsat_l={r.get('demand_unsat_light')} stop={r.get('demand_stop_converged')} "
            f"sched={r.get('mark_relit_schedule_n')} fstale={r.get('mark_relit_force_stale_n')} "
            f"ok={r.get('mesh_dirty_schedule_ok_n')} skip_snap={r.get('mesh_dirty_schedule_skip_snapshot_n')} "
            f"drop={r.get('dirty_dropped')} dirty={r.get('dirty')} "
            f"legal={r.get('visible_black_legal_dark_n')} "
            f"fd_rep={r.get('visible_black_fully_dark_repair_n')} "
            f"fd_stall={r.get('visible_black_fully_dark_stalled_n')} "
            f"no_tix={r.get('visible_black_no_ticket_n')} "
            f"stale_age={r.get('oldest_stale_vertex_light_age_frames')} "
            f"fifo={r.get('relight_fifo_n')} apply={r.get('relight_apply_n')}"
        )

    last = rows[-1] if rows else {}
    print("\n--- 135414 STOP last-period DoD autopsy ---")
    for k in [
        "near_focus_holes",
        "chunk_meshed_unlit",
        "pending_light",
        "demand_stop_converged",
        "mark_relit_schedule_n",
        "mesh_dirty_schedule_skip_snapshot_n",
        "dirty",
        "draw_oracle_stale_vertex_light_n",
        "draw_oracle_fully_dark_debt_n",
        "draw_oracle_legal_dark_n",
        "visible_black_fully_dark_repair_n",
        "visible_black_fully_dark_stalled_n",
        "visible_black_no_ticket_n",
        "oldest_stale_vertex_light_age_frames",
        "fully_dark_census_n",
        "dark_face_stale_near_n",
        "relight_fifo_n",
        "relight_apply_n",
        "softdefer_held_n",
        "column_render_ready_n",
        "column_job_render_ready_n",
        "column_job_pending_light_n",
        "mesh_dirty_schedule_ok_n",
        "mesh_dirty_schedule_skip_n",
        "dirty_dropped",
        "gpu_kick_n",
        "mark_relit_prefer_kick_n",
    ]:
        if k in last:
            print(f"  {k}={last.get(k)}")

    dest = Path(r"e:\Work\Home\Cubatarium\bin\suite_reports\a41\manual_135414_score.json")
    dest.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(f"\nwrote {dest}")


if __name__ == "__main__":
    main()
