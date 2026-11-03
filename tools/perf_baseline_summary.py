#!/usr/bin/env python3
"""Aggregate repeated flight-sim baseline reports and quantify run spread."""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

METRIC_KEYS = (
    "wall_ms_med",
    "wall_ms_fly_med",
    "wall_ms_fly",
    "effective_fps_fly",
    "world_streaming_phase_ms",
    "stream_ms",
    "scene_ms",
    "mesh_emerge_ms",
    "dirty_med",
    "dirty_max",
    "unfinished_visual",
    "visual_holes_rate",
    "emerge_spike_frac",
    "spike_count",
    "gpu_draw_cmds_med",
    "opaque_cmd_total_med",
)

IDENTITY_KEYS = (
    "git_sha",
    "world",
    "world_seed_or_hash",
    "world_metadata_hash",
    "cold_warm_mode",
    "fog_on",
    "gpu_driver",
    "resolution",
)

BACKEND_KEYS = (
    "backend_mesher_mode",
    "backend_store_mode",
    "backend_cull_mode",
    "backend_lighting_mode",
)


def _load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def _get(data: dict, dotted: str):
    value = data
    for part in dotted.split("."):
        if not isinstance(value, dict):
            return None
        value = value.get(part)
    return value


def _rounded(value):
    if value is None or isinstance(value, bool):
        return value
    if isinstance(value, (int, float)) and math.isfinite(value):
        return round(float(value), 6)
    return value


def _run_record(path: Path) -> dict:
    report = _load(path)
    metrics = report.get("metrics") or {}
    startup = report.get("run_manifest", {}).get("world_startup") or {}
    gl = report.get("run_manifest", {}).get("gl_capabilities") or {}

    record = {
        "report": str(path),
        "pass": report.get("pass"),
        "perf_jsonl": report.get("perf_jsonl"),
        "periods": metrics.get("periods"),
        "steady_periods": metrics.get("steady_periods"),
        "focus_start": startup.get("start_focus"),
        "focus_end": startup.get("end_focus"),
        "startup_operation_ms": startup.get("startup_operation_ms"),
        "startup_operation": startup.get("startup_operation"),
        "runtime_world_matches_requested": startup.get(
            "runtime_world_matches_requested"
        ),
        "world_startup_operation": report.get("world_startup_operation"),
        "gl_version": gl.get("gl_version"),
    }
    for key in IDENTITY_KEYS:
        record[key] = _get(report, f"run_manifest.{key}")
    for key in BACKEND_KEYS:
        record[key] = metrics.get(key)
    for key in METRIC_KEYS:
        record[key] = metrics.get(key)

    app_path_value = _get(
        report, "run_manifest.artifact_paths.application_report"
    )
    if app_path_value:
        app_path = Path(app_path_value)
        record["application_report"] = str(app_path)
        try:
            app = _load(app_path)
        except (OSError, json.JSONDecodeError, UnicodeDecodeError):
            app = {}
        record["app_start_focus"] = app.get("start_focus")
        record["app_end_focus"] = app.get("end_focus")
        record["app_chunks_traveled"] = app.get("chunks_traveled_cheb")
        record["app_wall_elapsed_sec"] = app.get("wall_elapsed_sec")
        record["app_ingame_frames"] = app.get("ingame_frames")
        route = app.get("route_line_tracking") or {}
        record["app_end_route_deviation_blocks"] = route.get(
            "end_lateral_deviation_blocks"
        )
        record["app_obstacle_attempts"] = (
            (app.get("obstacle_avoidance") or {}).get("attempts")
        )
    return record


def _numeric_series(records: list[dict], key: str) -> list[float]:
    values = []
    for record in records:
        value = record.get(key)
        if isinstance(value, (int, float)) and math.isfinite(value):
            values.append(float(value))
    return values


def _spread(records: list[dict], key: str) -> dict:
    values = _numeric_series(records, key)
    if not values:
        return {"n": 0}
    stats = {
        "n": len(values),
        "min": min(values),
        "median": statistics.median(values),
        "max": max(values),
        "range": max(values) - min(values),
    }
    if len(values) > 1 and statistics.mean(values) != 0:
        stats["population_cv"] = statistics.pstdev(values) / abs(
            statistics.mean(values)
        )
    return stats


