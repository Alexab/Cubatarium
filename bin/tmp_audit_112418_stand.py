#!/usr/bin/env python3
"""112418 long-stand evolution + stream composition."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

LOGS = Path(__file__).resolve().parents[1] / "bin" / "logs"


def load(p, kind="spike"):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == kind:
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


def slice_stats(lab, rows):
    keys = [
        "wall_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "mesh_dirty_tick_ms",
        "mesh_dirty_schedule_ms",
        "mesh_dirty_gpu_ms",
        "softdefer_empty_scan_ms",
        "relight_drain_ms",
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "visible_black_focus_n",
        "visible_black_no_ticket_n",
        "pending_light_focus",
        "dirty_n",
        "dirty_touch_n",
        "dirty_fm_n",
        "dirty_remesh_n",
        "focus_missing_mesh",
        "miss_horiz",
        "softdefer_empty_stuck_n",
        "opaque_refs_cpu_vis",
        "prep_unfinished_calls_n",
        "prep_unfinished_full_n",
        "first_mesh_schedule_cap",
        "remesh_schedule_cap",
        "mesh_schedule_final",
        "heal_deferred_for_miss",
        "underfeet_need",
        "underfeet_reason",
        "underfeet_stage",
    ]
    print(f"\n=== {lab} n={len(rows)} ===")
    if not rows:
        return
    for k in keys:
        xs = [r.get(k) for r in rows]
        print(f"  {k:32s} med={med(xs):8} p90={p90(xs):8}")
    print(
        f"  discard={rate(rows,'mesh_discarded_late')} "
        f"stale={rate(rows,'mesh_apply_stale')} "
        f"SiteB={rate(rows,'softdefer_capture_retarget_n')} "
        f"floor={rate(rows,'softdefer_capture_floor_hits')}"
    )
    miss = sum(1 for r in rows if int(r.get("focus_missing_mesh") or 0) > 0)
    print(f"  miss_frac={round(miss/len(rows),3)}")
    r0, r1 = rows[0], rows[-1]
    print(
        f"  miss {r0.get('miss_cx')},{r0.get('miss_cy')},{r0.get('miss_cz')} nh={r0.get('miss_horiz')}"
        f" -> {r1.get('miss_cx')},{r1.get('miss_cy')},{r1.get('miss_cz')} nh={r1.get('miss_horiz')}"
        f" focus {r0.get('focus_cx')},{r0.get('focus_cz')} -> {r1.get('focus_cx')},{r1.get('focus_cz')}"
    )
    print(
        f"  pending_cols sample: {r0.get('pending_cols')} || late: {r1.get('pending_cols')}"
    )


def main():
    p = LOGS / "perf_20260826-112418_14164.jsonl"
    u = load(p)
    periods = load(p, "period")
    print("spikes", len(u), "periods", len(periods))
    if periods:
        last = periods[-1]
        print(
            "last period frames=",
            last.get("frames"),
            "spikes=",
            last.get("spikes"),
            "wall max=",
            last.get("max_wall_ms"),
        )
        # crude duration: sum of period wall * frames? period is aggregated
        # spikes are ~every slow frame; count periods
        print("n periods", len(periods), "first keys frames", periods[0].get("frames"))

    # cruise: before long stand
    cruise = u[:229]
    stand = u[229:]
    slice_stats("CRUISE 0:229 (115,90)->(-32,74)", cruise)
    n = len(stand)
    for lab, sl in [
        ("STAND early 0-200", stand[:200]),
        ("STAND mid 2000-2200", stand[2000:2200]),
        ("STAND late -200", stand[-200:]),
        ("STAND all", stand),
    ]:
        slice_stats(lab, sl)

    # underfeet_need on stand: is it miss nh=0?
    need = [r for r in stand if int(r.get("underfeet_need") or 0)]
    print("\nstand underfeet_need", len(need), "/", len(stand))
    if need:
        from collections import Counter

        print("  uf_reason", Counter(r.get("underfeet_reason") for r in need).most_common(8))
        print("  uf_stage", Counter(r.get("underfeet_stage") for r in need).most_common(8))
        print(
            "  miss when need",
            Counter(
                (r.get("miss_cx"), r.get("miss_cy"), r.get("miss_cz"), r.get("miss_horiz"))
                for r in need
            ).most_common(5),
        )
        print(
            "  miss when !need",
            Counter(
                (r.get("miss_cx"), r.get("miss_cy"), r.get("miss_cz"), r.get("miss_horiz"))
                for r in stand
                if not int(r.get("underfeet_need") or 0)
            ).most_common(5),
        )

    # stream_ms attribution on late stand: which component dominates
    late = stand[-200:]
    print("\nlate stand stream vs emerge vs dirty vs scan")
    for r in late[::40]:
        print(
            f"  wall={r.get('wall_ms'):.0f} stream={r.get('stream_ms'):.1f} "
            f"emerge={r.get('mesh_emerge_ms'):.1f} dirty={r.get('mesh_dirty_tick_ms'):.1f} "
            f"sched={r.get('mesh_dirty_schedule_ms'):.1f} gpu={r.get('mesh_dirty_gpu_ms'):.1f} "
            f"scan={r.get('softdefer_empty_scan_ms'):.2f} unf={r.get('unfinished_visual')} "
            f"vb={r.get('visible_black_focus_n')} dirty_n={r.get('dirty_n')} "
            f"touch={r.get('dirty_touch_n')} fm={r.get('dirty_fm_n')} "
            f"cap={r.get('first_mesh_schedule_cap')}/{r.get('remesh_schedule_cap')} "
            f"ok={r.get('mesh_dirty_schedule_ok_n')} skip={r.get('mesh_dirty_schedule_skip_n')} "
            f"miss={r.get('miss_cx')},{r.get('miss_cy')},{r.get('miss_cz')} nh={r.get('miss_horiz')}"
        )

    # skip reasons on late stand
    skips = [
        "mesh_dirty_schedule_skip_pipeline_n",
        "mesh_dirty_schedule_skip_snapshot_n",
        "mesh_dirty_schedule_skip_softdefer_n",
        "mesh_dirty_schedule_skip_locked_n",
        "mesh_dirty_schedule_skip_orphan_n",
        "mesh_dirty_schedule_skip_remesh_starve_n",
        "mesh_dirty_schedule_skip_other_n",
    ]
    print("\nlate skip medians:")
    for k in skips:
        print(f"  {k} med={med([r.get(k) for r in late])}")


if __name__ == "__main__":
    main()
