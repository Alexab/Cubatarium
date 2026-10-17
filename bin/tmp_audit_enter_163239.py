#!/usr/bin/env python3
"""Audit enter_lit stuck for 163239 vs prior."""
import json
from collections import Counter
from pathlib import Path

LOGS = Path(__file__).resolve().parents[1] / "bin" / "logs"


def load(name):
    p = LOGS / name
    return [json.loads(l) for l in p.open(encoding="utf-8") if l.strip().startswith("{")]


def summarize(name):
    rows = load(name)
    if not rows:
        print(name, "EMPTY")
        return
    last = rows[-1]
    print(f"\n=== {name} n={len(rows)} last_t={last.get('elapsed_ms')} ===")
    keys = [
        "ring_not_ready",
        "ring_blocker",
        "spawn_mesh_ring_ready",
        "mesh_missing_greedy",
        "mesh_dirty",
        "dirty_n",
        "visibility_debt",
        "snapshot_debt",
        "fifo_n",
        "mesh_gpu_pending_near",
        "pending_gpu_global",
        "enter_phantom_dirty_pruned_n",
        "stuck_dirty_cx",
        "stuck_dirty_cy",
        "stuck_dirty_cz",
        "stuck_has_chunk",
        "stuck_has_drawable",
        "top_dirty_cx",
        "top_dirty_cz",
        "gate_done_n",
        "underfeet_present_ready",
        "raa_commit_mark_dirty_n",
        "gpu_kick_n",
        "gpu_finish_n",
    ]
    for k in keys:
        if k in last:
            print(f"  {k}={last.get(k)}")
    print("ring", Counter(r.get("ring_not_ready") for r in rows).most_common(5))
    print("blocker", Counter(r.get("ring_blocker") for r in rows).most_common())
    print("ready", Counter(r.get("spawn_mesh_ring_ready") for r in rows).most_common())
    print("dirty", Counter(r.get("dirty_n") for r in rows).most_common(6))
    # early vs late
    for lab, r in [("first", rows[0]), ("mid", rows[len(rows) // 2]), ("last", rows[-1])]:
        print(
            f"  {lab}: t={r.get('elapsed_ms')} ring={r.get('ring_not_ready')} "
            f"dirty={r.get('dirty_n')} miss={r.get('mesh_missing_greedy')} "
            f"blocker={r.get('ring_blocker')} debt={r.get('visibility_debt')} "
            f"phantom={r.get('enter_phantom_dirty_pruned_n')} "
            f"stuck=({r.get('stuck_dirty_cx')},{r.get('stuck_dirty_cy')},{r.get('stuck_dirty_cz')}) "
            f"has_chunk={r.get('stuck_has_chunk')}"
        )


for n in [
    "enter_lit_20260826-163239.jsonl",
    "enter_lit_20260826-143303.jsonl",
    "enter_lit_20260826-112459.jsonl",
]:
    summarize(n)
