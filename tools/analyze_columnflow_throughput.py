#!/usr/bin/env python3
"""Summarize ColumnFlow queue pressure across perf-monitor periods.

ColumnFlow fields in ``kind=period`` rows are sampled snapshots (the generic
FramePerfMonitor period writer keeps the last value for fields without an
explicit accumulator). This tool therefore compares distributions; it does
not sum drain/defer/queue values across periods.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


METRICS = (
    "focus_cx",
    "focus_cz",
    "wall_ms",
    "world_streaming_phase_ms",
    "mesh_emerge_ms",
    "column_flow_drained_n",
    "column_flow_deferred_n",
    "column_flow_queue_live_n",
    "column_flow_queue_stale_heap_n",
    "column_flow_probed_n",
    "column_flow_cooldown_deferred_n",
    "column_flow_probe_budget_hit_n",
    "column_flow_post_deadline_unit_ms_max",
    "column_flow_drain_request_n",
    "column_flow_critical_units_at_entry_n",
    "column_flow_critical_units_at_exit_n",
    "column_flow_live_first_mesh_n",
    "column_flow_live_relight_n",
    "column_flow_live_seam_n",
    "column_flow_live_promote_n",
    "column_flow_probed_first_mesh_n",
    "column_flow_probed_relight_n",
    "column_flow_probed_seam_n",
    "column_flow_probed_promote_n",
    "column_flow_deferred_first_mesh_n",
    "column_flow_deferred_relight_n",
    "column_flow_deferred_seam_n",
    "column_flow_deferred_promote_n",
    "column_flow_dispatched_first_mesh_n",
    "column_flow_dispatched_relight_n",
    "column_flow_dispatched_seam_n",
    "column_flow_dispatched_promote_n",
    "column_flow_dispatch_first_mesh_ms_max",
    "column_flow_dispatch_relight_ms_max",
    "column_flow_dispatch_seam_ms_max",
    "column_flow_dispatch_promote_ms_max",
    "mesh_async",
    "column_loaded_no_mesh_n",
    "phase_abort_heavy",
    "focus_missing_mesh",
    "visual_holes",
    "stream_disk_complete_n",
    "stream_gen_commit_n",
)


def percentile(values: list[float], quantile: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(quantile * (len(ordered) - 1)))
    return ordered[index]


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {"period_n": len(rows)}
    if rows:
        result["focus_start"] = [rows[0].get("focus_cx"), rows[0].get("focus_cz")]
        result["focus_end"] = [rows[-1].get("focus_cx"), rows[-1].get("focus_cz")]
    for metric in METRICS:
        values = [
            float(row[metric])
            for row in rows
            if isinstance(row.get(metric), (int, float))
        ]
        if values:
            result[metric] = {
                "median": statistics.median(values),
                "p95": percentile(values, 0.95),
                "max": max(values),
            }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--window-periods", type=int, default=400)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    if args.window_periods <= 0:
        parser.error("--window-periods must be positive")

    periods: list[dict[str, Any]] = []
    with args.perf_jsonl.open("r", encoding="utf-8") as source:
        for line in source:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            if row.get("kind") == "period":
                periods.append(row)

    if not periods:
        parser.error(f"no kind=period rows found in {args.perf_jsonl}")

    windows = [
        summarize(periods[start : start + args.window_periods])
        for start in range(0, len(periods), args.window_periods)
    ]
    report = {
        "schema": "columnflow_throughput.v1",
        "source": str(args.perf_jsonl),
        "aggregation_note": (
            "period fields are snapshots unless FramePerfMonitor explicitly "
            "accumulates them; compare medians/p95/max, do not sum them"
        ),
        "window_periods": args.window_periods,
        "overall": summarize(periods),
        "windows": windows,
    }
    serialized = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(serialized, encoding="utf-8")
    print(serialized, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
