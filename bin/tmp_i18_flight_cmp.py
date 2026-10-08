#!/usr/bin/env python3
import json
import statistics as st
from pathlib import Path

FLIGHTS = [
    ("I17", "bin/logs/perf_20260831-214536_23384.jsonl"),
    ("I18", "bin/logs/perf_20260901-075743_32052.jsonl"),
    ("SoT", "bin/logs/perf_20260831-144853_32564.jsonl"),
]

KEYS = [
    "wall_ms", "stream_ms", "mesh_emerge_ms", "prep_refresh_gap_ms",
    "unfinished_visual", "visible_black_focus_n", "visible_black_stalled_n",
    "visual_holes", "focus_missing_mesh", "mesh_dirty_schedule_ok_n",
    "dirty_fm_n", "fm_dirty_to_gpu_finish_n", "fm_dirty_gpu_watch_n",
    "fm_dirty_gpu_watch_max_age", "gpu_finish_watch_rim_n", "ingress_debt_level",
    "ingress_debt_streak", "softdefer_witness_retarget_delta",
    "mesh_apply_stale_delta", "mesh_discarded_late_delta", "freechunk_live_n",
    "opaque_idle_churn", "fog_hole_debt", "admission_carve_out", "miss_horiz",
    "gpu_finish_n", "gpu_kick_n", "mesh_replace_hole_avoided_delta",
]


def load(path: str):
    ps = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "period":
            ps.append(r)
    return ps


def summarize(label, path):
    ps = load(path)
    cruise = [p for p in ps if p.get("moving")]
    print(f"\n=== {label} periods={len(ps)} moving={len(cruise)} ===")
    for k in KEYS:
        nums = [float(p[k]) for p in cruise if p.get(k) is not None]
        if not nums:
            continue
        nz = sum(1 for v in nums if v > 0)
        print(
            f"  {k:40s} med={st.median(nums):7.1f} "
            f"max={max(nums):7.0f} nz={nz}/{len(nums)}"
        )


def worst_i18():
    ps = load("bin/logs/perf_20260901-075743_32052.jsonl")
    worst = sorted(
        ps,
        key=lambda p: (
            -p.get("unfinished_visual", 0),
            -p.get("visible_black_focus_n", 0),
            -p.get("softdefer_witness_retarget_delta", 0),
        ),
    )[:12]
    print("\n=== I18 worst periods ===")
    for p in worst:
        i = ps.index(p)
        print(
            f"i={i:3d} unf={p.get('unfinished_visual')} vb={p.get('visible_black_focus_n')} "
            f"vh={p.get('visual_holes')} wit_d={p.get('softdefer_witness_retarget_delta')} "
            f"debt={p.get('ingress_debt_level')} watch={p.get('fm_dirty_gpu_watch_n')} "
            f"watch_age={p.get('fm_dirty_gpu_watch_max_age')} fin={p.get('fm_dirty_to_gpu_finish_n')} "
            f"sched={p.get('mesh_dirty_schedule_ok_n')} emerge={p.get('mesh_emerge_ms', 0):.0f} "
            f"stream={p.get('stream_ms', 0):.0f} opaque={p.get('opaque_idle_churn')} "
            f"freechunk={p.get('freechunk_live_n')} stale_d={p.get('mesh_apply_stale_delta')}"
        )


if __name__ == "__main__":
    for label, path in FLIGHTS:
        summarize(label, path)
    worst_i18()
