#!/usr/bin/env python3
"""Compare autofly vs manual ocean cruise analyze reports (Era30 H0 parity)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ANALYZE = Path(__file__).with_name("flight_sim_analyze.py")

PARITY_KEYS = [
    "fly_void_near_max",
    "fly_visible_black_max",
    "effective_holes_rate",
    "wall_ms_fly_med",
    "chunk_count_end",
    "pending_light_focus_med",
    "fly_frontier_pressure_frac",
    "fly_fluid_map_cpu_max",
    "enter_void_near_max",
    "enter_unfinished_max",
    "enter_app_update_max",
    "stop_dark_face_void_near_end",
    "relight_drain_near_zero_while_vb_sec",
]


def metric(report: dict, key: str):
    m = report.get("metrics") or {}
    if key in m:
        return m.get(key)
    return report.get(key)


def ratio(autofly, manual) -> float | None:
    if autofly is None or manual is None:
        return None
    mf = float(manual)
    af = float(autofly)
    if mf == 0.0:
        return 1.0 if af == 0.0 else float("inf")
    return af / mf


def compare_reports(manual: dict, autofly: dict) -> dict:
    rows = []
    for key in PARITY_KEYS:
        m_val = metric(manual, key)
        a_val = metric(autofly, key)
        rows.append(
            {
                "metric": key,
                "manual": m_val,
                "autofly": a_val,
                "ratio_autofly_over_manual": ratio(a_val, m_val),
            }
        )
    void_r = ratio(metric(autofly, "fly_void_near_max"), metric(manual, "fly_void_near_max"))
    holes_r = ratio(
        metric(autofly, "effective_holes_rate"),
        metric(manual, "effective_holes_rate"),
    )
    parity_valid = (
        (metric(autofly, "fly_void_near_max") or 0) >= 400.0
        and (metric(autofly, "effective_holes_rate") or 0) >= 0.40
    )
    return {
        "manual_perf": manual.get("perf_jsonl"),
        "autofly_perf": autofly.get("perf_jsonl"),
        "rows": rows,
        "parity_coefficients": {
            "void_ratio": void_r,
            "holes_ratio": holes_r,
        },
        "parity_valid_pre_fix": parity_valid,
        "parity_thresholds": {
            "fly_void_near_max_ge": 400.0,
            "effective_holes_rate_ge": 0.40,
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--manual-report", type=Path, required=True)
    ap.add_argument("--autofly-report", type=Path, required=True)
    ap.add_argument("--out", type=Path, default=ROOT / "bin/iter_reports/era30_ocean_parity.json")
    ap.add_argument(
        "--manual-jsonl",
        type=Path,
        default=None,
        help="optional: analyze manual jsonl if report missing metrics",
    )
    args = ap.parse_args()

    if not args.manual_report.is_file():
        print(f"FAIL: missing manual report {args.manual_report}", file=sys.stderr)
        return 2
    if not args.autofly_report.is_file():
        print(f"FAIL: missing autofly report {args.autofly_report}", file=sys.stderr)
        return 2

    manual = json.loads(args.manual_report.read_text(encoding="utf-8"))
    autofly = json.loads(args.autofly_report.read_text(encoding="utf-8"))

    if args.manual_jsonl and args.manual_jsonl.is_file():
        import importlib.util

        spec = importlib.util.spec_from_file_location("flight_sim_analyze", ANALYZE)
        if spec and spec.loader:
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            manual = mod.analyze(args.manual_jsonl, warmup_sec=16.0, manual_idle=True)

    result = compare_reports(manual, autofly)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    print(f"parity report: {args.out}")
    for row in result["rows"]:
        print(
            f"  {row['metric']}: manual={row['manual']} autofly={row['autofly']} "
            f"ratio={row['ratio_autofly_over_manual']}"
        )
    print(
        f"parity_valid_pre_fix={result['parity_valid_pre_fix']} "
        f"(void_ratio={result['parity_coefficients']['void_ratio']}, "
        f"holes_ratio={result['parity_coefficients']['holes_ratio']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
