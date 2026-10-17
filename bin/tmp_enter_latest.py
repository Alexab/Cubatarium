import json
from pathlib import Path
from collections import Counter

logs = sorted(Path("bin/logs").glob("enter_lit_*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)
print("latest", logs[0] if logs else None)
if not logs:
    raise SystemExit(0)
p = logs[0]
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("n", len(rows), "ready", any(r.get("spawn_mesh_ring_ready") == 1 for r in rows))
print("blockers", Counter(r.get("ring_blocker") for r in rows))
print("ready times", [r.get("elapsed_ms") for r in rows if r.get("spawn_mesh_ring_ready") == 1][:5])
prev = None
for r in rows:
    key = (r.get("mesh_missing_greedy"), r.get("dirty_n"), r.get("ring_blocker"), r.get("mesh_async_pending"), r.get("stuck_dirty_cx"), r.get("stuck_dirty_cy"), r.get("stuck_dirty_cz"), r.get("visibility_debt"), r.get("underfeet_present_ready"), r.get("spawn_mesh_ring_ready"), r.get("pending_global"))
    if key != prev:
        print(f"t={r.get('elapsed_ms'):.0f} miss={key[0]} dn={key[1]} block={key[2]} async={key[3]} stuck=({key[4]},{key[5]},{key[6]}) debt={key[7]} uf={key[8]} ready={key[9]} pend={key[10]}")
        prev = key
