import json
from pathlib import Path
from collections import Counter

p = Path("bin/logs/enter_lit_20260827-143636.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("n", len(rows), "ready", any(r.get("spawn_mesh_ring_ready") == 1 for r in rows))

# stuck/top miss witness
for r in rows[::15]:
    print(
        f"t={r['elapsed_ms']:.0f} miss={r['mesh_missing_greedy']} dn={r['dirty_n']} "
        f"block={r['ring_blocker']} async={int(r['mesh_async_pending'])} "
        f"gpu={r['mesh_gpu_pending_near']} pg={r['pending_gpu_global']} "
        f"raa={r['remesh_after_apply_n']} skip_inf={r['dirty_schedule_skip_inflight_n']} "
        f"top=({r['top_dirty_cx']},{r['top_dirty_cz']}) "
        f"stuck=({r['stuck_dirty_cx']},{r['stuck_dirty_cy']},{r['stuck_dirty_cz']}) "
        f"debt={r['visibility_debt']} gate={r['gate_done_n']} pend={r['pending_global']} "
        f"q={r['enter_lit_quiesce']}"
    )

# when dn=19 what stuck coords appear
stuck_when_dn = Counter()
for r in rows:
    if r.get("dirty_n", 0) >= 15:
        stuck_when_dn[(r["stuck_dirty_cx"], r["stuck_dirty_cy"], r["stuck_dirty_cz"])] += 1
print("stuck when dn>=15", stuck_when_dn.most_common(8))

# perf if exists
perf_glob = sorted(Path("bin/logs").glob("perf_20260827-1436*.jsonl"))
if perf_glob:
    perf = perf_glob[0]
    print("perf", perf.name)
    prows = []
    for line in perf.open(encoding="utf-8", errors="replace"):
        line = line.strip()
        if not line:
            continue
        try:
            prows.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    print("perf rows", len(prows))
    keys = [
        "miss_cx", "miss_cy", "miss_cz", "nearest_miss_cx", "nearest_miss_cy",
        "nearest_miss_cz", "focus_missing_mesh", "mesh_missing_greedy",
        "spawn_mesh_ring_ready", "dirty_n", "softdefer_held_n",
        "softdefer_empty_placeholder_n", "enter_lit_quiesce",
    ]
    for i in [0, len(prows)//4, len(prows)//2, 3*len(prows)//4, -1]:
        if not prows:
            break
        r = prows[i]
        hit = {k: r.get(k) for k in keys if k in r}
        print("perf", i, hit)
