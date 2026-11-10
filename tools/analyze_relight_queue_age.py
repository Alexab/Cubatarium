#!/usr/bin/env python3
"""Estimate terrain-relight FIFO wait from timestamped RelightAudit glog lines.

The estimate starts at the first observed `queue_request outcome=inserted` for
a column and ends at its next `capture dequeue`. Coalesced requests do not reset
the clock. A dequeue without an observed insertion is reported as unknown; the
tool does not infer an age from queue position or from a light delta. With
`--jobs`, it also pairs `job_submit` and `apply` audit records by job id.
"""

from __future__ import annotations

import argparse
import math
import re
import statistics
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


GLOG_RE = re.compile(
    r"^[IWEF](\d{8})\s+(\d{2}:\d{2}:\d{2}\.\d+).*"
    r"\[RelightAudit\]\s+(.*)$"
)
QUEUE_REQUEST_RE = re.compile(
    r"relight_admission event=queue_request outcome=(\S+).*?"
    r"column=\((-?\d+),(-?\d+)\).*?queue=(\S+)"
)
SOURCE_RE = re.compile(r"\bsource=(\S+)")
DEQUEUE_RE = re.compile(
    r"capture dequeue column=\((-?\d+),(-?\d+)\) "
    r"queue=(\S+) remaining=(\d+)"
)
LIGHT_DELTA_RE = re.compile(
    r"light_delta job=(\d+) coord=\((-?\d+),(-?\d+),(-?\d+)\)"
)
JOB_SUBMIT_RE = re.compile(
    r"job_submit job=(\d+).*?column=\((-?\d+),(-?\d+)\).*?"
    r"band=(-?\d+):(-?\d+).*?source=(\S+)"
)
APPLY_RE = re.compile(r"apply job=(\d+).*?chunks=(\d+).*?changed=(\d+)")


@dataclass
class Enqueue:
    at: datetime
    queue: str
    source: str


@dataclass
class Dequeue:
    path: Path
    at: datetime
    column: tuple[int, int]
    queue: str
    remaining: int
    queued_ms: float | None
    enqueue: Enqueue | None


@dataclass
class JobTrace:
    path: Path
    job_id: int
    column: tuple[int, int] | None
    source: str
    submitted_at: datetime | None
    applied_at: datetime | None = None
    chunks: int | None = None
    changed_chunks: int | None = None


def parse_timestamp(date: str, time: str) -> datetime:
    return datetime.strptime(f"{date} {time}", "%Y%m%d %H:%M:%S.%f")


