#!/usr/bin/env python3
"""Join selected-section scheduling logs to independent native depth faults.

This is diagnostic evidence, not a visual acceptance gate. Missing trace rows
never prove readiness; the pixel/depth verdict remains authoritative.
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re


FIELDS = re.compile(r"(\w+)=(\([^)]*\)|[^\s]+)")
COORD = re.compile(r"\(?(-?\d+),\s*(-?\d+),\s*(-?\d+)\)?\Z")
EXECUTORS = ("inflight", "capture_pending", "awaiting_light", "light_refresh")


def analyze(artifacts: Path, verdict: dict) -> dict:
    faults = verdict.get("components", {}).get("fixture_opaque_depth", {}).get(
        "fault_attribution", {}).get("sections", [])
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
        "log_files": len(logs), "malformed_rows": malformed,
        "sections": [sections[key] for key in sorted(sections)],
        "scope": "Selected CUBA_TRACE_CHUNK scheduling observations plus ray-repair logs. "
                 "Queue-only means no executor reported by that watch sample; it is not "
                 "a proof of permanent starvation or its cause. Frame gaps/revisions break "
                 "runs. Resource preparation is not publication. Missing trace is unknown. "
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
