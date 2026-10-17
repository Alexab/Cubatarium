import json
from pathlib import Path

p = Path(r"e:\Work\Home\Cubatarium\bin\logs\enter_lit_20260813-222820.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8")]
keys = [
    "elapsed_ms",
    "gate_done_n",
    "visibility_debt",
    "snapshot_debt",
    "pending_global",
    "fifo_n",
    "dirty_n",
    "mesh_gpu_pending_near",
    "dark_face_near_n",
    "dark_face_void_near_n",
    "enter_terminal_held_n",
    "suppress_relight_seam",
    "mesh_visual_warmup",
    "ring_not_ready",
    "spawn_mesh_ring_ready",
]
# print if spawn exists
print("has spawn", "spawn_mesh_ring_ready" in rows[-1])
for r in rows[:: max(1, len(rows) // 12)]:
    print({k: r.get(k) for k in keys})
