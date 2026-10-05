#!/usr/bin/env python3
"""Compare disk/procedural world-column source events in chunk-X bins."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


EVENT_RE = re.compile(
    r"\[WorldColumnSource\].*?\bsource=(disk|procedural)\s+"
    r"outcome=(\w+)\s+coord=\((-?\d+),(-?\d+),(-?\d+)\)"
)
SAVE_EVENT_RE = re.compile(
    r"\[WorldColumnSave\].*?outcome=(\w+)"
    r"(?:\s+coord=\((-?\d+),(-?\d+),(-?\d+)\))?"
)
FIELD_RE = re.compile(r"\b([a-z][a-z0-9_]*)=([-+]?(?:\d+\.?\d*|\.\d+))")
TIMING_FIELDS = (
    "elapsed_ms",
    "result_wait_ms",
    "result_wait_max_ms",
    "file_read_ms",
    "deserialize_ms",
    "deserialize_max_ms",
    "apply_ms",
    "apply_max_ms",
    "deserialize_apply_ms",
    "deserialize_apply_max_ms",
    "queue_ms",
    "scheduler_queue_ms",
    "worker_pool_queue_ms",
    "generation_ms",
    "ready_wait_ms",
    "apply_ms",
    "total_ms",
)


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(len(ordered) * fraction) - 1)
    return ordered[index]


def summarize_events(events: list[dict[str, Any]]) -> dict[str, Any]:
    counts = Counter(f"{row['source']}/{row['outcome']}" for row in events)
    columns: dict[str, set[tuple[int, int]]] = defaultdict(set)
    timings: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    for row in events:
        category = f"{row['source']}/{row['outcome']}"
        columns[category].add((row["cx"], row["cz"]))
        for field in TIMING_FIELDS:
            if field in row:
                timings[category][field].append(float(row[field]))

    rendered_timings: dict[str, dict[str, dict[str, float | int | None]]] = {}
    for category, fields in timings.items():
        rendered_timings[category] = {}
        for field, values in fields.items():
            rendered_timings[category][field] = {
                "count": len(values),
                "median": round(statistics.median(values), 4),
                "p95": round(percentile(values, 0.95) or 0.0, 4),
                "max": round(max(values), 4),
            }

    return {
        "event_counts": dict(sorted(counts.items())),
        "unique_columns_by_source_outcome": {
            key: len(value) for key, value in sorted(columns.items())
        },
        "timings_ms": rendered_timings,
    }


def summarize_save_events(events: list[dict[str, Any]]) -> dict[str, Any]:
    counts = Counter(row["outcome"] for row in events)
    by_column: dict[tuple[int, int], list[dict[str, Any]]] = defaultdict(list)
    world_folders: set[str] = set()
    for row in events:
        if row.get("cx") is None:
            continue
        by_column[(row["cx"], row["cz"])].append(row)
        if row.get("world_folder"):
            world_folders.add(row["world_folder"])

    columns = []
    for (cx, cz), column_events in sorted(by_column.items()):
        outcome_counts = Counter(row["outcome"] for row in column_events)
        queued_slices = [
            int(row["fields"]["slices"])
            for row in column_events
            if row["outcome"] == "queued" and "slices" in row["fields"]
        ]
        columns.append(
            {
                "chunk_xz": [cx, cz],
                "event_counts": dict(sorted(outcome_counts.items())),
                "expected_slices": max(queued_slices, default=None),
                "written_cy": sorted(
                    {
                        row["cy"]
                        for row in column_events
                        if row["outcome"] == "slice_written"
                    }
                ),
                "failed_cy": sorted(
                    {
                        row["cy"]
                        for row in column_events
                        if row["outcome"] == "slice_failed"
                    }
                ),
            }
        )

    return {
        "event_counts": dict(sorted(counts.items())),
        "unique_columns_with_save_events": len(by_column),
        "world_folders": sorted(world_folders),
        "columns": columns,
    }


def parse_log(
    path: Path, min_chunk_x: int, max_chunk_x: int
) -> tuple[list[dict[str, Any]], int, list[dict[str, Any]], int, list[dict[str, Any]]]:
    source_events: list[dict[str, Any]] = []
    save_events: list[dict[str, Any]] = []
    shutdown_state_events: list[dict[str, Any]] = []
    malformed_source_lines = 0
    malformed_save_lines = 0
    with path.open("r", encoding="utf-8", errors="replace") as source:
        for line in source:
            if "[WorldColumnSource]" in line:
                match = EVENT_RE.search(line)
                if not match:
                    malformed_source_lines += 1
                    continue
                kind, outcome, cx, cy, cz = match.groups()
                cx_value = int(cx)
                if not min_chunk_x <= cx_value < max_chunk_x:
                    continue
                row: dict[str, Any] = {
                    "source": kind,
                    "outcome": outcome,
                    "cx": cx_value,
                    "cy": int(cy),
                    "cz": int(cz),
                }
                row.update(
                    (key, float(value)) for key, value in FIELD_RE.findall(line)
                )
                source_events.append(row)
            elif "[WorldColumnSave]" in line:
                match = SAVE_EVENT_RE.search(line)
                if not match:
                    malformed_save_lines += 1
                    continue
                outcome, cx, cy, cz = match.groups()
                fields = {
                    key: float(value) for key, value in FIELD_RE.findall(line)
                }
                row = {"outcome": outcome, "fields": fields}
                if cx is not None:
                    cx_value = int(cx)
                    if not min_chunk_x <= cx_value < max_chunk_x:
                        continue
                    row.update({"cx": cx_value, "cy": int(cy), "cz": int(cz)})
                else:
                    shutdown_state_events.append(row)
                folder = re.search(
                    r"\bworld_folder=(.+?)(?:\s+[a-z][a-z0-9_]*=|$)", line
                )
                if folder and cx is not None:
                    row["world_folder"] = folder.group(1).strip()
                save_events.append(row)
    return (
        source_events,
        malformed_source_lines,
        save_events,
        malformed_save_lines,
        shutdown_state_events,
    )


def parse_run_spec(value: str) -> tuple[str, Path]:
    if "=" not in value:
        raise argparse.ArgumentTypeError("run must be LABEL=INFO_LOG_PATH")
    label, raw_path = value.split("=", 1)
    if not label or not raw_path:
        raise argparse.ArgumentTypeError("run must be LABEL=INFO_LOG_PATH")
    return label, Path(raw_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--run",
        action="append",
        required=True,
        type=parse_run_spec,
        metavar="LABEL=INFO_LOG_PATH",
        help="repeat to include multiple INFO logs; reuse a label to combine log rotation",
    )
    parser.add_argument("--min-chunk-x", type=int, default=-512)
    parser.add_argument("--max-chunk-x", type=int, default=16)
    parser.add_argument("--bin-chunks", type=int, default=16)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    if args.bin_chunks <= 0 or args.max_chunk_x <= args.min_chunk_x:
        parser.error("require positive --bin-chunks and --max-chunk-x > --min-chunk-x")

    runs: dict[str, dict[str, Any]] = {}
    for label, path in args.run:
        if not path.is_file():
            parser.error(f"INFO log does not exist: {path}")
        report = runs.setdefault(
            label,
            {
                "info_logs": [],
                "events": [],
                "save_events": [],
                "shutdown_state_events": [],
                "malformed_source_lines": 0,
                "malformed_save_lines": 0,
                "source_bins": defaultdict(list),
                "save_bins": defaultdict(list),
            },
        )
        report["info_logs"].append(str(path))
        (
            events,
            malformed_source,
            save_events,
            malformed_save,
            shutdown_state_events,
        ) = parse_log(path, args.min_chunk_x, args.max_chunk_x)
        report["events"].extend(events)
        report["save_events"].extend(save_events)
        report["shutdown_state_events"].extend(shutdown_state_events)
        report["malformed_source_lines"] += malformed_source
        report["malformed_save_lines"] += malformed_save
        for row in events:
            bin_index = (row["cx"] - args.min_chunk_x) // args.bin_chunks
            report["source_bins"][bin_index].append(row)
        for row in save_events:
            if row.get("cx") is not None:
                bin_index = (row["cx"] - args.min_chunk_x) // args.bin_chunks
                report["save_bins"][bin_index].append(row)

    rendered_runs: dict[str, Any] = {}
    for label, run in runs.items():
        events = run["events"]
        bins = []
        bin_indices = sorted(set(run["source_bins"]) | set(run["save_bins"]))
        for bin_index in bin_indices:
            lo = args.min_chunk_x + bin_index * args.bin_chunks
            hi = min(args.max_chunk_x, lo + args.bin_chunks)
            bin_events = run["source_bins"].get(bin_index, [])
            bin_save_events = run["save_bins"].get(bin_index, [])
            bins.append(
                {
                    "chunk_x": [lo, hi],
                    **summarize_events(bin_events),
                    "save": summarize_save_events(bin_save_events),
                }
            )
        rendered_runs[label] = {
            "info_logs": run["info_logs"],
            "matched_chunk_x": [args.min_chunk_x, args.max_chunk_x],
            "bin_chunks": args.bin_chunks,
            "malformed_source_lines": run["malformed_source_lines"],
            "malformed_save_lines": run["malformed_save_lines"],
            "summary": summarize_events(events),
            "save_summary": summarize_save_events(run["save_events"]),
            "shutdown_state_events": run["shutdown_state_events"],
            "bins": bins,
        }

    report = {
        "schema": "world-column-source-x-bins.v2",
        "matched_chunk_x": [args.min_chunk_x, args.max_chunk_x],
        "bin_chunks": args.bin_chunks,
        "runs": rendered_runs,
        "interpretation_note": (
            "disk/complete means a stored column supplied a source result; "
            "procedural/disk_miss followed by procedural/committed means the "
            "column was generated after no disk source was found. Timing fields "
            "measure source service and queue delay, not visual readiness by themselves. "
            "WorldColumnSave queued/written/failed events trace save admission and "
            "slice writes only when the source trace is enabled; a final shutdown_state "
            "reports outstanding IO."
        ),
    }
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
