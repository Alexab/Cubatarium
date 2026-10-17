"""opaque_cpu vs render_ready + residency proxies on 091745."""
import json
from pathlib import Path
from statistics import median

rows = []
for line in Path("bin/logs/perf_20260825-091745_23048.jsonl").read_text(
    encoding="utf-8", errors="replace"
).splitlines():
    if line.startswith("{"):
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)

print("=== cpu vs ready on big drops ===")
for i in range(1, len(rows)):
    a = int(rows[i - 1].get("opaque_refs_cpu_vis") or 0)
    b = int(rows[i].get("opaque_refs_cpu_vis") or 0)
    if a - b < 80:
        continue
    ra = int(rows[i - 1].get("opaque_refs_render_ready") or 0)
    rb = int(rows[i].get("opaque_refs_render_ready") or 0)
    print(
        f"{i-1}->{i} cpu {a}->{b} ({b-a}) ready {ra}->{rb} ({rb-ra}) "
        f"keep={rows[i].get('keep_cols')} margin={rows[i].get('keep_margin_eff')} "
        f"pool={rows[i].get('gpu_pool_used_mb')}/{rows[i].get('gpu_pool_cap_mb')} "
        f"gpu_finish={rows[i].get('gpu_finish_n')} kick={rows[i].get('gpu_kick_n')} "
        f"softdefer_held={rows[i].get('softdefer_held_n')} "
        f"empty_avoid={rows[i].get('softdefer_empty_publish_avoided')} "
        f"meshed_unlit={rows[i].get('chunk_meshed_unlit')} "
        f"culled0={rows[i].get('chunk_meshed_culled0')} "
        f"draw_n={rows[i].get('opaque_draw_n')} packed={rows[i].get('opaque_gpu_packed_n')}"
    )

print("\n=== steady medians ===")
s = rows[60:]
keys = [
    "opaque_refs_cpu_vis",
    "opaque_refs_render_ready",
    "keep_cols",
    "keep_margin_eff",
    "gpu_pool_used_mb",
    "softdefer_held_n",
    "softdefer_empty_owned_n",
    "softdefer_empty_publish_avoided",
    "pending_gpu_applies_n",
    "gpu_finish_n",
    "gpu_kick_n",
    "mesh_dirty_gpu_n",
    "dirty_fm_n",
    "dirty_remesh_n",
    "chunk_meshed_unlit",
    "chunk_meshed_unlit_hidden",
    "opaque_draw_n",
    "opaque_gpu_packed_n",
]
for k in keys:
    xs = [r.get(k) for r in s if r.get(k) is not None]
    if not xs:
        print(f"{k}: missing")
        continue
    print(f"{k:40s} med={median(float(x) for x in xs):8.1f} max={max(float(x) for x in xs):8.1f}")

# gap cpu-ready = hidden by render-ready filter
print("\n=== (cpu-ready) = hide-filter magnitude ===")
gaps = [
    int(r.get("opaque_refs_cpu_vis") or 0) - int(r.get("opaque_refs_render_ready") or 0)
    for r in s
]
print(f"gap med={median(gaps)} p90={sorted(gaps)[int(0.9*len(gaps))]} max={max(gaps)}")

# when cpu drops, does ready drop same amount?
print("\n=== drop coupling cpu vs ready ===")
same = 0
hide_only = 0
cache_loss = 0
for i in range(1, len(rows)):
    dc = int(rows[i].get("opaque_refs_cpu_vis") or 0) - int(
        rows[i - 1].get("opaque_refs_cpu_vis") or 0
    )
    if dc > -50:
        continue
    dr = int(rows[i].get("opaque_refs_render_ready") or 0) - int(
        rows[i - 1].get("opaque_refs_render_ready") or 0
    )
    # if cpu drops but ready stable -> shouldn't happen if ready<=cpu
    # if both drop similarly -> cache eviction
    # if ready drops more than cpu -> more hide
    if abs(dc - dr) <= 20:
        same += 1
        kind = "coupled"
    elif dr < dc - 20:  # ready fell more (more negative)
        hide_only += 1
        kind = "ready_worse"
    else:
        cache_loss += 1
        kind = "cpu_worse"
    print(f"{i-1}->{i} dc={dc} dr={dr} {kind}")
print(f"summary coupled={same} ready_worse={hide_only} cpu_worse={cache_loss}")
