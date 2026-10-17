import json
import sys
from pathlib import Path
from collections import Counter

p = Path(sys.argv[1])
print("FILE", p.resolve(), "exists", p.exists(), "size", p.stat().st_size)
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("n", len(rows), "t0", rows[0].get("elapsed_ms"), "tN", rows[-1].get("elapsed_ms"))
ready = [r for r in rows if r.get("spawn_mesh_ring_ready") == 1]
print("ready_at", ready[0].get("elapsed_ms") if ready else None)
print("blockers", Counter(r.get("ring_blocker") for r in rows))
prev = None
for r in rows:
    key = (
        r.get("spawn_mesh_ring_ready"),
        r.get("mesh_missing_greedy"),
        r.get("dirty_n"),
        r.get("ring_blocker"),
        r.get("mesh_async_pending"),
        r.get("inflight"),
        r.get("stuck_dirty_cx"),
        r.get("stuck_dirty_cy"),
        r.get("stuck_dirty_cz"),
        r.get("visibility_debt"),
        r.get("enter_lit_quiesce"),
    )
    if key != prev:
        print(
            f"t={r.get('elapsed_ms'):.0f} ready={key[0]} miss={key[1]} dn={key[2]} "
            f"block={key[3]} async={key[4]} infl={key[5]} "
            f"stuck=({key[6]},{key[7]},{key[8]}) debt={key[9]} q={key[10]}"
        )
        prev = key
print("LAST", {k: rows[-1].get(k) for k in (
    "spawn_mesh_ring_ready","mesh_missing_greedy","dirty_n","ring_blocker",
    "mesh_async_pending","inflight","visibility_debt","underfeet_present_ready",
    "top_dirty_cx","top_dirty_cz","enter_lit_quiesce","fifo_n")})
