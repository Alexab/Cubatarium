#!/usr/bin/env python3
"""Audit manual cruise perf jsonl vs SoT SLA baselines."""
from __future__ import annotations

import json
import statistics as st
from collections import Counter, defaultdict
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")


def load(name: str):
    p = LOGS / name
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def pct(vals, p):
    if not vals:
        return None
    vals = sorted(vals)
    i = min(len(vals) - 1, int(round((len(vals) - 1) * p)))
    return vals[i]


def num(rows, key):
    return [r[key] for r in rows if isinstance(r.get(key), (int, float))]


def summarize(label, rows, min_chunks=80):
    cruise = [r for r in rows if (r.get("chunk_count") or 0) >= min_chunks]
    print(f"\n===== {label} raw={len(rows)} cruise={len(cruise)} =====")
    if not cruise:
        print("  (empty)")
        return cruise
    r0, r1 = cruise[0], cruise[-1]
    print(
        f"  focus ({r0.get('focus_cx')},{r0.get('focus_cz')}) -> "
        f"({r1.get('focus_cx')},{r1.get('focus_cz')}) "
        f"chunks {r0.get('chunk_count')}->{r1.get('chunk_count')}"
    )
    keys = [
        "wall_ms",
        "world_streaming_phase_ms",
        "mesh_emerge_ms",
        "mesh_emerge_prep_unfinished_ms",
        "prep_pending_light_ms",
        "prep_black_sticky_ms",
        "prep_dirty_count_ms",
        "prep_softdefer_setup_ms",
        "mesh_dirty_tick_ms",
        "mesh_dirty_prune_ms",
        "mesh_dirty_sort_ms",
        "mesh_dirty_schedule_ms",
        "mesh_dirty_drain_ms",
        "mesh_dirty_gpu_ms",
        "dirty",
        "dirty_fm_n",
        "dirty_remesh_n",
        "mesh_dirty_schedule_ok_n",
        "mesh_dirty_schedule_skip_n",
        "focus_missing_mesh",
        "visual_holes",
        "visible_black_focus_n",
        "dark_face_void_near_n",
        "opaque_cmd_on",
        "column_absent_in_rd_n",
        "column_loaded_no_mesh_n",
        "column_render_ready_n",
        "column_meshing_n",
        "column_lighting_n",
        "gen_backlog_total",
        "relight_fifo_dropped",
        "relight_fifo_n",
        "pending_light_focus_n",
        "softdefer_empty_near_n",
        "softdefer_held_n",
        "frontier_pressure",
        "gpu_pending_n",
        "remesh_after_apply_n",
        "underfeet_has_mesh",
    ]
    for k in keys:
        vals = num(cruise, k)
        if not vals:
            continue
        print(
            f"  {k}: med={pct(vals,0.5)} p90={pct(vals,0.9)} "
            f"max={max(vals)} min={min(vals)}"
        )
    miss = 100.0 * sum(1 for r in cruise if r.get("focus_missing_mesh")) / len(cruise)
    holes = 100.0 * sum(1 for r in cruise if r.get("visual_holes")) / len(cruise)
    uf0 = 100.0 * sum(1 for r in cruise if not r.get("underfeet_has_mesh")) / len(cruise)
    print(f"  miss_frame%={miss:.1f} holes_frame%={holes:.1f} underfeet_missing%={uf0:.1f}")
    print("  underfeet_reason", Counter(r.get("underfeet_reason") for r in cruise).most_common(8))
    print("  streaming_mode", Counter(r.get("streaming_mode") for r in cruise).most_common(6))
    return cruise


