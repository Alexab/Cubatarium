#!/usr/bin/env python3
"""FM completion chain audit: classify fly spikes by mesh pipeline stage."""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path


def classify_stage(r: dict) -> str:
    dirty_fm = float(r.get("dirty_fm_n") or 0)
    schedule_ok = float(r.get("mesh_dirty_schedule_ok_n") or 0)
    watch_n = float(r.get("fm_dirty_gpu_watch_n") or 0)
    gpu_finish = float(r.get("fm_dirty_to_gpu_finish_n") or 0)
    gpu_not_ready = float(r.get("gpu_finish_not_ready_n") or 0)
    pending_gpu = float(r.get("pending_gpu_n") or 0)
    pending_capture = float(r.get("mesh_pending_capture_n") or 0)
    mesh_async = float(r.get("mesh_async_inflight_n") or 0)
    mesh_apply_stale = float(r.get("mesh_apply_stale_n") or 0)

    if dirty_fm <= 0:
        return "empty_fm"
    if schedule_ok <= 0 and pending_capture > 0:
        return "capture_pending"
    if schedule_ok <= 0:
        return "schedule_starved"
    if watch_n > 0 and gpu_finish <= 0 and mesh_async > 0:
        return "async_inflight"
    if gpu_not_ready > 0:
        return "gpu_not_ready"
    if pending_gpu > 0 and gpu_finish <= 0:
        return "gpu_budget"
    if mesh_apply_stale > 0:
        return "dark_reject"
    if gpu_finish > 0:
        return "complete"
    return "async_inflight"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jsonl", type=Path, help="flight sim jsonl log")
    ap.add_argument("--y-min", type=float, default=10.0)
    args = ap.parse_args()
    if not args.jsonl.is_file():
        print(f"FAIL: missing {args.jsonl}", file=sys.stderr)
        return 1

    spikes: list[dict] = []
    for line in args.jsonl.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if float(row.get("player_y") or 0) <= args.y_min:
            continue
        spikes.append(row)

    bucket: Counter[str] = Counter()
    for r in spikes:
        bucket[classify_stage(r)] += 1

    total = sum(bucket.values())
    print(f"spikes={total}")
    for stage, count in bucket.most_common():
        pct = 100.0 * count / total if total else 0.0
        print(f"  {stage}: {count} ({pct:.1f}%)")
    if bucket:
        dominant = bucket.most_common(1)[0][0]
        print(f"dominant_stall_stage={dominant}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
