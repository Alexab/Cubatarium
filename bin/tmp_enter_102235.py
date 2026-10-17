import json
from pathlib import Path
from collections import Counter

p = Path("bin/logs/enter_lit_20260827-102235.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("n", len(rows), "ready", any(r.get("spawn_mesh_ring_ready") == 1 for r in rows))
print("blockers", Counter(r.get("ring_blocker") for r in rows))
prev = None
for r in rows:
    key = (
        r.get("mesh_missing_greedy"),
        r.get("dirty_n"),
        r.get("ring_blocker"),
        r.get("mesh_async_pending"),
        r.get("stuck_dirty_cx"),
        r.get("stuck_dirty_cy"),
        r.get("stuck_dirty_cz"),
        r.get("visibility_debt"),
        r.get("underfeet_present_ready"),
    )
    if key != prev:
        print(
            f"t={r.get('elapsed_ms'):.0f} miss={key[0]} dn={key[1]} block={key[2]} "
            f"async={key[3]} stuck=({key[4]},{key[5]},{key[6]}) debt={key[7]} uf={key[8]}"
        )
        prev = key

# perf SoftDefer for this run
perf = Path("bin/logs/perf_20260827-102142_18464.jsonl")
if perf.exists():
    prows = [json.loads(l) for l in perf.open(encoding="utf-8") if l.strip()]
    keys = [
        "softdefer_held_n",
        "softdefer_empty_placeholder_n",
        "softdefer_empty_near_n",
        "miss_cy",
        "nearest_miss_cx",
        "nearest_miss_cy",
        "nearest_miss_cz",
        "spawn_mesh_ring_ready",
        "mesh_missing_greedy",
        "lighting_relight_deferred",
    ]
    # find keys that exist
    sample = prows[len(prows) // 2] if prows else {}
    present = [k for k in keys if k in sample or any(k in r for r in prows[:5])]
    print("perf n", len(prows), "sample keys hit", [k for k in keys if any(k in r for r in prows[:: max(1, len(prows)//20)])])
    # dump unique nearest miss
    miss_coords = Counter()
    for r in prows:
        cx = r.get("nearest_miss_cx", r.get("miss_cx"))
        cy = r.get("nearest_miss_cy", r.get("miss_cy"))
        cz = r.get("nearest_miss_cz", r.get("miss_cz"))
        if cx is not None:
            miss_coords[(cx, cy, cz)] += 1
        # also try nested
        for kk in ("nearest_missing", "miss_witness", "hole"):
            if isinstance(r.get(kk), dict):
                d = r[kk]
                miss_coords[(d.get("x"), d.get("y"), d.get("z"))] += 1
    print("miss_coords top", miss_coords.most_common(8))
    # SoftDefer series
    for i in [0, len(prows)//4, len(prows)//2, -1]:
        r = prows[i]
        print(
            "perf", i,
            {k: r.get(k) for k in r if any(s in k for s in ("softdefer", "miss", "spawn_mesh", "lighting_relight", "pending_light", "focus"))}
        )
