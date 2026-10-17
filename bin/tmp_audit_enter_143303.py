#!/usr/bin/env python3
import json
from collections import Counter
from pathlib import Path

p = Path(__file__).resolve().parents[1] / "bin" / "logs" / "enter_lit_20260826-143303.jsonl"
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip().startswith("{")]
print("n", len(rows))
last = rows[-1]
print("last keys with values of interest:")
for k in sorted(last.keys()):
    if any(
        x in k.lower()
        for x in (
            "ring",
            "miss",
            "dirty",
            "debt",
            "ready",
            "block",
            "prog",
            "stuck",
            "top",
            "fifo",
            "gpu",
            "soft",
            "snap",
            "gate",
            "elapsed",
            "focus",
            "void",
            "unlit",
            "pending",
        )
    ):
        print(f"  {k}={last.get(k)}")

print("\n--- timeline ---")
step = max(1, len(rows) // 20)
for r in rows[::step] + ([rows[-1]] if rows else []):
    print(
        f"t={r.get('elapsed_ms')} ring={r.get('ring_not_ready')} "
        f"miss={r.get('mesh_missing')} dirty={r.get('dirty_n')} "
        f"debt={r.get('visibility_debt')} snap={r.get('snapshot_debt')} "
        f"ready={r.get('spawn_mesh_ring_ready')} blocker={r.get('ring_blocker')} "
        f"stuck={r.get('stuck_dirty')} top={r.get('top_dirty')} "
        f"gpu={r.get('mesh_gpu_pending_near')} fifo={r.get('fifo_n')}"
    )

print("\nring Counter", Counter(r.get("ring_not_ready") for r in rows).most_common(8))
print("blocker", Counter(r.get("ring_blocker") for r in rows).most_common())
print("ready", Counter(r.get("spawn_mesh_ring_ready") for r in rows).most_common())
print("miss", Counter(r.get("mesh_missing") for r in rows).most_common())
print("dirty", Counter(r.get("dirty_n") for r in rows).most_common(8))

# Compare vs a successful enter if present
for other in [
    "enter_lit_20260826-112459.jsonl",
    "enter_lit_20260826-104819.jsonl",
    "enter_lit_20260826-134605.jsonl",
]:
    op = p.parent / other
    if not op.exists():
        continue
    orows = [json.loads(l) for l in op.open(encoding="utf-8") if l.strip().startswith("{")]
    if not orows:
        continue
    o = orows[-1]
    print(
        f"\ncompare {other}: n={len(orows)} last_t={o.get('elapsed_ms')} "
        f"ready={o.get('spawn_mesh_ring_ready')} ring={o.get('ring_not_ready')} "
        f"miss={o.get('mesh_missing')} dirty={o.get('dirty_n')} blocker={o.get('ring_blocker')}"
    )
