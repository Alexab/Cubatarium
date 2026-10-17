import json
from pathlib import Path

p = Path(r"e:\Work\Home\Cubatarium\bin\logs\enter_lit_20260813-232312.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8")]
keys = [
    "elapsed_ms",
    "snapshot_debt",
    "visibility_debt",
    "fifo_n",
    "mesh_visual_warmup",
    "ring_not_ready",
    "pending_global",
    "enter_lit_quiesce",
    "kind",
    "stage",
]
for r in rows[:: max(1, len(rows) // 8)]:
    print({k: r.get(k) for k in keys})
print("kinds", sorted({r.get("kind") for r in rows}))
print("LAST", {k: rows[-1].get(k) for k in keys})
