import json
from pathlib import Path
from collections import Counter

logs = sorted(Path("bin/logs").glob("enter_lit_*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)
p = logs[0]
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print(p.name, "n", len(rows), "ready", any(r.get("spawn_mesh_ring_ready") == 1 for r in rows))
if rows:
    ready_t = [r.get("elapsed_ms") for r in rows if r.get("spawn_mesh_ring_ready") == 1]
    print("first_ready_ms", ready_t[0] if ready_t else None)
    print("blockers", Counter(r.get("ring_blocker") for r in rows))
    r = rows[-1]
    print("last", {k: r.get(k) for k in ("elapsed_ms","mesh_missing_greedy","dirty_n","ring_blocker","spawn_mesh_ring_ready","pending_global","visibility_debt","gate_miss_cx","gate_miss_cy","gate_miss_cz","gate_miss_soft_held","gate_miss_defer")})
    prev = None
    for r in rows[:40]:
        key = (r.get("mesh_missing_greedy"), r.get("dirty_n"), r.get("ring_blocker"), r.get("spawn_mesh_ring_ready"), r.get("pending_global"))
        if key != prev:
            print(f"t={r.get('elapsed_ms'):.0f} miss={key[0]} dn={key[1]} block={key[2]} ready={key[3]} pend={key[4]}")
            prev = key
