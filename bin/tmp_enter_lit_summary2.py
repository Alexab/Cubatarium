import json
from pathlib import Path

p = Path(r"e:\Work\Home\Cubatarium\bin\logs\enter_lit_20260813-222820.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8")]
keys = [
    "elapsed_ms",
    "gate_done_n",
    "visibility_debt",
    "snapshot_debt",
    "relight_completed_n",
    "fifo_n",
    "mesh_gpu_pending_near",
    "pending_gpu_global",
    "dirty_n",
    "mesh_dirty",
    "inflight",
    "remesh_after_apply_n",
    "mark_relit_raa_total",
    "raa_commit_mark_dirty_n",
    "enter_lit_quiesce",
    "enter_terminal_held_n",
    "dark_face_near_n",
    "dark_face_void_near_n",
    "ring_blocker",
    "stuck_dirty_cx",
    "stuck_dirty_cy",
    "stuck_has_drawable",
]
for r in rows[:: max(1, len(rows) // 15)]:
    print({k: r.get(k) for k in keys})
print("LAST", {k: rows[-1].get(k) for k in keys})
