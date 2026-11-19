#!/usr/bin/env python3
"""Join selected-section scheduling logs to independent native depth faults.

This is diagnostic evidence, not a visual acceptance gate. Missing trace rows
never prove readiness; the pixel/depth verdict remains authoritative.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


FIELDS = re.compile(r"(\w+)=(\([^)]*\)|[^\s]+)")
COORD = re.compile(r"\(?(-?\d+),\s*(-?\d+),\s*(-?\d+)\)?\Z")
EXECUTORS = ("inflight", "capture_pending", "awaiting_light", "light_refresh")


def analyze(artifacts: Path, verdict: dict) -> dict:
    faults = verdict.get("components", {}).get("fixture_opaque_depth", {}).get(
        "fault_attribution", {}).get("sections", [])
    fault_boundaries = {
        tuple(fault["coord"]): {fault.get("first_frame_epoch"), fault.get("last_frame_epoch")}
        for fault in faults
    }
    sections: dict[tuple[int, ...], dict] = {}
    malformed = 0
    logs = sorted((artifacts / "logs").glob("*.log.INFO.*"))
    for path in logs:
        with path.open(encoding="utf-8", errors="replace") as source:
            for line in source:
                if "[ChunkPublication]" not in line and "[ScreenRayRepair]" not in line:
                    continue
                tag = "[ScreenRayRepair] " if "[ScreenRayRepair]" in line else "[ChunkPublication] "
                payload = line.split(tag, 1)[-1]
                fields = dict(FIELDS.findall(payload))
                coord = COORD.fullmatch(fields.get("coord", ""))
                if not coord:
                    malformed += 1
                    continue
                key = tuple(map(int, coord.groups()))
                section = sections.setdefault(key, {
                    "coord": list(key), "events": Counter(),
                    "repair_actions": Counter(), "timeline": [],
                    "queue_only_samples": 0, "longest_queue_only_run": None,
                    "maximum_observed_queue_age": 0, "_run": None,
                })
                event = payload.split(" coord=", 1)[0]
                if "[ScreenRayRepair]" in line:
                    section["repair_actions"][fields.get("action", "unknown")] += 1
                    continue
                section["events"][event] += 1
                if event != "watch":
                    # Retain a bounded history. Aggregate counts remain complete.
                    if len(section["timeline"]) < 64:
                        section["timeline"].append({"event": event, **fields})
                    continue
                try:
                    frame = int(fields["frame_epoch"])
                    revision = int(fields["revision"])
                    queued = int(fields["queue_kind"]) > 0
                    age = int(fields["queue_age"])
                    owners = [int(fields[name]) for name in EXECUTORS]
                    if any(value < 0 for value in owners) or frame < 0 or age < 0:
                        raise ValueError("invalid scheduling metadata")
                except (KeyError, ValueError):
                    malformed += 1
                    section["_run"] = None
                    continue
                if frame in fault_boundaries.get(key, ()):
                    sample = {
                        "frame_epoch": frame, "revision": revision,
                        "queue_kind": int(fields["queue_kind"]),
                        **dict(zip(EXECUTORS, owners)),
                    }
                    boundary = section.setdefault("fault_boundary_watch", [])
                    prior = next((row for row in boundary if row["frame_epoch"] == frame), None)
                    if prior is None:
                        boundary.append(sample)
                    elif prior != sample:
                        section["fault_boundary_watch_conflicts"] = section.get(
                            "fault_boundary_watch_conflicts", 0) + 1
                section["maximum_observed_queue_age"] = max(
                    age, section["maximum_observed_queue_age"])
                if not queued or any(owners):
                    section["_run"] = None
                    continue
                section["queue_only_samples"] += 1
                run = section["_run"]
                if not run or run["revision"] != revision or run["last_frame_epoch"] + 1 != frame:
                    run = {"first_frame_epoch": frame, "last_frame_epoch": frame,
                           "samples": 0, "revision": revision}
                    section["_run"] = run
                run["last_frame_epoch"] = frame
                run["samples"] += 1
                longest = section["longest_queue_only_run"]
                if longest is None or run["samples"] > longest["samples"]:
                    section["longest_queue_only_run"] = dict(run)
    for fault in faults:
        key = tuple(fault["coord"])
        section = sections.setdefault(key, {"coord": list(key)})
        section["depth_fault_attribution"] = fault
    for section in sections.values():
        section.pop("_run", None)
        section["trace_observed"] = bool(section.get("events"))
    return {
        "schema": "mesh-publication-diagnostics.v1",
        "analyzer_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "log_files": len(logs), "malformed_rows": malformed,
        "sections": [sections[key] for key in sorted(sections)],
        "scope": "Selected CUBA_TRACE_CHUNK scheduling observations plus ray-repair logs. "
                 "Queue-only means no executor reported by that watch sample; it is not "
                 "a proof of permanent starvation or its cause. Frame gaps/revisions break "
                 "runs. Resource preparation is not publication. Missing trace is unknown. "
                 "Fault-boundary watches precede that tick's work and are not exact pixel-time "
                 "stage samples; conflicting same-frame watches are counted. Renderer "
                 "owner bit 1 (mask 2) is an aggregate active-source flag, "
                 "not proof that a CPU builder is executing. "
                 "Independent depth faults are retained verbatim and never overridden.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", type=Path)
    args = parser.parse_args()
    verdict = json.loads((args.artifacts / "smoke-verdict.json").read_text(encoding="utf-8"))
    report = analyze(args.artifacts, verdict)
    output = args.artifacts / "mesh-publication-diagnostics.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
