#!/usr/bin/env python3
"""Deep RC audit for manual 170947: hole-on-approach."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

LOG = Path(r"E:\Work\Home\Cubatarium\bin\logs\perf_20260908-170947_28864.jsonl")

KEYS_OF_INTEREST = [
    "kind",
    "t_ms",
    "player_x",
    "player_z",
    "keep_cols",
    "focus_missing_mesh",
    "unfinished_visual",
    "column_loaded_no_mesh_n",
    "chunk_meshed_unlit_hidden",
    "visual_holes",
    "visual_holes_n",
    "holes",
    "miss_horiz",
    "miss_horiz_med",
    "opaque_cull_ms",
    "scene_opaque_cull_ms",
    "mesh_discarded_late",
    "mesh_discarded_late_delta",
    "mesh_discarded_late_epoch",
    "mesh_discarded_late_job_mismatch",
    "relight_discarded_late",
    "softdefer_empty_stuck_n",
    "softdefer_age_max",
    "softdefer_owned_no_gpu_n",
    "softdefer_empty_publish_avoided",
    "softdefer_capture_retarget_n",
    "softdefer_ingress_witness_n",
    "pending_light",
    "pending_light_n",
    "pending_light_focus",
    "pending_light_dropped",
    "relight_fifo",
    "relight_fifo_depth",
    "visible_black_focus",
    "vb_no_ticket",
    "vb_stalled",
    "remesh_n",
    "dirty_fm",
    "dirty_remesh",
    "mesh_dirty_schedule_ok",
    "gpu_pending",
    "gpu_kick",
    "fully_dark_n",
    "dark_face_stale",
    "keep_lit",
    "opaque_draw_n",
    "visibility_debt",
    "empty_backlog",
    "wall_ms",
    "phase_ms",
    "stream_ms",
    "mesh_emerge_ms",
]


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            rows.append(json.loads(line))
    return rows


def med(xs):
    xs = [x for x in xs if x is not None]
    return st.median(xs) if xs else None


def pctl(xs, p):
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return None
    if len(xs) == 1:
        return xs[0]
    k = (len(xs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    if f == c:
        return xs[f]
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def rate(rows, key):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)
    return d / max(1, len(rows) - 1)


def get(r, *names, default=None):
    for n in names:
        if n in r and r[n] is not None:
            return r[n]
    return default


def main():
    all_rows = load(LOG)
    spikes = [r for r in all_rows if r.get("kind") == "spike"]
    print(f"all={len(all_rows)} spikes={len(spikes)}")
    if not spikes:
        print("NO SPIKES — sample first row keys:")
        print(sorted(all_rows[0].keys())[:80] if all_rows else None)
        return

    # key inventory from first spike
    keys = sorted(spikes[0].keys())
    print("\n--- key hits ---")
    for needle in [
        "discard",
        "soft",
        "hole",
        "miss",
        "vb",
        "black",
        "opaque",
        "pending_light",
        "fifo",
        "remesh",
        "fully",
        "dark",
        "keep",
        "gpu",
        "focus",
        "unfin",
        "no_mesh",
        "unlit",
        "dirty",
        "epoch",
        "publish",
        "park",
        "stale",
        "relight",
        "wall",
        "phase",
        "stream",
        "emerge",
        "abort",
        "empty",
        "visib",
    ]:
        hits = [k for k in keys if needle in k.lower()]
        if hits:
            print(f"{needle}: {hits}")

    # cruise = keep>=160 moving
    cruise = []
    for i in range(1, len(spikes)):
        cur, prev = spikes[i], spikes[i - 1]
        keep = int(get(cur, "keep_cols", default=0) or 0)
        if keep < 160:
            continue
        if (cur.get("player_x"), cur.get("player_z")) == (
            prev.get("player_x"),
            prev.get("player_z"),
        ):
            continue
        cruise.append(cur)
    print(f"\ncruise_moving keep>=160: {len(cruise)}")

    def series(rows, *names):
        return [float(get(r, *names, default=0) or 0) for r in rows]

    metrics = {
        "focus_missing": ("focus_missing_mesh",),
        "unfinished": ("unfinished_visual",),
        "no_mesh": ("column_loaded_no_mesh_n", "no_mesh_n"),
        "unlit_hidden": ("chunk_meshed_unlit_hidden",),
        "holes": ("visual_holes_n", "visual_holes", "holes_n"),
        "miss_horiz": ("miss_horiz", "focus_miss_horiz"),
        "vb_focus": ("visible_black_focus",),
        "vb_no_ticket": ("vb_no_ticket",),
        "vb_stalled": ("vb_stalled",),
        "softdefer_stuck": ("softdefer_empty_stuck_n",),
        "softdefer_age": ("softdefer_age_max",),
        "softdefer_owned_no_gpu": ("softdefer_owned_no_gpu_n",),
        "pending_light": ("pending_light", "pending_light_n"),
        "pending_light_focus": ("pending_light_focus",),
        "fifo": ("relight_fifo", "relight_fifo_depth", "relight_completed_fifo"),
        "gpu_pending": ("gpu_pending", "pending_gpu_apply"),
        "gpu_kick": ("gpu_kick", "gpu_kick_n"),
        "dirty_fm": ("dirty_fm", "mesh_dirty_fm"),
        "dirty_remesh": ("dirty_remesh", "mesh_dirty_remesh"),
        "opaque_cull": ("scene_opaque_cull_ms", "opaque_cull_ms"),
        "wall": ("wall_ms",),
        "phase": ("phase_ms",),
        "stream": ("stream_ms",),
        "mesh_emerge": ("mesh_emerge_ms",),
        "visibility_debt": ("visibility_debt",),
        "empty_backlog": ("empty_backlog",),
        "keep_cols": ("keep_cols",),
        "dark_face_stale": ("dark_face_stale",),
        "fully_dark": ("fully_dark_n", "chunk_fully_dark_n"),
        "mesh_discarded_late_abs": ("mesh_discarded_late",),
        "mesh_discarded_late_delta": ("mesh_discarded_late_delta",),
        "mesh_discarded_late_epoch": ("mesh_discarded_late_epoch",),
        "softdefer_empty_pub_avoid": ("softdefer_empty_publish_avoided",),
    }

    print("\n=== cruise med/p90/max ===")
    for name, knames in metrics.items():
        xs = []
        for r in cruise:
            v = get(r, *knames)
            if v is not None:
                xs.append(float(v))
        if not xs:
            # try presence on any spike
            present = any(get(r, *knames) is not None for r in spikes)
            print(f"  {name}: MISSING (present_any={present})")
            continue
        print(
            f"  {name}: n={len(xs)} med={med(xs):.3f} p90={pctl(xs,90):.3f} max={max(xs):.3f}"
        )

    print("\n=== cumulative rates /spike (full spikes) ===")
    for key in [
        "mesh_discarded_late",
        "mesh_discarded_late_epoch",
        "mesh_discarded_late_job_mismatch",
        "relight_discarded_late",
        "softdefer_empty_publish_avoided",
        "softdefer_capture_retarget_n",
        "softdefer_ingress_witness_n",
        "softdefer_capture_floor_hits",
        "pending_light_dropped",
        "relight_fifo_drop",
        "mark_dirty",
        "mark_relit",
        "remesh_scheduled",
        "first_mesh_scheduled",
        "gpu_apply_n",
        "mesh_apply_n",
        "collect_stale_dark_n",
        "stale_dark_enqueued",
        "fully_dark_requeue",
    ]:
        if key in spikes[0] or any(key in r for r in spikes[::10]):
            print(f"  {key}/spike={rate(spikes, key)}")
        else:
            # fuzzy
            hits = [k for k in keys if key.replace("_n", "") in k]
            if hits:
                print(f"  {key} not exact; close={hits[:5]}")

    # correlate: when focus_missing rises / holes high — what else high?
    print("\n=== focus_missing==1 cruise co-occurring medians ===")
    miss = [r for r in cruise if int(get(r, "focus_missing_mesh", default=0) or 0) == 1]
    ok = [r for r in cruise if int(get(r, "focus_missing_mesh", default=0) or 0) == 0]
    print(f"  miss_n={len(miss)} ok_n={len(ok)}")

    def dump_group(label, rows):
        if not rows:
            print(f"  [{label}] empty")
            return
        for name, knames in [
            ("unf", ("unfinished_visual",)),
            ("nm", ("column_loaded_no_mesh_n",)),
            ("uh", ("chunk_meshed_unlit_hidden",)),
            ("vb", ("visible_black_focus",)),
            ("vb_nt", ("vb_no_ticket",)),
            ("sd_stuck", ("softdefer_empty_stuck_n",)),
            ("sd_age", ("softdefer_age_max",)),
            ("pl", ("pending_light", "pending_light_n")),
            ("fifo", ("relight_fifo", "relight_fifo_depth")),
            ("gpu_pen", ("gpu_pending", "pending_gpu_apply")),
            ("disc_d", ("mesh_discarded_late_delta",)),
            ("disc_abs", ("mesh_discarded_late",)),
            ("opaque", ("scene_opaque_cull_ms", "opaque_cull_ms")),
            ("debt", ("visibility_debt",)),
            ("empty_bl", ("empty_backlog",)),
            ("miss_h", ("miss_horiz",)),
            ("dark_stale", ("dark_face_stale",)),
            ("dirty_fm", ("dirty_fm",)),
            ("dirty_rm", ("dirty_remesh",)),
        ]:
            xs = [float(get(r, *knames, default=0) or 0) for r in rows]
            print(f"  [{label}] {name} med={med(xs)} max={max(xs) if xs else None}")

    dump_group("MISS", miss)
    dump_group("OK", ok)

    # track discarded_late absolute growth vs holes/VB over time
    print("\n=== timeline (every ~5th cruise spike) ===")
    for i, r in enumerate(cruise):
        if i % 5 != 0 and i != len(cruise) - 1:
            continue
        print(
            "  i={:02d} t={:.0f} px={:.1f} pz={:.1f} miss={} unf={} nm={} uh={} vb={} vbnt={} "
            "sd={} age={} pl={} fifo={} discA={} discD={} gpuP={} debt={} empty={} "
            "opaque={:.2f} wall={:.1f} keep={}".format(
                i,
                float(get(r, "t_ms", default=0) or 0),
                float(get(r, "player_x", default=0) or 0),
                float(get(r, "player_z", default=0) or 0),
                get(r, "focus_missing_mesh", default="?"),
                get(r, "unfinished_visual", default="?"),
                get(r, "column_loaded_no_mesh_n", default="?"),
                get(r, "chunk_meshed_unlit_hidden", default="?"),
                get(r, "visible_black_focus", default="?"),
                get(r, "vb_no_ticket", default="?"),
                get(r, "softdefer_empty_stuck_n", default="?"),
                get(r, "softdefer_age_max", default="?"),
                get(r, "pending_light", "pending_light_n", default="?"),
                get(r, "relight_fifo", "relight_fifo_depth", default="?"),
                get(r, "mesh_discarded_late", default="?"),
                get(r, "mesh_discarded_late_delta", default="?"),
                get(r, "gpu_pending", "pending_gpu_apply", default="?"),
                get(r, "visibility_debt", default="?"),
                get(r, "empty_backlog", default="?"),
                float(get(r, "scene_opaque_cull_ms", "opaque_cull_ms", default=0) or 0),
                float(get(r, "wall_ms", default=0) or 0),
                get(r, "keep_cols", default="?"),
            )
        )

    # delta of discarded across cruise
    if cruise:
        d0 = int(get(cruise[0], "mesh_discarded_late", default=0) or 0)
        d1 = int(get(cruise[-1], "mesh_discarded_late", default=0) or 0)
        e0 = int(get(cruise[0], "mesh_discarded_late_epoch", default=0) or 0)
        e1 = int(get(cruise[-1], "mesh_discarded_late_epoch", default=0) or 0)
        a0 = int(get(cruise[0], "softdefer_empty_publish_avoided", default=0) or 0)
        a1 = int(get(cruise[-1], "softdefer_empty_publish_avoided", default=0) or 0)
        pl0 = int(get(cruise[0], "pending_light_dropped", default=0) or 0)
        pl1 = int(get(cruise[-1], "pending_light_dropped", default=0) or 0)
        print(
            f"\ncruise Δ mesh_discarded_late={d1-d0} epochΔ={e1-e0} "
            f"softdefer_empty_pub_avoidΔ={a1-a0} pending_light_droppedΔ={pl1-pl0}"
        )
        print(f"abs start/end discarded={d0}/{d1}")

    # find spikes where discarded_late_delta > 0
    print("\n=== spikes with mesh_discarded_late_delta>0 ===")
    n_pos = 0
    for r in spikes:
        d = int(get(r, "mesh_discarded_late_delta", default=0) or 0)
        if d > 0:
            n_pos += 1
            if n_pos <= 15:
                print(
                    f"  t={get(r,'t_ms')} d={d} abs={get(r,'mesh_discarded_late')} "
                    f"epoch={get(r,'mesh_discarded_late_epoch')} "
                    f"miss={get(r,'focus_missing_mesh')} vb={get(r,'visible_black_focus')} "
                    f"sd={get(r,'softdefer_empty_stuck_n')} unf={get(r,'unfinished_visual')}"
                )
    print(f"  total positive-delta spikes: {n_pos}/{len(spikes)}")

    # unlit_hidden high with drawable? look for remesh-blink signature:
    # high uh + has keep + rising discarded or rising remesh
    print("\n=== high unlit_hidden cruise (uh>=10) ===")
    hi = [
        r
        for r in cruise
        if float(get(r, "chunk_meshed_unlit_hidden", default=0) or 0) >= 10
    ]
    print(f"  count={len(hi)}")
    for r in hi[:10]:
        print(
            f"  t={get(r,'t_ms')} uh={get(r,'chunk_meshed_unlit_hidden')} "
            f"nm={get(r,'column_loaded_no_mesh_n')} unf={get(r,'unfinished_visual')} "
            f"vb={get(r,'visible_black_focus')} miss={get(r,'focus_missing_mesh')} "
            f"dark_stale={get(r,'dark_face_stale')} fifo={get(r,'relight_fifo','relight_fifo_depth')}"
        )


if __name__ == "__main__":
    main()
