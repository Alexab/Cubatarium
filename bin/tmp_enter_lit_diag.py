import json
from pathlib import Path
import sys

p = Path(sys.argv[1] if len(sys.argv) > 1 else "bin/logs/enter_lit_20260826-225948.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("file", p.name, "n", len(rows))
print("keys", sorted(rows[-1].keys()))
step = max(1, len(rows) // 10)
for i in list(range(0, len(rows), step)) + ([len(rows) - 1] if (len(rows) - 1) % step else []):
    r = rows[i]
    print(
        f"t={r.get('elapsed_ms'):.0f} ready={r.get('spawn_mesh_ring_ready')} "
        f"miss={r.get('mesh_missing_greedy')} dirty={r.get('dirty_n')} "
        f"block={r.get('ring_blocker')} top={r.get('top_dirty')} "
        f"miss_xy={r.get('missing_xy') or r.get('miss_xy')} "
        f"softd={r.get('softdefer_held_n')} infl={r.get('inflight_n')} "
        f"pend={r.get('pending_gpu_n')} fm={r.get('first_mesh_n')}"
    )