def timeline(cruise, step=25):
    print("\n--- timeline ---")
    idxs = list(range(0, len(cruise), max(1, len(cruise) // 12)))
    if idxs[-1] != len(cruise) - 1:
        idxs.append(len(cruise) - 1)
    for i in idxs:
        r = cruise[i]
        print(
            f"[{i:4d}] f=({r.get('focus_cx')},{r.get('focus_cz')}) "
            f"wall={r.get('wall_ms')} emerge={r.get('mesh_emerge_ms')} "
            f"prep={r.get('mesh_emerge_prep_unfinished_ms')} "
            f"dirty={r.get('dirty')} ok={r.get('mesh_dirty_schedule_ok_n')} "
            f"miss={r.get('focus_missing_mesh')} holes={r.get('visual_holes')} "
            f"vb={r.get('visible_black_focus_n')} opaque={r.get('opaque_cmd_on')} "
            f"rr={r.get('column_render_ready_n')} no_mesh={r.get('column_loaded_no_mesh_n')} "
            f"absent={r.get('column_absent_in_rd_n')} "
            f"fifo_d={r.get('relight_fifo_dropped')} "
            f"uf={r.get('underfeet_has_mesh')}/{r.get('underfeet_reason')}"
        )


def sla_check(cruise):
    print("\n===== SLA vs docs/streaming_cruise_sot.md =====")
    wall = num(cruise, "wall_ms")
    prep_keys = [
        "prep_pending_light_ms",
        "prep_black_sticky_ms",
        "prep_dirty_count_ms",
        "prep_softdefer_setup_ms",
    ]
    prep_sum = []
    for r in cruise:
        s = 0.0
        ok = False
        for k in prep_keys:
            if isinstance(r.get(k), (int, float)):
                s += float(r[k])
                ok = True
        if ok:
            prep_sum.append(s)
        elif isinstance(r.get("mesh_emerge_prep_unfinished_ms"), (int, float)):
            prep_sum.append(float(r["mesh_emerge_prep_unfinished_ms"]))
    opaque = num(cruise, "opaque_cmd_on")
    vb = num(cruise, "visible_black_focus_n")
    ok_n = num(cruise, "mesh_dirty_schedule_ok_n")
    remesh = num(cruise, "dirty_remesh_n")
    print(f"  wall med/p90 target <=130/220 => {pct(wall,0.5)}/{pct(wall,0.9)}")
    print(f"  prep hot med target <=20 => {pct(prep_sum,0.5) if prep_sum else None}")
    print(f"  schedule_ok med (holes proxy >=8) => {pct(ok_n,0.5)}")
    print(f"  dirty_remesh med => {pct(remesh,0.5)}")
    print(f"  opaque med/min => {pct(opaque,0.5)}/{min(opaque) if opaque else None}")
    print(f"  visible_black med target <=25 => {pct(vb,0.5)}")
    # near-stationary opaque
    near = [
        r
        for r in cruise
        if abs((r.get("focus_cx") or 0) - (cruise[0].get("focus_cx") or 0))
        + abs((r.get("focus_cz") or 0) - (cruise[0].get("focus_cz") or 0))
        <= 6
    ]
    if near:
        o = num(near, "opaque_cmd_on")
        print(f"  opaque near-start (dFocus<=6) med/min => {pct(o,0.5)}/{min(o) if o else None}")


def spikes(cruise):
    print("\n===== spikes / pathology =====")
    walls = sorted(cruise, key=lambda r: -(r.get("wall_ms") or 0))[:8]
    print("  top wall frames:")
    for r in walls:
        print(
            f"    wall={r.get('wall_ms')} emerge={r.get('mesh_emerge_ms')} "
            f"dirty_tick={r.get('mesh_dirty_tick_ms')} dirty={r.get('dirty')} "
            f"miss={r.get('focus_missing_mesh')} holes={r.get('visual_holes')} "
            f"f=({r.get('focus_cx')},{r.get('focus_cz')})"
        )
    # miss streaks
    streak = max_streak = 0
    for r in cruise:
        if r.get("focus_missing_mesh"):
            streak += 1
            max_streak = max(max_streak, streak)
        else:
            streak = 0
    print(f"  max miss streak frames={max_streak}")
    # opaque collapse
    low_opaque = [r for r in cruise if (r.get("opaque_cmd_on") or 0) < 200]
    print(f"  frames opaque<200: {len(low_opaque)}/{len(cruise)}")
    if low_opaque:
        print(
            "  sample low opaque:",
            {
                k: low_opaque[len(low_opaque) // 2].get(k)
                for k in [
                    "opaque_cmd_on",
                    "focus_missing_mesh",
                    "visual_holes",
                    "visible_black_focus_n",
                    "column_absent_in_rd_n",
                    "dirty",
                    "focus_cx",
                    "focus_cz",
                ]
            },
        )


def main():
    new = load("perf_20260816-184340_26004.jsonl")
    cruise = summarize("184340 MANUAL (new)", new)
    if cruise:
        timeline(cruise)
        sla_check(cruise)
        spikes(cruise)

    for name, label in [
        ("perf_20260815-203518_21932.jsonl", "203518 baseline"),
        ("perf_20260816-145451_11344.jsonl", "145451 inland cruise"),
        ("perf_20260816-102747_5292.jsonl", "102747 fail"),
    ]:
        p = LOGS / name
        if p.exists() and p.stat().st_size > 0:
            summarize(label, load(name))


if __name__ == "__main__":
    main()
