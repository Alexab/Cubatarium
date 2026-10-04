#!/usr/bin/env python3
"""Summarize WorldColumnSource lifecycle events from application INFO logs."""

from __future__ import annotations

import argparse
import json
import math
import re
from collections import Counter, defaultdict
from pathlib import Path
from statistics import median
from typing import Any


EVENT_RE = re.compile(r"\[WorldColumnSource\]\s+(.*)$")
FIELD_RE = re.compile(r"([A-Za-z][A-Za-z0-9_]*)=([^\s]+)")
COORD_RE = re.compile(r"\((-?\d+),0,(-?\d+)\)")


def numeric(values: list[dict[str, str]], field: str) -> dict[str, float | int] | None:
    parsed: list[float] = []
    for event in values:
        try:
            parsed.append(float(event[field]))
        except (KeyError, ValueError):
            continue
    if not parsed:
        return None
    ordered = sorted(parsed)
    p95_index = min(len(ordered) - 1, math.ceil(len(ordered) * 0.95) - 1)
    return {
        "count": len(ordered),
        "median": round(median(ordered), 4),
        "p95": round(ordered[p95_index], 4),
        "max": round(ordered[-1], 4),
        "sum": round(sum(ordered), 4),
    }


def coords(events: list[dict[str, str]]) -> set[str]:
    result: set[str] = set()
    for event in events:
        match = COORD_RE.fullmatch(event.get("coord", ""))
        if match:
            result.add(f"{match.group(1)},0,{match.group(2)}")
    return result


def summarize(paths: list[Path]) -> dict[str, Any]:
    events: list[dict[str, str]] = []
    files: list[dict[str, Any]] = []
    for path in paths:
        count = 0
        with path.open("r", encoding="utf-8", errors="replace") as source:
            for line in source:
                match = EVENT_RE.search(line)
                if not match:
                    continue
                fields = dict(FIELD_RE.findall(match.group(1)))
                if "source" not in fields or "outcome" not in fields:
                    continue
                fields["_file"] = str(path)
                events.append(fields)
                count += 1
        files.append({"path": str(path), "events": count})

    grouped: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for event in events:
        grouped[(event["source"], event["outcome"])].append(event)

    counts = Counter((event["source"], event["outcome"]) for event in events)
    by_source: dict[str, dict[str, Any]] = {}
    for source in sorted({source for source, _ in grouped}):
        outcomes: dict[str, Any] = {}
        for (event_source, outcome), rows in sorted(grouped.items()):
            if event_source != source:
                continue
            entry: dict[str, Any] = {"count": len(rows)}
            timing_fields = (
                ("elapsed_ms", "worker_queue_ms", "file_open_ms", "file_read_ms",
                 "result_wait_ms", "deserialize_apply_ms", "disk_discovery_ms",
                 "format_detect_ms", "finalize_prelog_ms")
                if source == "disk" and outcome == "complete"
                else ("queue_ms", "generation_ms", "ready_wait_ms", "apply_ms", "total_ms")
                if source == "procedural" and outcome == "committed"
                else ("elapsed_ms", "distance_chunks", "remaining_slices")
            )
            for field in timing_fields:
                summary = numeric(rows, field)
                if summary is not None:
                    entry[field] = summary
            if source == "disk" and outcome == "complete":
                for field in ("chunkio_ready_loads", "chunkio_pending_jobs",
                              "chunkio_active_jobs", "pending_disk_columns"):
                    summary = numeric(rows, field)
                    if summary is not None:
                        entry[field] = summary
            if source == "procedural" and outcome == "committed":
                for field in ("ready_batch_n", "max_commits_per_frame",
                              "max_apply_budget_ms", "priority_refresh_n"):
                    summary = numeric(rows, field)
                    if summary is not None:
                        entry[field] = summary
            outcomes[outcome] = entry
        by_source[source] = outcomes

    disk_queued = coords(grouped.get(("disk", "queued"), []))
    disk_complete = coords(grouped.get(("disk", "complete"), []))
    disk_cancelled = coords(grouped.get(("disk", "cancelled_out_of_range"), []))
    proc_cancelled = coords(grouped.get(("procedural", "cancelled_out_of_range"), []))
    return {
        "schema": "world_column_source_trace.v1",
        "files": files,
        "event_count": len(events),
        "event_counts": {
            f"{source}/{outcome}": count
            for (source, outcome), count in sorted(counts.items())
        },
        "by_source": by_source,
        "unique_disk_coordinates": {
            "queued": len(disk_queued),
            "completed": len(disk_complete),
            "cancelled_out_of_range": len(disk_cancelled),
            "queued_without_terminal_event": len(
                disk_queued - disk_complete - disk_cancelled
            ),
        },
        "unique_procedural_cancelled_out_of_range": len(proc_cancelled),
        "note": (
            "Queued coordinates without a completion or cancellation log are an "
            "unmatched-event count; they do not prove a live pending owner. "
            "Timing values are source-log observations, not framebuffer-area metrics."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("info_logs", nargs="+", type=Path)
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    report = summarize(args.info_logs)
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    if args.json_out:
        args.json_out.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
