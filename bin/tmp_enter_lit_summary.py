import json
from pathlib import Path

p = Path(r"e:\Work\Home\Cubatarium\bin\logs\enter_lit_20260813-222820.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8")]
print("n", len(rows))
print("keys sample", sorted(rows[-1].keys()))
keys = [
    "elapsed_ms",
    "gate_done",
    "gate_remaining",
    "relight_completed",
    "visibility_debt",
    "snapshot_debt",
    "fifo_n",
    "mesh_gpu_pending_near",
    "dark_face_stale_near_n",
    "dark_face_n",
    "ring_not_ready",
    "mesh_visual_warmup",
    "underfeet_need",
]
idxs = [0, 1, len(rows) // 4, len(rows) // 2, max(0, len(rows) - 5), len(rows) - 1]
for i in idxs:
    r = rows[i]
    print(i, {k: r.get(k) for k in keys})

# progress of gate_done / remaining / stale over time
print("--- timeline ---")
for r in rows[:: max(1, len(rows) // 20)]:
    print(
        int(r.get("elapsed_ms", 0)),
        "done",
        r.get("gate_done"),
        "rem",
        r.get("gate_remaining"),
        "relit",
        r.get("relight_completed"),
        "stale",
        r.get("dark_face_stale_near_n"),
        "gpu",
        r.get("mesh_gpu_pending_near"),
        "vis",
        r.get("visibility_debt"),
        "snap",
        r.get("snapshot_debt"),
    )
