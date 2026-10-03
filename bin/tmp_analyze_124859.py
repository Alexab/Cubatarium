#!/usr/bin/env python3
import json
import statistics as st
from pathlib import Path

LOGS = {
    "new": "bin/logs/perf_20260901-124859_22296.jsonl",
    "prev": "bin/logs/perf_20260901-102834_15836.jsonl",
    "gate": "bin/logs/perf_20260831-214536_23384.jsonl",
}


def load(path):
    ps = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "period":
                ps.append(r)
    return ps


def fly_segment(ps, stop_n):
    return ps[: len(ps) - stop_n] if len(ps) > stop_n else ps


STOP = {"new": 217, "prev": 52, "gate": 50}
KEYS = [
    "wall_ms", "stream_ms", "mesh_emerge_ms", "world_streaming_phase_ms",
    "prep_refresh_gap_ms", "prep_column_flow_drain_ms", "prep_refresh_pressure_ms",
    "unfinished_visual", "visible_black_focus_n", "focus_missing_mesh",
    "chunk_not_ready_n", "mesh_dirty_schedule_ok_n", "dirty_fm_n",
    "fm_dirty_to_gpu_finish_n", "ingress_debt_level", "opaque_idle_churn",
    "pool_unsync_uploads", "fluid_map_cpu_ms", "softdefer_empty_n",
]

for name, path in LOGS.items():
    ps = load(path)
    f = fly_segment(ps, STOP[name])
    print(f"\n=== {name} periods={len(ps)} fly={len(f)} ===")
    for k in KEYS:
        nums = [float(p.get(k) or 0) for p in f]
        nz = sum(1 for v in nums if v > 0)
        p95 = st.quantiles(nums, n=20)[18] if len(nums) > 5 else max(nums)
        print(f"  {k:38s} med={st.median(nums):8.1f} p95={p95:8.1f} nz={nz}/{len(nums)}")

ps = load(LOGS["new"])
f = fly_segment(ps, STOP["new"])
print("\n=== prep breakdown (new fly) ===")
for k in sorted(ps[0].keys()):
    if k.startswith("prep_") and k.endswith("_ms"):
        nums = [float(p.get(k) or 0) for p in f]
        if max(nums) > 0.5:
            print(f"  {k:40s} med={st.median(nums):7.1f} max={max(nums):7.1f}")

print("\n=== empty/hole proxies (new) ===")
for label, subset in [("fly", f), ("stop", ps[len(f):])]:
    unf = sum(1 for p in subset if float(p.get("unfinished_visual") or 0) > 0)
    miss = sum(1 for p in subset if float(p.get("focus_missing_mesh") or 0) > 0)
    empty = sum(1 for p in subset if float(p.get("softdefer_empty_n") or 0) > 0)
    print(f"  {label}: unf>0 {unf}/{len(subset)} miss>0 {miss}/{len(subset)} softdefer_empty>0 {empty}/{len(subset)}")

sp = []
for line in Path(LOGS["new"]).read_text(encoding="utf-8", errors="replace").splitlines():
    if line.startswith("{"):
        r = json.loads(line)
        if r.get("kind") == "spike":
            sp.append(r)
print("\n=== worst spikes (new) ===")
for s in sorted(sp, key=lambda x: -float(x.get("wall_ms") or 0))[:8]:
    print(
        f"  wall={s.get('wall_ms', 0):.0f} stream={s.get('stream_ms', 0):.0f} "
        f"emerge={s.get('mesh_emerge_ms', 0):.0f} fluid={s.get('fluid_map_cpu_ms', 0):.1f} "
        f"unf={s.get('unfinished_visual')} gap={s.get('prep_refresh_gap_ms', 0):.1f} "
        f"sched={s.get('mesh_dirty_schedule_ok_n')}"
    )

from collections import Counter
c = Counter(int(p.get("ingress_debt_level") or 0) for p in f)
print(f"\ningress_debt_level fly: {dict(c)}")
finish_nz = sum(1 for p in f if float(p.get("fm_dirty_to_gpu_finish_n") or 0) > 0)
print(f"fm_dirty_to_gpu_finish_n>0: {finish_nz}/{len(f)}")
