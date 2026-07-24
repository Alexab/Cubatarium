#!/usr/bin/env python3
"""Timeline slices for perf JSONL (start / mid / end + standing periods)."""

import json
import statistics
import sys
from pathlib import Path


def med(vals):
    return round(statistics.median(vals), 2) if vals else 0


def p95(vals):
    if not vals:
        return 0
    s = sorted(vals)
    return s[max(0, int(0.95 * len(s)) - 1)]


def summarize(rows, label):
    if not rows:
        return
    keys = [
        "wall_ms",
        "dirty",
        "mesh_async",
        "gen_backlog_total",
        "gen_q",
        "commit_seal_ms",
        "mesh_sync_ms",
        "phys_ms",
        "prefetch_keep_ops",
    ]
    print(f"  {label} (n={len(rows)})")
    for key in keys:
        vals = [r[key] for r in rows if key in r]
        if not vals:
            continue
        print(f"    {key}: med={med(vals)} p95={p95(vals)} max={max(vals)}")


def main():
    path = Path(sys.argv[1])
    rows = []
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    print(f"== {path.name} total={len(rows)}")
    n = len(rows)
    thirds = [rows[: n // 3], rows[n // 3 : 2 * n // 3], rows[2 * n // 3 :]]
    for label, chunk in zip(["start", "middle", "end"], thirds):
        summarize(chunk, label)
    periods = [r for r in rows if r.get("kind") == "period"]
    if len(periods) >= 8:
        summarize(periods[:4], "first_periods")
        summarize(periods[-4:], "last_periods")


if __name__ == "__main__":
    main()
