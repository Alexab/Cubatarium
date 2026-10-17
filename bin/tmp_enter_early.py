import json
from pathlib import Path
rows = [json.loads(l) for l in Path("bin/logs/enter_lit_20260827-211257.jsonl").open() if l.strip()]
for r in rows[:25]:
    print(
        f"t={r['elapsed_ms']:.0f} miss={r['mesh_missing_greedy']} dn={r['dirty_n']} "
        f"block={r['ring_blocker']} ready={r['spawn_mesh_ring_ready']} uf={r['underfeet_present_ready']} "
        f"gate={r['gate_done_n']} stuck=({r.get('stuck_dirty_cx')},{r.get('stuck_dirty_cy')},{r.get('stuck_dirty_cz')}) "
        f"top=({r['top_dirty_cx']},{r['top_dirty_cz']}) infl={r['inflight']} pend={r['pending_global']}"
    )