def parse_log(path: Path) -> tuple[list[Dequeue], int, list[JobTrace]]:
    pending: dict[tuple[int, int], Enqueue] = {}
    dequeues: list[Dequeue] = []
    jobs: dict[int, JobTrace] = {}
    light_deltas = 0

    with path.open("r", encoding="utf-8", errors="replace") as log:
        for raw_line in log:
            match = GLOG_RE.match(raw_line.rstrip("\r\n"))
            if not match:
                continue
            at = parse_timestamp(match.group(1), match.group(2))
            message = match.group(3)

            request = QUEUE_REQUEST_RE.search(message)
            if request:
                outcome, cx, cz, queue = request.groups()
                column = (int(cx), int(cz))
                if outcome == "inserted" and column not in pending:
                    source_match = SOURCE_RE.search(message)
                    source = source_match.group(1) if source_match else "unknown"
                    pending[column] = Enqueue(at, queue, source)
                continue

            submit = JOB_SUBMIT_RE.search(message)
            if submit:
                job, cx, cz, _min_y, _max_y, source = submit.groups()
                job_id = int(job)
                jobs[job_id] = JobTrace(
                    path=path,
                    job_id=job_id,
                    column=(int(cx), int(cz)),
                    source=source,
                    submitted_at=at,
                )
                continue

            delta = LIGHT_DELTA_RE.search(message)
            if delta:
                light_deltas += 1
                job, cx, _cy, cz = delta.groups()
                job_id = int(job)
                trace = jobs.setdefault(
                    job_id,
                    JobTrace(
                        path,
                        job_id,
                        (int(cx), int(cz)),
                        "submit_unobserved",
                        None,
                    ),
                )
                if trace.column is None:
                    trace.column = (int(cx), int(cz))
                continue

            apply = APPLY_RE.search(message)
            if apply:
                job, chunks, changed = apply.groups()
                job_id = int(job)
                trace = jobs.setdefault(
                    job_id,
                    JobTrace(path, job_id, None, "submit_unobserved", None),
                )
                trace.applied_at = at
                trace.chunks = int(chunks)
                trace.changed_chunks = int(changed)
                continue

            dequeue = DEQUEUE_RE.search(message)
            if not dequeue:
                continue
            cx, cz, queue, remaining = dequeue.groups()
            column = (int(cx), int(cz))
            enqueue = pending.pop(column, None)
            age_ms = (
                (at - enqueue.at).total_seconds() * 1000.0
                if enqueue is not None
                else None
            )
            dequeues.append(
                Dequeue(
                    path=path,
                    at=at,
                    column=column,
                    queue=queue,
                    remaining=int(remaining),
                    queued_ms=age_ms,
                    enqueue=enqueue,
                )
            )

    return dequeues, light_deltas, list(jobs.values())


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path, help="glog INFO files")
    parser.add_argument(
        "--column",
        action="append",
        default=[],
        metavar="CX,CZ",
        help="limit to a chunk column in chunk coordinates; may be repeated",
    )
    parser.add_argument("--events", action="store_true", help="print each dequeue")
    parser.add_argument(
        "--jobs", action="store_true", help="summarize submit-to-apply timing"
    )
    parser.add_argument(
        "--job-events", action="store_true", help="print each job timing row"
    )
    args = parser.parse_args()

    wanted: set[tuple[int, int]] | None = None
    if args.column:
        try:
            wanted = {
                tuple(int(part.strip()) for part in item.split(",", maxsplit=1))
                for item in args.column
            }
            if any(len(column) != 2 for column in wanted):
                raise ValueError
        except ValueError:
            parser.error("--column must use the form CX,CZ")

    rows: list[Dequeue] = []
    job_rows: list[JobTrace] = []
    delta_counts: dict[Path, int] = {}
    for path in args.logs:
        parsed, light_delta_count, parsed_jobs = parse_log(path)
        rows.extend(parsed)
        job_rows.extend(parsed_jobs)
        delta_counts[path] = light_delta_count

    if wanted is not None:
        rows = [row for row in rows if row.column in wanted]
        job_rows = [
            row for row in job_rows if row.column is not None and row.column in wanted
        ]

    by_column: dict[tuple[int, int], list[Dequeue]] = defaultdict(list)
    for row in rows:
        by_column[row.column].append(row)

    for path, count in delta_counts.items():
        print(f"log={path.name} light_delta_events={count}")
    if wanted is None:
        ages = [row.queued_ms for row in rows if row.queued_ms is not None]
        unknown = len(rows) - len(ages)
        if ages:
            print(
                f"queue_dequeues={len(rows)} observed_ages={len(ages)} "
                f"unknown_age={unknown} wait_ms_min={min(ages):.3f} "
                f"median={statistics.median(ages):.3f} "
                f"p95={percentile(ages, 0.95):.3f} max={max(ages):.3f}"
            )
        else:
            print(f"queue_dequeues={len(rows)} observed_ages=0 unknown_age={unknown}")
    else:
        for column, column_rows in sorted(by_column.items()):
            ages = [row.queued_ms for row in column_rows if row.queued_ms is not None]
            unknown = len(column_rows) - len(ages)
            if ages:
                print(
                    f"column={column[0]},{column[1]} dequeues={len(column_rows)} "
                    f"observed_ages={len(ages)} unknown_age={unknown} "
                    f"wait_ms_min={min(ages):.3f} "
                    f"median={statistics.median(ages):.3f} "
                    f"p95={percentile(ages, 0.95):.3f} max={max(ages):.3f}"
                )
            else:
                print(
                    f"column={column[0]},{column[1]} dequeues={len(column_rows)} "
                    f"observed_ages=0 unknown_age={unknown}"
                )

    if args.events:
        for row in rows:
            if row.queued_ms is None:
                print(
                    f"event={row.path.name} dequeue_at={row.at.isoformat()} "
                    f"column={row.column[0]},{row.column[1]} "
                    f"queue={row.queue} wait_ms=unknown "
                    f"remaining={row.remaining}"
                )
            else:
                print(
                    f"event={row.path.name} dequeue_at={row.at.isoformat()} "
                    f"enqueued_at={row.enqueue.at.isoformat()} "
                    f"column={row.column[0]},{row.column[1]} "
                    f"queue={row.queue} wait_ms={row.queued_ms:.3f} "
                    f"enqueue_source={row.enqueue.source} "
                    f"enqueue_queue={row.enqueue.queue} remaining={row.remaining}"
                )

    if args.jobs or args.job_events:
        paired = [
            row
            for row in job_rows
            if row.submitted_at is not None and row.applied_at is not None
        ]
        missing_submit = sum(row.submitted_at is None for row in job_rows)
        missing_apply = sum(row.applied_at is None for row in job_rows)
        print(
            f"jobs={len(job_rows)} paired={len(paired)} "
            f"missing_submit={missing_submit} missing_apply={missing_apply}"
        )
        latencies: list[float] = []
        by_source: dict[str, list[float]] = defaultdict(list)
        by_job_column: dict[tuple[int, int], list[float]] = defaultdict(list)
        for row in paired:
            elapsed = (row.applied_at - row.submitted_at).total_seconds() * 1000.0
            latencies.append(elapsed)
            by_source[row.source].append(elapsed)
            if row.column is not None:
                by_job_column[row.column].append(elapsed)
        if latencies:
            print(
                f"submit_to_apply_ms_median={statistics.median(latencies):.3f} "
                f"p95={percentile(latencies, 0.95):.3f} max={max(latencies):.3f}"
            )
        for source, source_latencies in sorted(by_source.items()):
            print(
                f"job_source={source} count={len(source_latencies)} "
                f"median_ms={statistics.median(source_latencies):.3f} "
                f"p95_ms={percentile(source_latencies, 0.95):.3f} "
                f"max_ms={max(source_latencies):.3f}"
            )
        if wanted is not None:
            for column, column_latencies in sorted(by_job_column.items()):
                print(
                    f"job_column={column[0]},{column[1]} "
                    f"count={len(column_latencies)} "
                    f"median_ms={statistics.median(column_latencies):.3f} "
                    f"p95_ms={percentile(column_latencies, 0.95):.3f} "
                    f"max_ms={max(column_latencies):.3f}"
                )
    if args.job_events:
        for row in job_rows:
            column = (
                f"{row.column[0]},{row.column[1]}"
                if row.column is not None
                else "unknown"
            )
            elapsed = (
                f"{(row.applied_at - row.submitted_at).total_seconds() * 1000.0:.3f}"
                if row.submitted_at is not None and row.applied_at is not None
                else "unknown"
            )
            print(
                f"job={row.job_id} column={column} source={row.source} "
                f"submit_to_apply_ms={elapsed} chunks={row.chunks} "
                f"changed_chunks={row.changed_chunks}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
