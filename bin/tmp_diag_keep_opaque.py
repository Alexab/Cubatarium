"""keep_cols vs opaque correlation on 091745."""
import json
from pathlib import Path
from collections import defaultdict

rows = []
for line in Path("bin/logs/perf_20260825-091745_23048.jsonl").read_text(
    encoding="utf-8", errors="replace"
).splitlines():
    if line.startswith("{"):
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)

by_keep = defaultdict(list)
for r in rows[40:]:
    by_keep[int(r.get("keep_cols") or 0)].append(
        int(r.get("opaque_refs_cpu_vis") or 0)
    )
for k, xs in sorted(by_keep.items()):
    xs = sorted(xs)
    print(f"keep={k} n={len(xs)} opaque med={xs[len(xs)//2]} min={xs[0]} max={xs[-1]}")

print("\nkeep transitions on opaque drop>=80:")
for i in range(1, len(rows)):
    dc = int(rows[i].get("opaque_refs_cpu_vis") or 0) - int(
        rows[i - 1].get("opaque_refs_cpu_vis") or 0
    )
    if dc > -80:
        continue
    print(
        f"{i-1}->{i} keep {rows[i-1].get('keep_cols')}->{rows[i].get('keep_cols')} "
        f"opaque {rows[i-1].get('opaque_refs_cpu_vis')}->{rows[i].get('opaque_refs_cpu_vis')}"
    )
