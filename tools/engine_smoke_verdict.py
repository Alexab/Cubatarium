#!/usr/bin/env python3
"""Classify native engine evidence; missing coverage never grants acceptance.

This is intentionally separate from performance/proxy gates. Pixel darkness
alone is not a fault: only joined, explicitly attributed visual faults fail.
"""
from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
from typing import Any


def validate_first_frame_witness(row):
    """Check complete census shape before using a renderer-emitted decision."""
    if not isinstance(row, dict) or row.get("schema") not in (
            "required-presentation-witness.v1", "required-presentation-witness.v2"):
        return None
    current_domain = row["schema"] == "required-presentation-witness.v2"
    if current_domain and (row.get("domain") != "configured-render-ring.v1" or
            not isinstance(row.get("render_distance_chunks"), int) or
            isinstance(row["render_distance_chunks"], bool) or
            row["render_distance_chunks"] != row.get("radius")):
        return None
    if row.get("phase") != "first_world_paint" or row.get("render_pass_complete") is not True:
        return None
    names = ("radius", "columns", "expected_columns", "sections", "expected_sections",
             "missing_data_columns", "unknown_sections", "known_air_sections",
             "resident_air_sections", "surface_sections", "light_unready_sections",
             "publication_unready_sections", "world_epoch")
    if any(not isinstance(row.get(k), int) or isinstance(row[k], bool) or row[k] < 0 for k in names):
        return None
    lo, hi = row.get("min_y"), row.get("max_y")
    focus = row.get("focus")
    if (any(not isinstance(v, int) or isinstance(v, bool) for v in (lo, hi)) or hi < lo or
            not isinstance(focus, list) or len(focus) != 3 or
            any(not isinstance(v, int) or isinstance(v, bool) for v in focus)):
        return None
    columns = (2 * row["radius"] + 1) ** 2
    sections = columns * (hi // 16 - lo // 16 + 1)
    if (row["radius"] < (1 if current_domain else 4) or row["world_epoch"] == 0 or
            row["expected_columns"] != columns or row["expected_sections"] != sections or
            row["columns"] != columns or row["sections"] != sections or
            sum(row[k] for k in ("unknown_sections", "known_air_sections",
                "resident_air_sections", "surface_sections")) != sections or
            row["light_unready_sections"] > row["resident_air_sections"] + row["surface_sections"] or
            row["publication_unready_sections"] > row["surface_sections"] or
            row["missing_data_columns"] > columns):
        return None
    ready = all(row[k] == 0 for k in ("missing_data_columns", "unknown_sections",
                                      "light_unready_sections", "publication_unready_sections"))
    return ready if isinstance(row.get("ready"), bool) and row["ready"] == ready else None


def read_startup_warmup_evidence(log_paths) -> dict[str, Any]:
    """Recognize positive failure evidence; ordinary logs never prove readiness."""
    observed = []
    sections = {}
    columns = {}
    first_frames = []
    required_ring = None
    input_requests = dict(schema="required-world-input-requests.v1", records=0,
                          runtime_records=0, requested_columns=0, light_note_attempts=0,
                          max_epoch=0, max_focus_displacement=0, beyond_first_data_domain_records=0)
    first_input_focus = None
    first_input_radius = None
    input_foci = set()
    for path in log_paths:
        with Path(path).open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if "[RequiredWorldInputs] " in line:
                    match = re.search(r"focus=\((-?\d+),(-?\d+)\) presentation=(\d+) light=(\d+) data=(\d+) requested=(\d+) light_noted=(\d+) epoch=(\d+)", line)
                    if match:
                        x, z, presentation, light, data, requested, noted, epoch = map(int, match.groups())
                        if presentation > 0 and light in (presentation + 1, presentation + 2) and data == light + 1:
                            if first_input_focus is None:
                                first_input_focus = (x, z)
                                first_input_radius = data
                            displacement = max(abs(x - first_input_focus[0]), abs(z - first_input_focus[1]))
                            input_requests["records"] += 1
                            input_requests["runtime_records"] += int(epoch > 0)
                            input_requests["requested_columns"] += requested
                            input_requests["light_note_attempts"] += noted
                            input_requests["max_epoch"] = max(input_requests["max_epoch"], epoch)
                            input_requests["max_focus_displacement"] = max(input_requests["max_focus_displacement"], displacement)
                            input_requests["beyond_first_data_domain_records"] += int(displacement > first_input_radius)
                            if len(input_foci) < 1024:
                                input_foci.add((x, z))
                marker = "[RequiredPresentationWitness] "
                if marker in line:
                    try:
                        row = json.loads(line.split(marker, 1)[1])
                        elapsed = row["elapsed_ms"]
                        if (row.get("schema") in ("required-presentation-witness.v1", "required-presentation-witness.v2") and
                                row.get("phase") == "enter_gate" and
                                isinstance(elapsed, (int, float)) and not isinstance(elapsed, bool) and
                                math.isfinite(elapsed) and elapsed >= 0 and
                                (required_ring is None or elapsed >= required_ring["elapsed_ms"])):
                            required_ring = row
                    except (ValueError, KeyError, TypeError):
                        pass
                marker = "[FirstWorldFrameWitness] "
                if marker in line:
                    try:
                        row = json.loads(line.split(marker, 1)[1])
                        if validate_first_frame_witness(row) is not None:
                            first_frames.append(row)
                    except (ValueError, TypeError):
                        pass
                marker = "[EnterSectionWitness] "
                if marker in line:
                    try:
                        row = json.loads(line.split(marker, 1)[1])
                        coord = row["coord"]
                        elapsed = row["elapsed_ms"]
                        if (row.get("schema") == "enter-section-witness.v1" and
                                isinstance(coord, list) and len(coord) == 3 and
                                all(isinstance(v, int) and not isinstance(v, bool) for v in coord) and
                                isinstance(elapsed, (int, float)) and not isinstance(elapsed, bool) and
                                math.isfinite(elapsed) and elapsed >= 0):
                            key = tuple(coord)
                            old = sections.get(key)
                            if old is None or elapsed >= old["elapsed_ms"]:
                                sections[key] = row
                    except (ValueError, KeyError, TypeError):
                        pass
                marker = "[EnterColumnWitness] "
                if marker in line:
                    try:
                        row = json.loads(line.split(marker, 1)[1])
                        column = row["column"]
                        elapsed = row["elapsed_ms"]
                        if (row.get("schema") == "enter-column-witness.v1" and
                                isinstance(column, list) and len(column) == 2 and
                                all(isinstance(v, int) and not isinstance(v, bool) for v in column) and
                                isinstance(elapsed, (int, float)) and not isinstance(elapsed, bool) and
                                math.isfinite(elapsed) and elapsed >= 0):
                            key = tuple(column)
                            old = columns.get(key)
                            if old is None or elapsed >= old["elapsed_ms"]:
                                columns[key] = row
                    except (ValueError, KeyError, TypeError):
                        pass
                if "[EnterWarmup]" not in line:
                    continue
                fields = dict(re.findall(r"\b(settle_reason|elapsed_ms|visibility_debt|underfeet)=([^\s]+)", line))
                reason = fields.get("settle_reason", "")
                try:
                    elapsed = float(fields["elapsed_ms"])
                    debt = int(fields["visibility_debt"])
                    underfeet = int(fields["underfeet"])
                except (KeyError, ValueError):
                    continue
                if not math.isfinite(elapsed) or elapsed < 0 or debt < 0 or underfeet not in (0, 1):
                    continue
                forced = reason.startswith("force_") or reason.startswith("soft_force")
                observed.append(dict(reason=reason, elapsed_ms=elapsed,
                                     visibility_debt=debt, underfeet_ready=bool(underfeet),
                                     forced_with_debt=forced and (debt > 0 or underfeet == 0)))
    return dict(schema="startup-warmup-evidence.v1", records=observed,
                forced_with_debt=any(row["forced_with_debt"] for row in observed),
                first_frame_witnesses=first_frames,
                required_ring_witness=required_ring,
                required_world_input_requests=dict(input_requests,
                    first_focus=list(first_input_focus) if first_input_focus is not None else None,
                    first_data_radius=first_input_radius, distinct_recorded_foci=len(input_foci),
                    scope="observed consumer requests only; no proof of calculation/publication or visual correctness"),
                section_witnesses=[sections[key] for key in sorted(sections)][:64],
                column_witnesses=[columns[key] for key in sorted(columns)][:81])


def component(status: str, reason: str, **evidence: Any) -> dict[str, Any]:
    return {"status": status, "reason": reason, **evidence}


def finite_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def classify(report: dict[str, Any], perf: Path | None, *,
             minimum_chunks: int = 16, minimum_pixel_captures: int = 3) -> dict[str, Any]:
    runtime = component("UNTESTED", "missing_process_result")
    if report.get("hang_killed"):
        runtime = component("FAIL", "process_timed_out")
    elif "exit_code" in report:
        frames = report.get("ingame_frames")
        ok = report["exit_code"] == 0 and finite_number(frames) and frames > 0
        runtime = component("PASS" if ok else "FAIL", "native_process", exit_code=report["exit_code"])

    route = component("UNTESTED", "missing_route_evidence")
    start, end = report.get("start_focus"), report.get("end_focus")
    if report.get("teleport_cruise") is True:
        route = component("FAIL", "teleport_enabled")
    elif (report.get("teleport_cruise") is False and
          isinstance(start, list) and isinstance(end, list) and
          len(start) == len(end) == 2 and all(finite_number(v) for v in start + end)):
        distance = max(abs(end[i] - start[i]) for i in range(2))
        # Endpoint displacement supports one-way frontier tests only. A future
        # round-trip scenario must supply visited/evicted/reloaded witnesses.
        route = component("FAIL" if distance < minimum_chunks else "UNTESTED",
                          "insufficient_travel" if distance < minimum_chunks else "missing_3d_relocation_audit",
                          displacement_chunks=distance, minimum_chunks=minimum_chunks)
        relocations = report.get("diagnostic_relocations_after_start")
        if finite_number(relocations) and relocations > 0:
            route = component("FAIL", "diagnostic_relocation_during_route")
        elif distance >= minimum_chunks and finite_number(relocations) and relocations == 0:
            initial = report.get("initial_resident_column_count")
            outside = report.get("outside_initial_resident_samples")
            route = component("UNTESTED", "missing_initial_footprint_witness")
            if finite_number(initial) and initial > 0 and finite_number(outside):
                route = component("PASS" if outside > 0 else "FAIL",
                                  "frontier_route_covered" if outside > 0 else "initial_footprint_not_crossed",
                                  displacement_chunks=distance, outside_initial_resident_samples=outside)

    samples = 0
    bot = report.get("diagnostic_bot")
    if isinstance(bot, dict) and bot.get("enabled") is True and bot.get("state") != "complete":
        route = component("FAIL", "bot_route_incomplete", bot_state=bot.get("state"),
                          bot_reason=bot.get("reason"))
    captures: set[int] = set()
    faults = 0
    attributed = 0
    malformed = 0
    backend_keys = ("cpu_light_seed_apply", "gpu_light_seed_apply", "gpu_light_readback")
    backend_maxima: dict[str, int] = {}
    backend_positive: dict[str, int] = {}
    backend_kinds: dict[str, int] = {}
    backend_invalid = 0
    if perf and perf.is_file():
        with perf.open(encoding="utf-8") as stream:
            for line in stream:
                if not line.strip():
                    continue
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    malformed += 1
                    continue
                if not isinstance(row, dict):
                    malformed += 1
                    continue
                # Counters are consumed by event samples too. Looking only at
                # periodic records can miss all actual seed execution.
                present = [key for key in backend_keys if key in row]
                if present:
                    valid = all(isinstance(row[key], int) and
                                not isinstance(row[key], bool) and row[key] >= 0
                                for key in present)
                    if not valid:
                        backend_invalid += 1
                    else:
                        kind = row.get("kind") if isinstance(row.get("kind"), str) else "unknown"
                        backend_kinds[kind] = backend_kinds.get(kind, 0) + 1
                        for key in present:
                            backend_maxima[key] = max(backend_maxima.get(key, 0), row[key])
                            if row[key] > 0:
                                backend_positive[key] = backend_positive.get(key, 0) + 1
                if row.get("kind") != "renderer_pixel_probe":
                    continue
                samples += 1
                epoch = row.get("frame_epoch")
                if isinstance(epoch, int) and not isinstance(epoch, bool):
                    captures.add(epoch)
                # New schema is an explicit reference/depth/draw/light join.
                # Legacy sparse probes remain useful evidence, but cannot be
                # promoted to full visual acceptance by counting dark RGB.
                if (row.get("visual_reference_checked") is True and
                        isinstance(row.get("visual_fault"), bool)):
                    attributed += 1
                    faults += row.get("visual_fault") is True
    visual = component("UNTESTED", "missing_visual_reference", samples=samples,
                       captures=len(captures), attributed_samples=attributed, faults=faults)
    if faults:
        visual["status"], visual["reason"] = "FAIL", "attributed_visual_fault"
    elif malformed:
        visual["reason"] = "malformed_trace"
    elif len(captures) >= minimum_pixel_captures and attributed == samples and samples > 0:
        visual["status"], visual["reason"] = "PASS", "sampled_reference_checks_passed"

    ready = report.get("first_frame_required_ring_ready")
    startup = component("UNTESTED", "missing_first_frame_ring_witness")
    if ready is False:
        startup = component("FAIL", "first_frame_ring_witness")
    warmup = report.get("startup_warmup_evidence")
    if isinstance(warmup, dict):
        witnesses = warmup.get("first_frame_witnesses")
        if isinstance(witnesses, list) and len(witnesses) == 1:
            checked = validate_first_frame_witness(witnesses[0])
            if checked is not None:
                startup = component("PASS" if checked else "FAIL",
                                    "first_world_paint_required_ring_census",
                                    evidence=witnesses[0],
                                    scope=("configured render-distance ring publication/input readiness; no pixel or physical-light oracle"
                                           if witnesses[0]["schema"] == "required-presentation-witness.v2" else
                                           "legacy configured radius-four-or-larger ring publication/input readiness; no pixel or physical-light oracle"))
    if ready is False:
        startup = component("FAIL", "first_frame_ring_witness")
    if (isinstance(warmup, dict) and warmup.get("schema") == "startup-warmup-evidence.v1"
            and warmup.get("forced_with_debt") is True):
        startup = component("FAIL", "forced_startup_with_unresolved_coverage",
                            evidence=warmup)
    parts = {"runtime": runtime, "route": route, "startup": startup, "visual": visual}
    if isinstance(bot, dict) and bot.get("enabled") is True:
        requested = bot.get("inspection_capture_requested_n")
        queued = bot.get("inspection_capture_queued_n")
        verified = report.get("inspection_capture_files_verified_n")
        inspection = component("UNTESTED", "missing_inspection_capture_witness")
        if (finite_number(requested) and requested > 0 and
                finite_number(queued) and finite_number(verified)):
            ok = requested == queued == verified
            inspection = component("PASS" if ok else "FAIL", "inspection_capture_coverage",
                                   requested=requested, queued=queued, verified=verified)
        parts["inspection"] = inspection
    statuses = [part["status"] for part in parts.values()]
    status = "FAIL" if "FAIL" in statuses else ("UNTESTED" if "UNTESTED" in statuses else "PASS")
    return {"schema": "engine-smoke-verdict.v1", "status": status,
            "components": parts, "malformed_trace_rows": malformed,
            "backend_observations": {
                "maxima": backend_maxima, "positive_records": backend_positive,
                "records_by_kind": backend_kinds, "invalid_counter_rows": backend_invalid,
                "scope": "observed CPU/GPU sky counters; not per-job correctness or compute parity"},
            "scope": "one-way frontier route and sampled reference checks; not full-screen proof"}


def startup_only_verdict(report: dict[str, Any], full: dict[str, Any],
                         observation: dict[str, Any] | None = None) -> dict[str, Any]:
    """A scoped startup result can never grant frontier or visual acceptance."""
    parts = {key: dict(full["components"][key]) for key in ("runtime", "startup")}
    bot = report.get("diagnostic_bot")
    if (not isinstance(bot, dict) or bot.get("enabled") is not True or
            bot.get("state") != "complete" or
            bot.get("startup_observation") is not True or
            not finite_number(bot.get("waypoints_visited")) or
            bot.get("waypoints_visited") != 0 or
            report.get("teleport_cruise") is not False or
            not finite_number(report.get("diagnostic_relocations_after_start")) or
            report.get("diagnostic_relocations_after_start") != 0):
        parts["observation"] = component("FAIL", "missing_stationary_no_relocation_completion")
    else:
        parts["observation"] = observation or component("UNTESTED", "missing_stationary_native_trace")
    statuses = [part["status"] for part in parts.values()]
    status = "FAIL" if "FAIL" in statuses else ("UNTESTED" if "UNTESTED" in statuses else "PASS")
    for key in ("route", "inspection", "visual", "fixture_opaque_depth"):
        parts[key] = component("UNTESTED", "not_requested_in_startup_only_scope")
    return {"schema": "engine-startup-smoke-verdict.v1", "status": status,
            "components": parts, "scope": "startup publication/input readiness only; no frontier, pixel or physical-light acceptance"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--perf", type=Path)
    parser.add_argument("--minimum-chunks", type=int, default=16)
    parser.add_argument("--minimum-pixel-captures", type=int, default=3)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if args.minimum_chunks < 1 or args.minimum_pixel_captures < 1:
        parser.error("coverage minima must be positive")
    report = json.loads(args.report.read_text(encoding="utf-8"))
    if not isinstance(report, dict):
        parser.error("report must contain a JSON object")
    perf = args.perf
    if perf is None and report.get("perf_jsonl"):
        perf = Path(report["perf_jsonl"])
    verdict = classify(report, perf, minimum_chunks=args.minimum_chunks,
                       minimum_pixel_captures=args.minimum_pixel_captures)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(verdict, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verdict, ensure_ascii=False))
    return {"PASS": 0, "FAIL": 1, "UNTESTED": 2}[verdict["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
