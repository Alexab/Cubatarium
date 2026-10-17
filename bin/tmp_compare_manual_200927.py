#!/usr/bin/env python3
"""Compare manual west flights: 154921 SoT vs latest 200927."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

PATHS = {
    "154921": Path("bin/logs/perf_20260914-154921_33492.jsonl"),
    "200927": Path("bin/logs/perf_20260914-200927_44748.jsonl"),
}

KEYS = [
    "wall_ms",
    "prep_schedule_policy_ms",
    "prep_spawn_ring_query_ms",
    "prep_drop_remesh_ms",
    "prep_sched_other_ms",
    "mesh_emerge_ms",
    "stream_ms",
    "streamer_update_ms",
    "async_io_ms",
    "visible_black_focus_n",
    "dark_face_stale_near_n",
    "chunk_meshed_unlit",
    "focus_missing_mesh",
    "unfinished_visual",
    "visual_holes",
    "effective_holes",
    "dirty",
    "dirty_remesh",
    "dirty_fm",
    "mesh_apply_stale",
    "mesh_discarded_late",
    "mesh_apply_superseded",
    "pool_unsync_uploads",
    "pool_retired_reclaimed",
    "pool_fence_timeout",
    "opaque_cmd_on",
    "ok_remesh",
    "ok_fm",
    "schedule_ok",
    "black_sticky",
    "focus_sticky_remesh",
    "false_neg_cull",
    "publication_progress_unit_n",
    "mesh_gpu_kick_n",
    "mesh_gpu_finish_n",
]


def load_rows(path: Path):
    rows = []
    for line in path.open(encoding="utf-8", errors="ignore"):
        if not line.startswith("{"):
            continue
        try:
            o = json.loads(line)
        except Exception:
            continue
        if "wall_ms" in o or o.get("kind") == "period":
            rows.append(o)
    return rows


def audit_filter(rows):
    out = []
    for r in rows:
        sp = r.get("movement_speed")
        py = r.get("player_y")
        cx = r.get("focus_cx")
        if not isinstance(sp, (int, float)):
            continue
        if not (2 < sp < 20):
            continue
        if isinstance(py, (int, float)) and py < 54:
            continue
        if isinstance(cx, (int, float)) and not (-3 <= cx <= 6):
            continue
        out.append(r)
    return out


def west_corridor(rows):
    # loose west cruise: any moving with player_y in band
    out = []
    for r in rows:
        sp = r.get("movement_speed")
        py = r.get("player_y")
        if not isinstance(sp, (int, float)) or sp < 1:
            continue
        if isinstance(py, (int, float)) and not (45 <= py <= 70):
            continue
        out.append(r)
    return out


def med(xs):
    xs = [x for x in xs if isinstance(x, (int, float))]
    if not xs:
        return None
    return float(sorted(xs)[len(xs) // 2])


def summarize(tag: str, rows: list, label: str):
    print(f"\n=== {tag} / {label} n={len(rows)} ===")
    if not rows:
        return
    for k in KEYS:
        vals = [r.get(k) for r in rows if isinstance(r.get(k), (int, float))]
        if not vals:
            # deltas often only on period dumps as accum — try *_delta
            continue
        print(
            f"  {k:28s} med={med(vals):10.4g}  max={max(vals):10.4g}  p90={sorted(vals)[int(0.9*(len(vals)-1))]:10.4g}"
        )
    # holeish
    hole = 0
    for r in rows:
        if (r.get("unfinished_visual") or 0) > 0 or (r.get("visual_holes") or 0) > 0:
            hole += 1
        elif (r.get("focus_missing_mesh") or 0) > 0:
            hole += 1
    print(f"  holeish_frac={hole/len(rows):.3f}")
    vb = [r.get("visible_black_focus_n") for r in rows if isinstance(r.get("visible_black_focus_n"), (int, float))]
    if len(vb) > 2:
        jumps = sum(1 for a, b in zip(vb, vb[1:]) if abs(a - b) >= 8)
        print(f"  vb_jumps_ge8={jumps}/{len(vb)-1}")
    # apply churn
    stale = [r.get("mesh_apply_stale") for r in rows if isinstance(r.get("mesh_apply_stale"), (int, float))]
    disc = [r.get("mesh_discarded_late") for r in rows if isinstance(r.get("mesh_discarded_late"), (int, float))]
    if stale:
        print(f"  mesh_apply_stale end-start={stale[-1]-stale[0] if len(stale)>1 else stale[0]}")
    if disc:
        print(f"  mesh_discarded_late end-start={disc[-1]-disc[0] if len(disc)>1 else disc[0]}")


def first_keys(path: Path):
    for line in path.open(encoding="utf-8", errors="ignore"):
        if line.startswith("{"):
            o = json.loads(line)
            if "wall_ms" in o:
                print("sample keys containing apply/pool/pub/cull/miss:")
                for k in sorted(o):
                    if any(
                        s in k
                        for s in (
                            "apply",
                            "pool",
                            "pub",
                            "cull",
                            "miss",
                            "hole",
                            "stale",
                            "discard",
                            "unlit",
                            "vb",
                            "black",
                            "dirty",
                            "ok_",
                            "sched",
                            "spawn",
                        )
                    ):
                        print(" ", k, "=", o[k])
                return


def main():
    for tag, path in PATHS.items():
        print(f"\n##### {tag} exists={path.exists()} size={path.stat().st_size if path.exists() else 0}")
        if not path.exists():
            continue
        rows = load_rows(path)
        print(f"rows={len(rows)}")
        if tag == "200927":
            first_keys(path)
        summarize(tag, audit_filter(rows), "audit_filter")
        summarize(tag, west_corridor(rows), "west_moving")
        summarize(tag, rows, "all_periods")


if __name__ == "__main__":
    main()
