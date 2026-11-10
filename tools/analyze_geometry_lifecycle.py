#!/usr/bin/env python3
"""Summarize geometry revision bumps, mesh scheduling, and publication in perf JSONL.

The trace is bounded and sparse. Missing schedule rows mean the scheduler event
was not retained; they do not prove that a revision was never scheduled.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


Coord = tuple[int, int, int]


def parse_coord(value: str) -> Coord:
    try:
        parts = tuple(int(part.strip()) for part in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("coordinate must be x,y,z integers") from exc
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("coordinate must be x,y,z integers")
    return parts  # type: ignore[return-value]


def coord_of(row: dict[str, Any]) -> Coord | None:
    values = (row.get("cx"), row.get("cy"), row.get("cz"))
    if any(value is None for value in values):
        return None
    return tuple(int(value) for value in values)  # type: ignore[return-value]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_jsonl", type=Path)
    parser.add_argument("--coord", type=parse_coord,
                        help="limit output to one x,y,z chunk coordinate")
    parser.add_argument("--max-coords", type=int, default=20,
                        help="maximum coordinates in summary without --coord")
    parser.add_argument("--output", type=Path,
                        help="write JSON here instead of stdout")
    args = parser.parse_args()
    if args.max_coords < 1:
        parser.error("--max-coords must be positive")

    bumps: dict[Coord, list[dict[str, Any]]] = defaultdict(list)
    schedules: dict[Coord, list[dict[str, Any]]] = defaultdict(list)
    publications: dict[Coord, list[dict[str, Any]]] = defaultdict(list)
    malformed_rows = 0
    with args.perf_jsonl.open(encoding="utf-8") as stream:
        for line in stream:
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                malformed_rows += 1
                continue
            if not isinstance(row, dict):
                continue
            coord = coord_of(row)
            if coord is None or (args.coord is not None and coord != args.coord):
                continue
            kind = row.get("kind")
            if kind == "demand_transition_trace" and row.get("event") == "mesh_revision_bumped":
                bumps[coord].append(row)
            elif kind in ("mesh_schedule_trace", "mesh_watched_schedule_trace"):
                schedules[coord].append(row)
            elif kind == "visual_lifecycle_trace" and row.get("stage") == "published":
                publications[coord].append(row)

    coords = set(bumps) | set(schedules) | set(publications)
    ranked = sorted(
        coords,
        key=lambda coord: (
            len(bumps.get(coord, ())),
            max((int(row.get("frame_epoch", 0)) for row in bumps.get(coord, ())), default=0),
        ),
        reverse=True,
    )
    if args.coord is not None:
        ranked = [args.coord] if args.coord in coords else []
    else:
        ranked = ranked[:args.max_coords]

    summaries = []
    for coord in ranked:
        coord_bumps = sorted(
            bumps.get(coord, ()), key=lambda row: int(row.get("frame_epoch", 0))
        )
        coord_schedules = schedules.get(coord, ())
        coord_publications = publications.get(coord, ())
        revision_rows = []
        exact_publication_count = 0
        for bump in coord_bumps:
            revision = int(bump.get("mesh_revision_after", bump.get("desired_geom_rev", 0)))
            attempt_id = int(bump.get("attempt_id", 0) or 0)
            bump_frame = int(bump.get("frame_epoch", 0))
            matching_schedules = [
                row for row in coord_schedules
                if int(row.get("desired_geom_rev", row.get("mesh_revision", 0)) or 0) == revision
                and (attempt_id == 0 or int(row.get("attempt_id", 0) or 0) == attempt_id)
                and int(row.get("frame_epoch", 0)) >= bump_frame
            ]
            matching_publications = [
                row for row in coord_publications
                if int(row.get("published_geom_rev", 0) or 0) == revision
                and (attempt_id == 0 or int(row.get("attempt_id", 0) or 0) == attempt_id)
                and int(row.get("frame_epoch", 0)) >= bump_frame
            ]
            matching_publications.sort(key=lambda row: int(row.get("frame_epoch", 0)))
            exact = bool(matching_publications)
            exact_publication_count += int(exact)
            revision_rows.append({
                "revision": revision,
                "bump_frame": bump_frame,
                "bump_reason": bump.get("mesh_revision_bump_reason"),
                "queue_at_bump": bump.get("mesh_dirty_queue_kind"),
                "queue_index_at_bump": bump.get("mesh_dirty_queue_index"),
                "attempt_id": attempt_id,
                "scheduled_queue_kinds": sorted({
                    int(row.get("mesh_dirty_queue_kind", 0) or 0)
                    for row in matching_schedules
                }),
                "schedule_frames": sorted({
                    int(row.get("frame_epoch", 0)) for row in matching_schedules
                }),
                "published_exactly": exact,
                "publication_frame": (
                    int(matching_publications[0].get("frame_epoch", 0))
                    if exact else None
                ),
            })
        summaries.append({
            "coord": list(coord),
            "geometry_bump_count": len(coord_bumps),
            "bump_reasons": dict(Counter(
                row.get("mesh_revision_bump_reason", "unknown")
                for row in coord_bumps
            )),
            "watched_schedule_rows": sum(
                row.get("kind") == "mesh_watched_schedule_trace"
                for row in coord_schedules
            ),
            "scheduled_queue_kinds": dict(Counter(
                int(row.get("mesh_dirty_queue_kind", 0) or 0)
                for row in coord_schedules
            )),
            "exact_publication_count": exact_publication_count,
            "revision_rows": revision_rows,
        })

    result = {
        "perf_jsonl": str(args.perf_jsonl),
        "coord_filter": list(args.coord) if args.coord is not None else None,
        "malformed_rows": malformed_rows,
        "coordinates": summaries,
        "interpretation": (
            "Exact publication requires a matching published geometry revision "
            "and attempt. Missing schedule rows are bounded-trace coverage gaps, "
            "not proof that work was never scheduled."
        ),
    }
    serialized = json.dumps(result, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(serialized + "\n", encoding="utf-8")
    else:
        print(serialized)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
