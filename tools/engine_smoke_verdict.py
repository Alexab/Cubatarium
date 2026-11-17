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


def read_startup_warmup_evidence(log_paths) -> dict[str, Any]:
    """Recognize positive failure evidence; ordinary logs never prove readiness."""
    observed = []
    sections = {}
    for path in log_paths:
        with Path(path).open(encoding="utf-8", errors="replace") as stream:
            for line in stream:
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
                section_witnesses=[sections[key] for key in sorted(sections)][:64])


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
    if isinstance(ready, bool):
        startup = component("PASS" if ready else "FAIL", "first_frame_ring_witness")
    warmup = report.get("startup_warmup_evidence")
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
