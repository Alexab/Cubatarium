#!/usr/bin/env python3
"""Miss column + relight time series for 195810."""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

p = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260817-195810_30216.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
cruise = [r for r in rows if (r.get("chunk_count") or 0) >= 80]
miss = [r for r in cruise if r.get("focus_missing_mesh")]
print("cruise", len(cruise), "miss", len(miss))
print("miss_horiz", Counter(r.get("miss_horiz") for r in miss).most_common())
print("miss_cy", Counter(r.get("miss_cy") for r in miss).most_common())
print("miss_cx/cz", Counter((r.get("miss_cx"), r.get("miss_cz")) for r in miss).most_common(12))
print("pending_light_focus", Counter(r.get("pending_light_focus") for r in miss).most_common())
print("pending_light (bool)", Counter(r.get("pending_light") for r in miss).most_common())


def pct(vals, q):
    if not vals:
        return None
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(round((len(vals) - 1) * q)))]


print("pending_gpu_queued_n miss med", pct([r["pending_gpu_queued_n"] for r in miss if isinstance(r.get("pending_gpu_queued_n"), (int, float))], 0.5))
print("heal_deferred_for_miss", Counter(r.get("heal_deferred_for_miss") for r in cruise).most_common(6))

# cz buckets
for cz in range(50, 56):
    b = [r for r in cruise if r.get("focus_cz") == cz]
    if not b:
        continue
    miss_b = [r for r in b if r.get("focus_missing_mesh")]
    o = [r["opaque_draw_n"] for r in b if isinstance(r.get("opaque_draw_n"), (int, float))]
    pl = [r["pending_light_n"] for r in b if isinstance(r.get("pending_light_n"), (int, float))]
    fm = [r["dirty_fm_n"] for r in b if isinstance(r.get("dirty_fm_n"), (int, float))]
    pack = [r["opaque_gpu_packed_n"] for r in b if isinstance(r.get("opaque_gpu_packed_n"), (int, float))]
    pool = [r["gpu_pool_used_mb"] for r in b if isinstance(r.get("gpu_pool_used_mb"), (int, float))]
    fifo = [r["relight_fifo_n"] for r in b if isinstance(r.get("relight_fifo_n"), (int, float))]
    done = [r["relight_completed_n"] for r in b if isinstance(r.get("relight_completed_n"), (int, float))]
    skip = [r["mesh_dirty_schedule_skip_n"] for r in b if isinstance(r.get("mesh_dirty_schedule_skip_n"), (int, float))]
    uf7 = sum(1 for r in b if r.get("underfeet_reason") == 7)
    print(
        f"cz={cz} n={len(b)} miss={len(miss_b)} uf7={uf7} "
        f"opaque med={pct(o,0.5)} packed med={pct(pack,0.5)} pool med={round(pct(pool,0.5),2) if pool else None} "
        f"pl med={pct(pl,0.5)} fifo med={pct(fifo,0.5)} done med={pct(done,0.5)} "
        f"fm med={pct(fm,0.5)} skip med={pct(skip,0.5)} "
        f"horiz={Counter(r.get('miss_horiz') for r in miss_b).most_common(4)}"
    )

# cool pending but still miss
cool_miss = [r for r in miss if (r.get("pending_light_n") or 0) <= 2]
print("cool miss n", len(cool_miss))
print("  horiz", Counter(r.get("miss_horiz") for r in cool_miss).most_common())
print("  cz", Counter(r.get("focus_cz") for r in cool_miss).most_common())
print("  fm med", pct([r["dirty_fm_n"] for r in cool_miss if isinstance(r.get("dirty_fm_n"), (int, float))], 0.5))
print("  sch med", pct([r["mesh_dirty_schedule_ok_n"] for r in cool_miss if isinstance(r.get("mesh_dirty_schedule_ok_n"), (int, float))], 0.5))

# compare 163559 pool at late
p2 = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260817-163559_2464.jsonl")
c2 = [json.loads(l) for l in p2.open(encoding="utf-8") if l.strip()]
c2 = [r for r in c2 if (r.get("chunk_count") or 0) >= 80]
late2 = [r for r in c2 if (r.get("focus_cz") or 0) >= 55]
late1 = [r for r in cruise if (r.get("focus_cz") or 0) >= 55]
print("\nlate pool 195810", pct([r["gpu_pool_used_mb"] for r in late1], 0.5), "min", min(r["gpu_pool_used_mb"] for r in late1))
print("late pool 163559", pct([r["gpu_pool_used_mb"] for r in late2], 0.5), "min", min(r["gpu_pool_used_mb"] for r in late2))
print("late packed 195810", pct([r["opaque_gpu_packed_n"] for r in late1], 0.5))
print("late packed 163559", pct([r["opaque_gpu_packed_n"] for r in late2], 0.5))
