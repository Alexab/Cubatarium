#!/usr/bin/env python3
"""Manual 112418 flight forensics vs 100413."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

LOGS = Path(__file__).resolve().parents[1] / "bin" / "logs"


def load(p):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return round(st.median(xs), 2) if xs else None


def p90(xs):
    xs = sorted(float(x) for x in xs if x is not None)
    return round(xs[min(len(xs) - 1, int(0.9 * len(xs)))], 2) if xs else None


def rate(rows, k):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(k) or 0) - int(rows[0].get(k) or 0)
    return round(d / max(1, len(rows) - 1), 4)


def g(rows, k):
    return [r.get(k) for r in rows]


def moving(rows):
    out = []
    for i, r in enumerate(rows):
        if i == 0:
            continue
        if (r.get("player_x"), r.get("player_z")) != (
            rows[i - 1].get("player_x"),
            rows[i - 1].get("player_z"),
        ):
            out.append(r)
    return out


def segs(rows):
    out = []
    i = 0
    while i < len(rows):
        x, z = rows[i].get("player_x"), rows[i].get("player_z")
        j = i + 1
        while j < len(rows) and (rows[j].get("player_x"), rows[j].get("player_z")) == (x, z):
            j += 1
        out.append((i, j, x, z, j - i))
        i = j
    return out


def dump_win(lab, rows):
    if not rows:
        print(f"  {lab}: empty")
        return
    print(
        f"--- {lab} n={len(rows)} xz="
        f"({rows[0].get('player_x')},{rows[0].get('player_z')})->"
        f"({rows[-1].get('player_x')},{rows[-1].get('player_z')}) "
        f"keep={med(g(rows, 'keep_cols'))} ---"
    )
    keys = [
        "wall_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "world_streaming_phase_ms",
        "mesh_dirty_tick_ms",
        "softdefer_empty_scan_ms",
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "visible_black_focus_n",
        "visible_black_no_ticket_n",
        "visible_black_focus_raw_n",
        "dark_face_stale_near_n",
        "dark_face_near_n",
        "focus_missing_mesh",
        "miss_horiz",
        "pending_light_focus",
        "dirty_n",
        "dirty_touch_n",
        "dirty_fm_n",
        "dirty_remesh_n",
        "softdefer_empty_stuck_n",
        "softdefer_capture_pin_age",
        "capture_bg_cap_n",
        "black_sticky",
        "underfeet_draw_ok",
        "near_focus_holes",
        "visual_holes",
        "prep_unfinished_full_n",
        "mesh_dirty_schedule_ok_n",
        "remesh_protect_lit_settle_n",
        "focus_dark_mesh",
        "opaque_refs_cpu_vis",
    ]
    for k in keys:
        xs = g(rows, k)
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:36s} med={med(xs):8} p90={p90(xs):8} "
            f"max={round(max(float(x or 0) for x in xs), 2):8}"
        )
    for k in [
        "mesh_discarded_late",
        "mesh_completed_discarded",
        "softdefer_capture_floor_hits",
        "softdefer_capture_retarget_n",
        "softdefer_ingress_witness_n",
        "softdefer_witness_retarget",
        "mesh_apply_stale",
        "stale_repair_wave_n",
        "stand_rim_dirty_n",
    ]:
        print(
            f"  {k:36s} /spike={rate(rows, k)}  "
            f"delta={int(rows[-1].get(k) or 0) - int(rows[0].get(k) or 0)}"
        )
    miss = sum(1 for r in rows if int(r.get("focus_missing_mesh") or 0) > 0)
    print(
        f"  miss_sticky_frac={round(miss / len(rows), 3)}  "
        f"underfeet_need={sum(1 for r in rows if int(r.get('underfeet_need') or 0))}  "
        f"uf_draw0={sum(1 for r in rows if int(r.get('underfeet_draw_ok') or 1) == 0)}"
    )
    # miss coord histogram when sticky
    from collections import Counter

    c = Counter()
    mh = []
    for r in rows:
        if int(r.get("focus_missing_mesh") or 0) > 0:
            c[(r.get("miss_cx"), r.get("miss_cy"), r.get("miss_cz"), r.get("miss_horiz"))] += 1
            mh.append(r.get("miss_horiz"))
    if c:
        print(f"  miss coords top: {c.most_common(6)}")
        print(f"  miss_horiz med={med(mh)} p90={p90(mh)}")


def main():
    p = LOGS / "perf_20260826-112418_14164.jsonl"
    u = load(p)
    print(
        "spikes",
        len(u),
        "first",
        u[0].get("player_x"),
        u[0].get("player_z"),
        "last",
        u[-1].get("player_x"),
        u[-1].get("player_z"),
    )
    s = segs(u)
    print("segments", len(s), "longest stands:")
    for i, j, x, z, n in sorted(s, key=lambda t: -t[4])[:15]:
        keep = med(g(u[i:j], "keep_cols"))
        print(f"  i={i:4d}-{j:4d} n={n:4d} xz=({x},{z}) keep={keep}")
    wp = [t for t in s if t[4] >= 8]
    print("stands>=8 count", len(wp))
    for t in wp[:20]:
        print(" ", t)
    print(" ... last 8:")
    for t in wp[-8:]:
        print(" ", t)

    mv = moving(u)
    print("\nMOVE n", len(mv), "not-move", len(u) - len(mv))
    dump_win("ALL", u)
    dump_win("MOVING", mv)
    land = [r for r in u if int(r.get("keep_cols") or 0) >= 160]
    dump_win("LAND keep>=160", land)
    dump_win("ENTER first 80", u[:80])
    dump_win("TAIL last 80", u[-80:])
    long = max(s, key=lambda t: t[4])
    dump_win(f"LONGEST STAND {long[2]},{long[3]}", u[long[0] : long[1]])
    s2 = sorted(s, key=lambda t: -t[4])
    if len(s2) > 1:
        a = s2[1]
        dump_win(f"2nd STAND {a[2]},{a[3]}", u[a[0] : a[1]])
    if len(s2) > 2:
        a = s2[2]
        dump_win(f"3rd STAND {a[2]},{a[3]}", u[a[0] : a[1]])

    # compare 100413 stand
    sot = load(LOGS / "perf_20260826-100413_20156.jsonl")
    print("\n======== 100413 ALL ========")
    dump_win("100413 ALL", sot)
    dump_win("100413 MOVING", moving(sot))


if __name__ == "__main__":
    main()
