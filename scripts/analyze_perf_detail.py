#!/usr/bin/env python3
import json
import statistics
from pathlib import Path

path = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260717-164641_3424.jsonl")
rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]
print("total", len(rows))

# First 30 samples after load
print("\n=== first 20 samples ===")
for i, r in enumerate(rows[:20]):
    print(
        f"{i:02d} kind={r.get('kind')} wall={r.get('wall_ms',0):.0f} "
        f"phys={r.get('phys_ms',0):.0f} dirty={r.get('dirty')} "
        f"mesh_async={r.get('mesh_async')} gen_bl={r.get('gen_backlog_total')} "
        f"gen_q={r.get('gen_q')} seal={r.get('commit_seal_ms',0):.1f} "
        f"sync={r.get('mesh_sync_ms',0):.2f} snap={r.get('mesh_snapshot_ms',0):.1f} "
        f"gui={r.get('gui_overlay_ms',0):.1f} residual={r.get('residual_ms',0):.1f}"
    )

print("\n=== last 20 samples ===")
for i, r in enumerate(rows[-20:]):
    print(
        f"{i:02d} kind={r.get('kind')} wall={r.get('wall_ms',0):.0f} "
        f"phys={r.get('phys_ms',0):.0f} dirty={r.get('dirty')} "
        f"mesh_async={r.get('mesh_async')} gen_bl={r.get('gen_backlog_total')} "
        f"gen_q={r.get('gen_q')} seal={r.get('commit_seal_ms',0):.1f} "
        f"sync={r.get('mesh_sync_ms',0):.2f} snap={r.get('mesh_snapshot_ms',0):.1f}"
    )

# Count frames by wall band
bands = [(0, 50), (50, 100), (100, 200), (200, 350), (350, 10000)]
for lo, hi in bands:
    chunk = [r for r in rows if lo <= r.get("wall_ms", 0) < hi]
    if not chunk:
        continue
    print(
        f"wall[{lo},{hi}): n={len(chunk)} "
        f"phys_med={statistics.median([r['phys_ms'] for r in chunk]):.0f} "
        f"dirty_med={statistics.median([r['dirty'] for r in chunk]):.0f} "
        f"gen_bl_med={statistics.median([r.get('gen_backlog_total',0) for r in chunk]):.0f}"
    )
