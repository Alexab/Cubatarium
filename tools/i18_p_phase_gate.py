#!/usr/bin/env python3
"""Summarize replay-manual autofly report for P0-P5 iteration."""
import json
import sys
from pathlib import Path

KEYS = [
    "wall_ms_fly_med", "wall_ms_med", "stop_wall_med",
    "stream_ms", "mesh_emerge_ms", "prep_refresh_gap_ms",
    "holes_rate", "chain_stall_sec", "fm_dirty_to_gpu_finish_med",
    "cruise_capture_retarget_blocked_ratio", "schedule_ok_zero_rate",
    "emerge_spike_frac", "pool_unsync_uploads", "opaque_idle_churn_max",
]


def main():
    path = Path(sys.argv[1])
    label = sys.argv[2] if len(sys.argv) > 2 else path.stem
    d = json.loads(path.read_text(encoding="utf-8"))
    m = d.get("metrics", {})
    print(f"\n=== {label} ===")
    print(f"  log: {d.get('perf_jsonl')}")
    print(f"  periods: {d.get('periods')} pass: {d.get('pass')}")
    for k in KEYS:
        v = m.get(k)
        if v is not None:
            print(f"  {k:42s} {v}")


if __name__ == "__main__":
    main()
