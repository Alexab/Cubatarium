#!/usr/bin/env python3
"""Summarize async chunk-I/O phases in a flight-sim perf JSONL file.

The legacy wall/streaming fields in ``kind=period`` rows are interval means.
The newly added async-I/O subphase fields currently describe the final frame
sampled for that period; keep that distinction visible in the output.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


PERIOD_MEAN_FIELDS = (
    "wall_ms",
    "world_streaming_phase_ms",
    "async_chunk_post_scheduler_ms",
    "async_chunk_io_drain_ms",
)

PERIOD_END_FIELDS = (
    "async_chunk_io_discard_cancelled_ms",
    "async_chunk_io_result_selection_ms",
    "async_chunk_io_result_processing_ms",
    "async_chunk_io_world_apply_ms",
    "async_chunk_io_column_finalize_ms",
    "async_chunk_io_result_requeue_ms",
    "async_chunk_io_save_drain_ms",
    "async_chunk_io_light_flags_save_ms",
    "async_chunk_io_ready_loads_before_n",
    "async_chunk_io_selected_loads_n",
    "async_chunk_io_processed_loads_n",
    "async_chunk_io_requeued_loads_n",
)

BANDS = (
    ("near", -200, None),
    ("mid", -600, -200),
    ("far_east", -740, -600),
    ("far_west", None, -740),
)


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = int(round(quantile * (len(ordered) - 1)))
    return float(ordered[index])


def summarize(values: list[float]) -> dict[str, float | int | None]:
    if not values:
        return {"n": 0, "median": None, "p95": None, "max": None}
    return {
        "n": len(values),
        "median": float(statistics.median(values)),
        "p95": percentile(values, 0.95),
        "max": float(max(values)),
    }


def numeric_values(rows: list[dict[str, Any]], key: str) -> list[float]:
    return [
        float(row[key])
        for row in rows
        if isinstance(row.get(key), (int, float))
        and math.isfinite(float(row[key]))
    ]


def read_rows(path: Path) -> tuple[list[dict[str, Any]], int]:
    rows: list[dict[str, Any]] = []
    malformed = 0
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                malformed += 1  # The live writer may be partway through a row.
                continue
            if isinstance(row, dict):
                rows.append(row)
    return rows, malformed


def analyze(path: Path) -> dict[str, Any]:
    rows, malformed = read_rows(path)
    periods = [row for row in rows if row.get("kind") == "period"]
    spikes = [row for row in rows if row.get("kind") == "spike"]
    bands: list[dict[str, Any]] = []

    for name, lower_exclusive, upper_inclusive in BANDS:
        selected = [
            row
            for row in periods
            if isinstance(row.get("focus_cx"), (int, float))
            and (lower_exclusive is None or row["focus_cx"] > lower_exclusive)
            and (upper_inclusive is None or row["focus_cx"] <= upper_inclusive)
        ]
        if not selected:
            continue

        mean_stats = {
            key: summarize(numeric_values(selected, key))
            for key in PERIOD_MEAN_FIELDS
        }
        point_stats = {
            key: summarize(numeric_values(selected, key))
            for key in PERIOD_END_FIELDS
        }
        point_stats["async_chunk_io_apply_time_budget_hit_n"] = sum(
            bool(row.get("async_chunk_io_apply_time_budget_hit"))
            for row in selected
        )
        point_stats["async_chunk_io_apply_time_budget_hit_samples"] = len(
            selected
        )
        bands.append(
            {
                "focus_band": name,
                "period_count": len(selected),
                "focus_cx_start": selected[0].get("focus_cx"),
                "focus_cx_end": selected[-1].get("focus_cx"),
                "period_mean_metrics": mean_stats,
                "period_end_frame_samples": point_stats,
            }
        )

    expensive_selection = sorted(
        (
            row
            for row in periods
            if isinstance(row.get("async_chunk_io_result_selection_ms"), (int, float))
        ),
        key=lambda row: float(row["async_chunk_io_result_selection_ms"]),
        reverse=True,
    )[:12]
    expensive_light_save = sorted(
        (
            row
            for row in periods
            if isinstance(row.get("async_chunk_io_light_flags_save_ms"), (int, float))
        ),
        key=lambda row: float(row["async_chunk_io_light_flags_save_ms"]),
        reverse=True,
    )[:12]
    largest_spikes = sorted(
        (row for row in spikes if isinstance(row.get("wall_ms"), (int, float))),
        key=lambda row: float(row["wall_ms"]),
        reverse=True,
    )[:12]

    def observations(source: list[dict[str, Any]], field: str) -> list[dict[str, Any]]:
        keys = (
            field,
            "focus_cx",
            "async_chunk_io_ready_loads_before_n",
            "async_chunk_io_selected_loads_n",
            "async_chunk_io_processed_loads_n",
            "async_chunk_io_requeued_loads_n",
            "async_chunk_io_apply_time_budget_hit",
            "async_chunk_io_drain_ms",
            "async_chunk_io_result_selection_ms",
            "async_chunk_io_light_flags_save_ms",
            "world_streaming_phase_ms",
            "wall_ms",
        )
        return [{key: row.get(key) for key in keys} for row in source]

    return {
        "input": str(path),
        "period_rows": len(periods),
        "spike_rows": len(spikes),
        "malformed_or_incomplete_rows": malformed,
        "phase_sample_semantics": {
            "period_mean_fields": list(PERIOD_MEAN_FIELDS),
            "period_end_frame_fields": list(PERIOD_END_FIELDS)
            + ["async_chunk_io_apply_time_budget_hit"],
            "nested_metrics": [
                "async_chunk_io_world_apply_ms is nested in result_processing_ms",
                "async_chunk_io_column_finalize_ms is nested in result_processing_ms",
            ],
        },
        "bands": bands,
        "highest_period_end_selection_samples": observations(
            expensive_selection, "async_chunk_io_result_selection_ms"
        ),
        "highest_period_end_light_save_samples": observations(
            expensive_light_save, "async_chunk_io_light_flags_save_ms"
        ),
        "highest_wall_spike_samples": observations(largest_spikes, "wall_ms"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--out", type=Path, help="optional JSON summary path")
    args = parser.parse_args()
    if not args.perf_jsonl.is_file():
        parser.error(f"perf JSONL file does not exist: {args.perf_jsonl}")

    summary = analyze(args.perf_jsonl)
    rendered = json.dumps(summary, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