def _apply_common_prefix(record: dict, chunks: int) -> None:
    """Slice perf period rows to a shared traveled prefix along X."""
    start = record.get("app_start_focus")
    perf_path_value = record.get("perf_jsonl")
    if not isinstance(start, (list, tuple)) or len(start) < 2:
        record["common_prefix_error"] = "missing application start focus"
        return
    perf_path = Path(str(perf_path_value)) if perf_path_value else None
    if perf_path is None or not perf_path.is_file():
        record["common_prefix_error"] = "missing perf jsonl"
        return

    end_cx = int(start[0]) - chunks
    rows = []
    try:
        with perf_path.open("r", encoding="utf-8") as stream:
            for line in stream:
                try:
                    row = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if row.get("kind") != "period":
                    continue
                try:
                    focus_cx = int(row.get("focus_cx"))
                except (TypeError, ValueError):
                    continue
                if focus_cx >= end_cx:
                    rows.append(row)
    except OSError as exc:
        record["common_prefix_error"] = str(exc)
        return

    def median_field(key: str):
        values = []
        for row in rows:
            value = row.get(key)
            if isinstance(value, (int, float)) and math.isfinite(value):
                values.append(float(value))
        return statistics.median(values) if values else None

    prefix = {
        "chunks": chunks,
        "end_focus_cx_threshold": end_cx,
        "period_rows": len(rows),
        "wall_ms_med": median_field("wall_ms"),
        "world_streaming_phase_ms_med": median_field(
            "world_streaming_phase_ms"
        ),
        "stream_ms_med": median_field("stream_ms"),
        "scene_ms_med": median_field("scene_ms"),
        "mesh_emerge_ms_med": median_field("mesh_emerge_ms"),
        "dirty_med": median_field("dirty"),
        "gpu_draw_cmds_med": median_field("gpu_draw_cmds"),
    }
    wall_ms_med = prefix["wall_ms_med"]
    prefix["effective_fps"] = (
        1000.0 / wall_ms_med if wall_ms_med and wall_ms_med > 0 else None
    )
    record["common_prefix"] = prefix


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", required=True, choices=("static", "moving"))
    parser.add_argument(
        "--common-prefix-chunks",
        type=int,
        help="moving-only: compare a fixed traveled prefix instead of unequal routes",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="optional JSON output path; default writes to stdout",
    )
    parser.add_argument("reports", nargs="+", type=Path)
    args = parser.parse_args()

    if args.common_prefix_chunks is not None:
        if args.mode != "moving":
            parser.error("--common-prefix-chunks is only valid for --mode moving")
        if args.common_prefix_chunks <= 0:
            parser.error("--common-prefix-chunks must be positive")

    records = []
    errors = []
    for path in args.reports:
        if not path.is_absolute():
            path = ROOT / path
        try:
            record = _run_record(path)
            if args.common_prefix_chunks is not None:
                _apply_common_prefix(record, args.common_prefix_chunks)
            records.append(record)
        except (OSError, json.JSONDecodeError, UnicodeDecodeError) as exc:
            errors.append({"report": str(path), "error": str(exc)})

    comparison = {}
    for key in (
        "wall_ms_fly_med",
        "effective_fps_fly",
        "world_streaming_phase_ms",
        "scene_ms",
        "mesh_emerge_ms",
        "dirty_med",
        "gpu_draw_cmds_med",
    ):
        comparison[key] = _spread(records, key)
    if args.common_prefix_chunks is not None:
        for source, target in (
            ("wall_ms_med", "common_prefix.wall_ms_med"),
            (
                "world_streaming_phase_ms_med",
                "common_prefix.world_streaming_phase_ms_med",
            ),
            ("stream_ms_med", "common_prefix.stream_ms_med"),
            ("scene_ms_med", "common_prefix.scene_ms_med"),
            ("mesh_emerge_ms_med", "common_prefix.mesh_emerge_ms_med"),
            ("dirty_med", "common_prefix.dirty_med"),
            ("gpu_draw_cmds_med", "common_prefix.gpu_draw_cmds_med"),
            ("effective_fps", "common_prefix.effective_fps"),
        ):
            comparison[target] = _spread(
                [r.get("common_prefix") or {} for r in records], source
            )

    result = {
        "schema": "perf-baseline-summary.v2",
        "mode": args.mode,
        "common_prefix_chunks": args.common_prefix_chunks,
        "reports_seen": len(args.reports),
        "records_loaded": len(records),
        "records": records,
        "comparison": comparison,
        "errors": errors,
    }
    encoded = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    else:
        sys.stdout.write(encoded)
    return 0 if records and not errors else 2


if __name__ == "__main__":
    raise SystemExit(main())
