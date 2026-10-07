#!/usr/bin/env python3
"""Summarize periodic emerge-census cost and single-frame census spikes.

``kind=period`` fields are frame averages. Dividing each cost average by the
average sample count estimates mean cost per census during that period; those
ratios are not percentiles of individual census calls. ``kind=spike`` rows are
single-frame measurements and can be read directly.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path
from typing import Any


BANDS = (
    ("near", -200, None),
    ("mid", -600, -200),
    ("far_east", -740, -600),
    ("far_west", None, -740),
)

SAMPLE_COUNT = "column_emerge_stage_sample_n"
SAMPLE_COST_FIELDS = {
    "total": "column_emerge_stage_sample_ms",
    "focus_jobs": "column_emerge_focus_jobs_ms",
    "shadow_census": "column_emerge_shadow_census_ms",
    "demand_breakdown": "column_emerge_demand_breakdown_ms",
    "demand_stop": "column_emerge_demand_stop_ms",
}


def numeric(row: dict[str, Any], key: str) -> float | None:
    value = row.get(key)
    if not isinstance(value, (int, float)) or not math.isfinite(float(value)):
        return None
    return float(value)


def percentile(values: list[float], quantile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    return float(ordered[int(round(quantile * (len(ordered) - 1)))])


def summarize(values: list[float]) -> dict[str, float | int | None]:
    if not values:
        return {"n": 0, "median": None, "p95": None, "max": None}
    return {
        "n": len(values),
        "median": float(statistics.median(values)),
        "p95": percentile(values, 0.95),
        "max": float(max(values)),
    }


def focus_band(row: dict[str, Any]) -> str | None:
    cx = numeric(row, "focus_cx")
    if cx is None:
        return None
    for name, lower_exclusive, upper_inclusive in BANDS:
        if (lower_exclusive is None or cx > lower_exclusive) and (
            upper_inclusive is None or cx <= upper_inclusive
        ):
            return name
    return None


def read_rows(path: Path) -> tuple[list[dict[str, Any]], int]:
    rows: list[dict[str, Any]] = []
    malformed = 0
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                malformed += 1
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
            if (cx := numeric(row, "focus_cx")) is not None
            and (lower_exclusive is None or cx > lower_exclusive)
            and (upper_inclusive is None or cx <= upper_inclusive)
        ]
        by_metric: dict[str, list[float]] = {
            metric: [] for metric in SAMPLE_COST_FIELDS
        }
        for row in selected:
            count = numeric(row, SAMPLE_COUNT) or 0.0
            if count <= 0.0:
                continue
            for metric, field in SAMPLE_COST_FIELDS.items():
                cost = numeric(row, field)
                if cost is not None:
                    by_metric[metric].append(cost / count)
        available_metrics = {
            metric: summarize(values)
            for metric, values in by_metric.items()
            if values
        }
        bands.append(
            {
                "focus_band": name,
                "period_count": len(selected),
                "period_mean_cost_per_census_ms": available_metrics,
            }
        )

    sampled_spikes = [
        row
        for row in spikes
        if (numeric(row, SAMPLE_COUNT) or 0.0) > 0.0
    ]
    sampled_spikes.sort(
        key=lambda row: numeric(row, SAMPLE_COST_FIELDS["total"]) or 0.0,
        reverse=True,
    )
    spike_fields = (
        "focus_cx",
        "player_x",
        "wall_ms",
        "world_streaming_phase_ms",
        "mesh_emerge_ms",
        SAMPLE_COST_FIELDS["total"],
        SAMPLE_COST_FIELDS["focus_jobs"],
        SAMPLE_COST_FIELDS["shadow_census"],
        SAMPLE_COST_FIELDS["demand_breakdown"],
        SAMPLE_COST_FIELDS["demand_stop"],
    )
    return {
        "input": str(path),
        "period_rows": len(periods),
        "spike_rows": len(spikes),
        "sampled_spike_rows": len(sampled_spikes),
        "malformed_rows": malformed,
        "period_semantics": (
            "Cost and sample count are per-frame period averages. Cost/count is "
            "the mean cost per census within the period, not an individual-call "
            "percentile. Spike rows are direct single-frame samples."
        ),
        "bands": bands,
        "largest_census_spikes": [
            {field: row.get(field) for field in spike_fields}
            for row in sampled_spikes[:20]
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    summary = analyze(args.perf_jsonl)
    output = json.dumps(summary, indent=2, ensure_ascii=False) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
