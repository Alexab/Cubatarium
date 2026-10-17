#!/usr/bin/env python3
"""Compare B1 (no A2) 202741 vs wall-diet tip 200927 vs 154921 anchors."""
from __future__ import annotations

import json
from pathlib import Path

FLIGHTS = {
    "B1_202741": Path("bin/logs/perf_20260914-202741_25256.jsonl"),
    "tip_200927": Path("bin/logs/perf_20260914-200927_44748.jsonl"),
}

ANCHOR_154921 = {
    "visible_black_focus_n": 77,
    "draw_oracle_stale_vertex_light_n": 75,
    "dirty_remesh_n": 50,
    "chunk_meshed_unlit_max": 13,
    "unfinished_visual": 5,
    "wall_ms": 71.1,
    "stream_ms": 31.2,
    "mesh_emerge_ms": 28.0,
}


def load(path: Path):
    rows = []
    for line in path.open(encoding="utf-8", errors="ignore"):
        if not line.startswith("{"):
            continue
        try:
            o = json.loads(line)
        except Exception:
            continue
        if "wall_ms" in o:
            rows.append(o)
    return rows


def mid(rows):
    out = []
    for r in rows:
        cx = r.get("focus_cx")
        if cx is None:
            continue
        if -2 <= float(cx) <= 6:
            out.append(r)
    return out


def med(xs):
    xs = [x for x in xs if isinstance(x, (int, float))]
    if not xs:
        return None
    return sorted(xs)[len(xs) // 2]


def summarize(tag, rows):
    print(f"\n=== {tag} n={len(rows)} ===")
    keys = [
        "wall_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "streamer_update_ms",
        "visible_black_focus_n",
        "draw_oracle_stale_vertex_light_n",
        "dark_face_stale_near_n",
        "dark_face_void_near_n",
        "chunk_meshed_unlit",
        "unfinished_visual",
        "visual_holes",
        "focus_missing_mesh",
        "dirty",
        "dirty_remesh_n",
        "mesh_apply_stale_visual",
        "pool_unsync_uploads",
        "opaque_cmd_on",
        "opaque_culled_frac",
        "false_neg_cull_n",
        "draw_oracle_false_neg_n",
        "gpu_compact_fail_open_n",
        "publication_progress_unit_n",
    ]
    for k in keys:
        vals = [r.get(k) for r in rows if isinstance(r.get(k), (int, float))]
        if not vals:
            continue
        print(
            f"  {k:36s} med={med(vals):10.4g}  max={max(vals):10.4g}  p90={sorted(vals)[int(0.9*(len(vals)-1))]:10.4g}"
        )
    u = [r.get("unfinished_visual") or 0 for r in rows]
    blink = sum(1 for a, b in zip(u, u[1:]) if (a > 0) != (b > 0))
    hole = sum(
        1
        for r in rows
        if (r.get("unfinished_visual") or 0) > 0
        or (r.get("visual_holes") or 0) > 0
        or (r.get("focus_missing_mesh") or 0) > 0
    )
    vb = [
        r.get("visible_black_focus_n")
        for r in rows
        if isinstance(r.get("visible_black_focus_n"), (int, float))
    ]
    vb_hi = sum(1 for v in vb if v >= 100)
    unlit = [
        r.get("chunk_meshed_unlit")
        for r in rows
        if isinstance(r.get("chunk_meshed_unlit"), (int, float))
    ]
    unlit_hi = sum(1 for v in unlit if v >= 40)
    print(f"  unfinished_blink={blink} holeish_frac={hole/len(rows) if rows else None:.3f}")
    print(f"  vb_ge100_frac={vb_hi/len(vb) if vb else None:.3f} unlit_ge40_frac={unlit_hi/len(unlit) if unlit else None:.3f}")
    # cull pressure
    culled = [
        r.get("opaque_culled_frac")
        for r in rows
        if isinstance(r.get("opaque_culled_frac"), (int, float))
    ]
    if culled:
        print(f"  opaque_culled_frac med={med(culled):.4f} max={max(culled):.4f}")


def main():
    print("154921 anchors:", ANCHOR_154921)
    for tag, path in FLIGHTS.items():
        rows = load(path)
        summarize(tag + "/mid", mid(rows))
        summarize(tag + "/all", rows)


if __name__ == "__main__":
    main()
