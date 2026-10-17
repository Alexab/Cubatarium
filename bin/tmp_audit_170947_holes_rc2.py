#!/usr/bin/env python3
"""170947 deep RC follow-up with correct key names."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

LOG = Path(r"E:\Work\Home\Cubatarium\bin\logs\perf_20260908-170947_28864.jsonl")


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            rows.append(json.loads(line))
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def p90(xs):
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return None
    k = (len(xs) - 1) * 0.9
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    return xs[f] if f == c else xs[f] + (xs[c] - xs[f]) * (k - f)


def g(r, *ks, default=0):
    for k in ks:
        if k in r and r[k] is not None:
            return r[k]
    return default


def rate(rows, key):
    if len(rows) < 2:
        return None
    return (int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)) / max(
        1, len(rows) - 1
    )


def main():
    spikes = [r for r in load(LOG) if r.get("kind") == "spike"]
    cruise = []
    for i in range(1, len(spikes)):
        cur, prev = spikes[i], spikes[i - 1]
        if int(g(cur, "keep_cols") or 0) < 160:
            continue
        if (cur.get("player_x"), cur.get("player_z")) == (
            prev.get("player_x"),
            prev.get("player_z"),
        ):
            continue
        cruise.append(cur)
    print(f"spikes={len(spikes)} cruise={len(cruise)}")

    keys = [
        ("vb_focus", "visible_black_focus_n"),
        ("vb_raw", "visible_black_focus_raw_n"),
        ("vb_no_ticket", "visible_black_no_ticket_n"),
        ("vb_stalled", "visible_black_stalled_n"),
        ("vb_progress", "visible_black_progress_n"),
        ("fifo", "relight_fifo_n"),
        ("fifo_drop", "relight_fifo_dropped"),
        ("gpu_pending", "pending_gpu_applies_n"),
        ("gpu_queued", "pending_gpu_queued_n"),
        ("gpu_kicked", "pending_gpu_kicked_n"),
        ("gpu_kick", "gpu_kick_n"),
        ("dirty_fm", "dirty_fm_n"),
        ("dirty_remesh", "dirty_remesh_n"),
        ("dirty_n", "dirty_n"),
        ("empty_backlog", "empty_backlog_n"),
        ("softdefer_age", "softdefer_empty_age_max_frames"),
        ("softdefer_held", "softdefer_held_n"),
        ("softdefer_held_age", "softdefer_held_age_max"),
        ("dark_stale_near", "dark_face_stale_near_n"),
        ("dark_void_near", "dark_face_void_near_n"),
        ("dark_near", "dark_face_near_n"),
        ("focus_dark_mesh", "focus_dark_mesh"),
        ("focus_pending_dark", "focus_pending_dark"),
        ("focus_not_rr", "focus_not_render_ready"),
        ("near_holes", "near_focus_holes"),
        ("visual_holes", "visual_holes"),
        ("opaque_draw", "opaque_draw_n"),
        ("opaque_gpu_packed", "opaque_gpu_packed_n"),
        ("opaque_refs_rr", "opaque_refs_render_ready"),
        ("opaque_refs_cpu", "opaque_refs_cpu_vis"),
        ("mesh_replace_hole_avoided", "mesh_replace_hole_avoided"),
        ("mesh_completed_discarded", "mesh_completed_discarded"),
        ("mesh_apply_stale", "mesh_apply_stale"),
        ("free_chunk?", "free_chunk_live_n"),
        ("prep_drop_remesh", "prep_drop_remesh_n"),
        ("sticky_erase_remesh", "sticky_erase_remesh_commit_n"),
        ("miss_witness_remesh", "miss_witness_remesh_n"),
        ("stage_skip_remesh_pl", "stage_skip_remesh_pending_light"),
        ("remesh_protect", "remesh_protect_lit_settle_n"),
        ("skip_softdefer", "mesh_dirty_schedule_skip_softdefer_n"),
        ("skip_remesh_starve", "mesh_dirty_schedule_skip_remesh_starve_n"),
        ("mark_relit_enqueue_fm", "mark_relit_enqueue_first_mesh_n"),
        ("mark_relit_skip_dirty", "mark_relit_skip_already_dirty_n"),
        ("relight_apply_n", "relight_apply_n"),
        ("relight_apply_to_markrelit", "relight_apply_to_markrelit_n"),
        ("chunk_meshed_unlit", "chunk_meshed_unlit"),
        ("chunk_meshed_unlit_hidden", "chunk_meshed_unlit_hidden"),
        ("chunk_meshed_unlit_preview", "chunk_meshed_unlit_preview"),
        ("unfinished", "unfinished_visual"),
        ("no_mesh", "column_loaded_no_mesh_n"),
        ("pl", "pending_light"),
        ("pl_focus", "pending_light_focus"),
        ("sd_stuck", "softdefer_empty_stuck_n"),
        ("sd_owned_no_gpu", "softdefer_owned_no_gpu_n"),
        ("sd_placeholder", "softdefer_empty_placeholder_n"),
        ("sd_owned", "softdefer_empty_owned_n"),
        ("abort_sched", "abort_schedule_final"),
        ("phase_abort_heavy", "phase_abort_heavy"),
        ("keep_ring_shrink", "keep_ring_shrink_n"),
        ("miss_stuck_run", "miss_stuck_run_frames"),
        ("miss_horiz", "miss_horiz"),
        ("wall", "wall_ms"),
        ("stream", "stream_ms"),
        ("mesh_emerge", "mesh_emerge_ms"),
        ("world_phase", "world_streaming_phase_ms"),
        ("visibility_debt", "visibility_debt"),
    ]

    print("\n=== cruise med/p90/max (corrected keys) ===")
    for label, key in keys:
        xs = [g(r, key) for r in cruise]
        if all(x == 0 for x in xs) and not any(key in r for r in cruise[:5]):
            # check presence
            if not any(key in r for r in spikes[::20]):
                print(f"  {label} ({key}): ABSENT")
                continue
        print(
            f"  {label}: med={med(xs):.3f} p90={p90(xs):.3f} max={max(float(x) for x in xs):.3f}"
        )

    print("\n=== rates /spike full ===")
    for key in [
        "mesh_discarded_late",
        "mesh_discarded_late_epoch",
        "mesh_completed_discarded",
        "mesh_replace_hole_avoided",
        "mesh_apply_stale",
        "prep_drop_remesh_n",
        "sticky_erase_remesh_commit_n",
        "miss_witness_remesh_n",
        "relight_fifo_dropped",
        "relight_fifo_drop_n",
        "softdefer_witness_retarget",
        "softdefer_capture_retarget_n",
        "softdefer_ingress_witness_n",
        "softdefer_empty_publish_avoided",
        "keep_ring_shrink_n",
        "mark_relit_enqueue_first_mesh_n",
        "mark_relit_skip_already_dirty_n",
        "relight_apply_to_markrelit_n",
        "fm_dirty_enqueue_n",
        "fm_dirty_enqueue_from_markrelit_n",
        "stale_repair_wave_n",
        "pending_light_dropped",
        "gpu_finish_n",
        "gpu_kick_n",
        "dirty_revisit_same_n",
        "focus_dirty_reconcile_delta",
    ]:
        if any(key in r for r in spikes[::30]):
            print(f"  {key}/spike={rate(spikes, key)}")

    # Early vs late cruise
    early, late = cruise[:25], cruise[-25:]
    print("\n=== early vs late cruise medians ===")
    for label, key in [
        ("unf", "unfinished_visual"),
        ("nm", "column_loaded_no_mesh_n"),
        ("uh", "chunk_meshed_unlit_hidden"),
        ("vb", "visible_black_focus_n"),
        ("vb_nt", "visible_black_no_ticket_n"),
        ("pl", "pending_light"),
        ("fifo", "relight_fifo_n"),
        ("sd", "softdefer_empty_stuck_n"),
        ("opaque_draw", "opaque_draw_n"),
        ("opaque_gpu", "opaque_gpu_packed_n"),
        ("dirty_rm", "dirty_remesh_n"),
        ("dirty_fm", "dirty_fm_n"),
        ("dark_near", "dark_face_near_n"),
        ("dark_stale", "dark_face_stale_near_n"),
        ("near_holes", "near_focus_holes"),
        ("miss_h", "miss_horiz"),
        ("disc", "mesh_discarded_late"),
        ("replace_avoid", "mesh_replace_hole_avoided"),
        ("gpu_pend", "pending_gpu_applies_n"),
        ("debt", "visibility_debt"),
    ]:
        e = med([g(r, key) for r in early])
        l = med([g(r, key) for r in late])
        print(f"  {label}: early={e} late={l}")

    # When uh rises vs when nm rises - approach signature
    print("\n=== approach signature: uh>nm vs nm-dominant ===")
    uh_dom = [r for r in cruise if float(g(r, "chunk_meshed_unlit_hidden")) > float(g(r, "column_loaded_no_mesh_n"))]
    nm_dom = [r for r in cruise if float(g(r, "column_loaded_no_mesh_n")) >= float(g(r, "chunk_meshed_unlit_hidden"))]
    print(f"  uh_dom={len(uh_dom)} nm_dom={len(nm_dom)}")
    for label, rows in [("UH", uh_dom), ("NM", nm_dom)]:
        if not rows:
            continue
        print(
            f"  [{label}] uh={med([g(r,'chunk_meshed_unlit_hidden') for r in rows])} "
            f"nm={med([g(r,'column_loaded_no_mesh_n') for r in rows])} "
            f"vb={med([g(r,'visible_black_focus_n') for r in rows])} "
            f"dark={med([g(r,'dark_face_near_n') for r in rows])} "
            f"stale={med([g(r,'dark_face_stale_near_n') for r in rows])} "
            f"fifo={med([g(r,'relight_fifo_n') for r in rows])} "
            f"pl={med([g(r,'pending_light') for r in rows])} "
            f"sd={med([g(r,'softdefer_empty_stuck_n') for r in rows])} "
            f"dirty_rm={med([g(r,'dirty_remesh_n') for r in rows])}"
        )

    # opaque_draw drop correlating with approach
    print("\n=== opaque_draw / gpu_packed vs unfinished timeline ===")
    for i, r in enumerate(cruise):
        if i % 4 != 0 and i != len(cruise) - 1:
            continue
        print(
            "  i={:02d} px={:6.1f} unf={:3} nm={:3} uh={:3} vb={:3} vbnt={:3} "
            "dark={:4} stale={:4} fifo={:3} pl={:3} sd={:2} "
            "odraw={:4} ogpu={:4} dirty_rm={:3} dirty_fm={:3} "
            "gpuP={:3} disc={:4} avoid={:5} miss_h={} focus_dark={} not_rr={}".format(
                i,
                float(g(r, "player_x")),
                int(g(r, "unfinished_visual")),
                int(g(r, "column_loaded_no_mesh_n")),
                int(g(r, "chunk_meshed_unlit_hidden")),
                int(g(r, "visible_black_focus_n")),
                int(g(r, "visible_black_no_ticket_n")),
                int(g(r, "dark_face_near_n")),
                int(g(r, "dark_face_stale_near_n")),
                int(g(r, "relight_fifo_n")),
                int(g(r, "pending_light")),
                int(g(r, "softdefer_empty_stuck_n")),
                int(g(r, "opaque_draw_n")),
                int(g(r, "opaque_gpu_packed_n")),
                int(g(r, "dirty_remesh_n")),
                int(g(r, "dirty_fm_n")),
                int(g(r, "pending_gpu_applies_n")),
                int(g(r, "mesh_discarded_late")),
                int(g(r, "mesh_replace_hole_avoided")),
                g(r, "miss_horiz"),
                g(r, "focus_dark_mesh"),
                g(r, "focus_not_render_ready"),
            )
        )

    # discarded growth windows
    print("\n=== discarded abs jumps ===")
    prev = None
    for r in spikes:
        cur = int(g(r, "mesh_discarded_late"))
        if prev is not None and cur > prev:
            print(
                f"  +{cur-prev} -> {cur} px={g(r,'player_x')} keep={g(r,'keep_cols')} "
                f"unf={g(r,'unfinished_visual')} uh={g(r,'chunk_meshed_unlit_hidden')} "
                f"vb={g(r,'visible_black_focus_n')} epoch={g(r,'mesh_discarded_late_epoch')} "
                f"jobmm={g(r,'mesh_discarded_late_job_mismatch')} "
                f"completed_disc={g(r,'mesh_completed_discarded')}"
            )
        prev = cur

    # SoftDefer stuck coords during cruise
    print("\n=== SoftDefer stuck locus (cruise unique) ===")
    loci = {}
    for r in cruise:
        n = int(g(r, "softdefer_empty_stuck_n"))
        if n <= 0:
            continue
        key = (
            int(g(r, "softdefer_empty_stuck_cx")),
            int(g(r, "softdefer_empty_stuck_cy")),
            int(g(r, "softdefer_empty_stuck_cz")),
            int(g(r, "softdefer_empty_stuck_horiz")),
        )
        loci[key] = loci.get(key, 0) + 1
    for k, c in sorted(loci.items(), key=lambda kv: -kv[1])[:15]:
        print(f"  stuck={k} count={c}")

    # miss witness
    print("\n=== miss witness (cruise) ===")
    miss_loci = {}
    for r in cruise:
        key = (int(g(r, "miss_cx")), int(g(r, "miss_cy")), int(g(r, "miss_cz")), int(g(r, "miss_horiz")))
        miss_loci[key] = miss_loci.get(key, 0) + 1
    for k, c in sorted(miss_loci.items(), key=lambda kv: -kv[1])[:10]:
        print(f"  miss={k} count={c}")

    print("\n=== miss_stuck_run / focus_not_rr / near_holes ===")
    print(
        f"  miss_stuck med={med([g(r,'miss_stuck_run_frames') for r in cruise])} "
        f"max={max(int(g(r,'miss_stuck_run_frames')) for r in cruise)}"
    )
    print(
        f"  focus_not_rr med={med([g(r,'focus_not_render_ready') for r in cruise])} "
        f"focus_dark med={med([g(r,'focus_dark_mesh') for r in cruise])} "
        f"near_holes med={med([g(r,'near_focus_holes') for r in cruise])}"
    )


if __name__ == "__main__":
    main()
