#!/usr/bin/env python3
"""Decompose mesh_emerge_ms on idle stop segments (I3 diagnostic)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.flight_sim_analyze import (  # noqa: E402
    classify_stop_period,
    detect_longest_stop_segment,
    median,
    p95,
    segment_metrics,
)


EMERGE_PARTS = (
    ("mesh_emerge_prep_ms", "prep"),
    ("relight_drain_ms", "relight"),
    ("mesh_immediate_ms", "immediate"),
    ("mesh_dirty_tick_ms", "dirty_tick"),
    ("mesh_sync_ms", "sync"),
)


def load_periods(path: Path, warmup_sec: float) -> list[dict]:
    rows = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    periods = [r for r in rows if r.get("kind") == "period"]
    skip = max(2, int(warmup_sec / 2.0))
    return periods[skip:] if len(periods) > skip else periods


def emerge_split(rows: list[dict]) -> dict:
    if not rows:
        return {"n": 0, "parts": [], "residual_med": None, "residual_p95": None}

    emerge_vals = [float(r.get("mesh_emerge_ms") or 0) for r in rows]
    emerge_med = median(emerge_vals) or 0.0
    emerge_p95 = p95(emerge_vals) or 0.0

    parts = []
    accounted_med = 0.0
    accounted_p95 = 0.0
    for key, label in EMERGE_PARTS:
        vals = [float(r.get(key) or 0) for r in rows]
        med = median(vals) or 0.0
        hi = p95(vals) or 0.0
        pct = (100.0 * med / emerge_med) if emerge_med > 0 else 0.0
        parts.append(
            {
                "key": key,
                "label": label,
                "med": med,
                "p95": hi,
                "pct_of_emerge": pct,
            }
        )
        accounted_med += med
        accounted_p95 += hi

    residual_vals = [
        max(0.0, float(r.get("mesh_emerge_ms") or 0) - sum(float(r.get(k) or 0) for k, _ in EMERGE_PARTS))
        for r in rows
    ]
    residual_med = median(residual_vals) or 0.0
    residual_p95 = p95(residual_vals) or 0.0
    parts.append(
        {
            "key": "residual",
            "label": "residual",
            "med": residual_med,
            "p95": residual_p95,
            "pct_of_emerge": (100.0 * residual_med / emerge_med) if emerge_med > 0 else 0.0,
        }
    )

    return {
        "n": len(rows),
        "emerge_med": emerge_med,
        "emerge_p95": emerge_p95,
        "accounted_med": accounted_med,
        "accounted_p95": accounted_p95,
        "parts": parts,
    }


def debt_snapshot(rows: list[dict]) -> dict:
    if not rows:
        return {}
    last = rows[-1]
    return {
        "pending_light_focus_end": float(last.get("pending_light_focus") or 0),
        "unfinished_visual_end": float(last.get("unfinished_visual") or 0),
        "holes_end": float(last.get("holes") or last.get("unfinished_visual") or 0),
        "dirty_end": float(last.get("dirty") or 0),
        "mesh_async_end": float(last.get("mesh_async") or 0),
        "focus_end": [int(last.get("focus_cx") or 0), int(last.get("focus_cz") or 0)],
    }


def analyze_log(path: Path, warmup_sec: float, manual_idle: bool) -> dict:
    steady = load_periods(path, warmup_sec)
    stop = (
        detect_longest_stop_segment(steady, min_len=8)
        if manual_idle
        else steady[-30:] if len(steady) >= 30 else steady
    )
    by_class: dict[str, list[dict]] = {
        "calm_stop": [],
        "recovery_stop": [],
        "contaminated_stop": [],
    }
    for r in stop:
        by_class[classify_stop_period(r)].append(r)

    out = {
        "perf_jsonl": str(path),
        "stop_periods": len(stop),
        "stop_wall": segment_metrics(stop),
        "debt": debt_snapshot(stop),
        "calm_stop": {
            "n": len(by_class["calm_stop"]),
            "wall": segment_metrics(by_class["calm_stop"]),
            "emerge_split": emerge_split(by_class["calm_stop"]),
        },
        "recovery_stop": {
            "n": len(by_class["recovery_stop"]),
            "wall": segment_metrics(by_class["recovery_stop"]),
            "emerge_split": emerge_split(by_class["recovery_stop"]),
        },
        "all_stop": {
            "emerge_split": emerge_split(stop),
        },
    }
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("perf_jsonl", type=Path)
    ap.add_argument("--warmup-sec", type=float, default=16.0)
    ap.add_argument("--manual-idle", action="store_true")
    ap.add_argument("--report", type=Path, default=None)
    args = ap.parse_args()

    report = analyze_log(args.perf_jsonl, args.warmup_sec, args.manual_idle)
    text = json.dumps(report, indent=2)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
