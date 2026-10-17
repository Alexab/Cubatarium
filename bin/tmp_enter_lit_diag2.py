import json
from pathlib import Path
from collections import Counter

p = Path("bin/logs/enter_lit_20260826-225948.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
print("n", len(rows))
# transitions of key fields
prev = None
for r in rows:
    key = (
        r.get("spawn_mesh_ring_ready"),
        r.get("mesh_missing_greedy"),
        r.get("dirty_n"),
        r.get("ring_blocker"),
        r.get("mesh_async_pending"),
        r.get("inflight"),
        r.get("pending_gpu_global"),
        r.get("stuck_dirty_cx"),
        r.get("stuck_dirty_cy"),
        r.get("stuck_dirty_cz"),
        r.get("stuck_has_chunk"),
        r.get("stuck_has_drawable"),
        r.get("top_dirty_cx"),
        r.get("top_dirty_cz"),
        r.get("enter_terminal_held_n"),
        r.get("stage_skip_remesh_pending_light"),
        r.get("underfeet_present_ready"),
        r.get("visibility_debt"),
    )
    if key != prev:
        print(
            f"t={r.get('elapsed_ms'):.0f} ready={key[0]} miss={key[1]} dn={key[2]} "
            f"block={key[3]} async={key[4]} infl={key[5]} gpu={key[6]} "
            f"stuck=({key[7]},{key[8]},{key[9]}) chunk={key[10]} draw={key[11]} "
            f"top=({key[12]},{key[13]}) term={key[14]} skipPL={key[15]} "
            f"uf={key[16]} debt={key[17]}"
        )
        prev = key

print("--- blockers ---")
print(Counter(r.get("ring_blocker") for r in rows))
print("--- last ---")
print({k: rows[-1].get(k) for k in sorted(rows[-1]) if k not in ()})
