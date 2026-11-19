#!/usr/bin/env python3
"""Join selected-section scheduling logs to independent native depth faults.

This is diagnostic evidence, not a visual acceptance gate. Missing trace rows
never prove readiness; the pixel/depth verdict remains authoritative.
"""
from __future__ import annotations

import argparse
from collections import Counter, OrderedDict
import hashlib
import json
from pathlib import Path
import re


FIELDS = re.compile(r"(\w+)=(\([^)]*\)|[^\s]+)")
COORD = re.compile(r"\(?(-?\d+),\s*(-?\d+),\s*(-?\d+)\)?\Z")
EXECUTORS = ("inflight", "capture_pending", "awaiting_light", "light_refresh")


def analyze_light_inputs(logs, sections):
    """Join immutable input observations; never infer a current certificate."""
    jobs, loads, latest_load = OrderedDict(), OrderedDict(), OrderedDict()
    malformed = 0
    clock = re.compile(r"^[IWEF](\d{8} \d{2}:\d{2}:\d{2}\.\d+)")
    for path in logs:
        with path.open(encoding="utf-8", errors="replace") as source:
            for line in source:
                if not any(tag in line for tag in (
                        "[RelightAudit]", "[RelightUnknownInput]", "[WorldColumnSource]")):
                    continue
                fields = dict(FIELDS.findall(line))
                observed = clock.match(line)
                observed_at = observed.group(1) if observed else None
                try:
                    if "[WorldColumnSource]" in line:
                        if fields.get("source") != "disk" or fields.get("outcome") not in (
                                "queued", "complete", "field_input_promoted"):
                            continue
                        coord = tuple(map(int, COORD.fullmatch(fields["coord"]).groups()))
                        key = (coord, int(fields["demand_id"]))
                        load = loads.setdefault(key, {"coord": list(coord), "demand_id": key[1]})
                        if fields["outcome"] == "queued":
                            load["queued_at"] = observed_at
                            latest_load[coord] = load
                            latest_load.move_to_end(coord)
                            if len(latest_load) > 512:
                                latest_load.popitem(last=False)
                        elif fields["outcome"] == "field_input_promoted":
                            promoted = load.setdefault("promoted_slices", [])
                            if len(promoted) < 32:
                                promoted.append({"slice_y": int(fields["slice_y"]),
                                                 "observed_at": observed_at})
                        else:
                            load["completed_at"] = observed_at
                            for name in ("elapsed_ms", "result_wait_max_ms", "file_read_ms", "worker_queue_max_ms"):
                                if name in fields:
                                    load[name] = float(fields[name])
                        loads.move_to_end(key)
                        if len(loads) > 4096:
                            loads.popitem(last=False)
                        continue
                    event = line.split("[RelightAudit] ", 1)[-1].split(" ", 1)[0]
                    if "[RelightUnknownInput]" not in line and event not in (
                            "job_submit", "worker_done", "calculation_certificate", "slice_handoff"):
                        continue
                    job_id = int(fields["job"])
                    if job_id <= 0:  # Legacy unowned jobs must never share an identity.
                        continue
                    job = jobs.setdefault(job_id, {"job_id": job_id})
                    jobs.move_to_end(job_id)
                    if len(jobs) > 256:
                        jobs.popitem(last=False)
                    if "[RelightUnknownInput]" in line:
                        coord = tuple(map(int, COORD.fullmatch(fields["coord"]).groups()))
                        unknown = job.setdefault("reported_unknown_inputs", [])
                        if len(unknown) < 32:
                            unknown.append({"coord": list(coord), "observed_at": observed_at,
                                            "same_column_disk_load": latest_load.get((coord[0], 0, coord[2]))})
                    elif event == "job_submit":
                        job["submit"] = {"observed_at": observed_at, **fields}
                    elif event in ("worker_done", "calculation_certificate"):
                        record = {"observed_at": observed_at, **fields}
                        for name in ("inputs_complete", "unknown_inputs", "sections"):
                            if name in fields:
                                value = int(fields[name])
                                if value < 0 or (name == "inputs_complete" and value not in (0, 1)):
                                    raise ValueError("invalid light input observation")
                                record[name] = value
                        job["worker" if event == "worker_done" else "certificate"] = record
                    else:
                        coord = tuple(map(int, COORD.fullmatch(fields["coord"]).groups()))
                        if coord not in sections:
                            continue
                        diagnostic = sections[coord].setdefault("light_input_diagnostics", {
                            "handoff_events": 0, "jobs": [], "unretained_handoff_events": 0})
                        diagnostic["handoff_events"] += 1
                        row = next((x for x in diagnostic["jobs"] if x["calculation"]["job_id"] == job_id), None)
                        if row is None and len(diagnostic["jobs"]) < 64:
                            row = {"calculation": job}
                            diagnostic["jobs"].append(row)
                        if row is not None:
                            row["reported_handoff"] = {"observed_at": observed_at, **fields}
                        else:
                            diagnostic["unretained_handoff_events"] += 1
                except (AttributeError, KeyError, ValueError):
                    malformed += 1
    for section in sections.values():
        diagnostic = section.get("light_input_diagnostics")
        if diagnostic is None:
            continue
        workers = [row["calculation"].get("worker", {}) for row in diagnostic["jobs"]]
        diagnostic["retained_incomplete_input_jobs"] = sum(x.get("inputs_complete") == 0 for x in workers)
        unknown = [x["unknown_inputs"] for x in workers if "unknown_inputs" in x]
        diagnostic["maximum_reported_unknown_inputs"] = max(unknown) if unknown else None
        diagnostic["scope"] = (
            "Captured worker inputs and reported handoffs; certificate section counts do not "
            "prove a complete current field. Unknown coordinates are a bounded log subset. "
            "Same-column disk events are observations, not a matching data-token certificate "
            "or proof of framebuffer-time causality. Missing metadata stays unknown.")
    return malformed


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
    malformed_light_rows = analyze_light_inputs(logs, sections)
    for section in sections.values():
        section.pop("_run", None)
        section["trace_observed"] = bool(section.get("events"))
    return {
        "schema": "mesh-publication-diagnostics.v1",
        "analyzer_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "explicit_owner_stage_bits": {
            "12": "retained geometry awaiting compatible field",
            "13": "published artifact field refresh",
        },
        "log_files": len(logs), "malformed_rows": malformed,
        "malformed_light_input_rows": malformed_light_rows,
        "sections": [sections[key] for key in sorted(sections)],
        "scope": "Selected CUBA_TRACE_CHUNK scheduling observations plus ray-repair logs. "
                 "Queue-only means no executor reported by that watch sample; it is not "
                 "a proof of permanent starvation or its cause. Frame gaps/revisions break "
                 "runs. Resource preparation is not publication. Missing trace is unknown. "
                 "Fault-boundary watches precede that tick's work and are not exact pixel-time "
                 "stage samples; conflicting same-frame watches are counted. Renderer "
                 "owner bit 1 (mask 2) is an aggregate active-source flag, "
                 "not proof that a CPU builder is executing. "
                 "Light input histories join captured unknown coordinates to same-column "
                 "disk observations; these are not data-token or pixel-time certificates. "
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
