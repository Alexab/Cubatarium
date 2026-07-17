#!/usr/bin/env python3
"""Compare perf JSONL sessions for P1/P3 streaming rework."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path


METRICS = [
    "dirty",
    "gen_q",
    "gen_backlog_total",
    "mesh_async",
    "commit_seal_ms",
    "mesh_sync_ms",
    "wall_ms",
    "prefetch_visual_ops",
    "prefetch_keep_ops",
    "idle_prefetch_ms",
]


def load_rows(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return rows


def summarize(rows: list[dict], key: str) -> dict | None:
    vals = [row[key] for row in rows if key in row]
    if not vals:
        return None
    vals_sorted = sorted(vals)
    p95_index = max(0, int(0.95 * len(vals_sorted)) - 1)
    return {
        "count": len(vals),
        "med": round(statistics.median(vals), 2),
        "p95": vals_sorted[p95_index],
        "max": max(vals),
    }


def print_report(label: str, rows: list[dict]) -> None:
    print(f"\n== {label} (samples={len(rows)})")
    spikes = sum(1 for row in rows if row.get("kind") == "spike")
    print(f"  spikes: {spikes}")
    for key in METRICS:
        stats = summarize(rows, key)
        if stats is None:
            continue
        print(
            f"  {key}: med={stats['med']} p95={stats['p95']} max={stats['max']}"
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "logs",
        nargs="+",
        type=Path,
        help="perf_*.jsonl files (baseline first, then current)",
    )
    args = parser.parse_args()
    for index, path in enumerate(args.logs):
        tag = "baseline" if index == 0 else f"current-{index}"
        print_report(f"{tag}: {path.name}", load_rows(path))


if __name__ == "__main__":
    main()
