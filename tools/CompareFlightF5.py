#!/usr/bin/env python3
"""Compare F5 retest perf/enter logs vs 141350 and 174657 baselines."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import AnalyzePhase57Scorecard as s  # noqa: E402
from AnalyzeEnterLit import analyze_enter_lit  # noqa: E402

KEYS = (
    "wall_med",
    "mesh_emerge_med",
    "mesh_apply_stale_med",
    "mesh_apply_superseded_med",
    "mesh_apply_drop_no_active_med",
    "unfinished_visual_med",
    "empty_backlog_med",
    "pool_retired_pending_med",
    "pool_fence_timeout_med",
    "pool_unsync_med",
)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--perf", type=Path, required=True, help="New perf JSONL")
    ap.add_argument(
        "--enter-lit",
        type=Path,
        default=None,
        help="New enter_lit JSONL",
    )
    ap.add_argument(
        "--base-141350",
        type=Path,
        default=ROOT / "bin/logs/perf_20260910-141350_27632.jsonl",
    )
    ap.add_argument(
        "--base-174657",
        type=Path,
        default=ROOT / "bin/logs/perf_20260910-174657_26996.jsonl",
    )
    args = ap.parse_args()

    new = s.analyze_perf(args.perf)
    b141 = s.analyze_perf(args.base_141350) if args.base_141350.exists() else {}
    b174 = s.analyze_perf(args.base_174657) if args.base_174657.exists() else {}

    print("=== F5 compare ===")
    print(f"new: {args.perf.name}")
    print(f"{'metric':32} {'141350':>12} {'174657':>12} {'new':>12}")
    for k in KEYS:
        print(
            f"{k:32} {s.fmt(b141.get(k)):>12} {s.fmt(b174.get(k)):>12} "
            f"{s.fmt(new.get(k)):>12}"
        )

    fails = s.evaluate_product(
        {
            "settle_count": 1,
            "soft_force_with_debt": [],
            "bad_live_soft_vis_debt": [],
            "empty_batch_event_n": 0,
        },
        new,
        b141,
    )
    print("product_vs_141350:", fails or "OK")

    if args.enter_lit and args.enter_lit.exists():
        enter = analyze_enter_lit(args.enter_lit)
        print("enter:", enter)
        if enter["gate_still_active_at_end"]:
            print("FAIL: enter gate still active at end")
            return 2
        if enter["max_continuous_gate_active_ms"] >= 60000:
            print("FAIL: gate active ~60s+")
            return 2

    stale = new.get("mesh_apply_stale_med")
    base_stale = b141.get("mesh_apply_stale_med")
    if stale is not None and base_stale is not None and float(stale) > 2.0 * float(
        base_stale
    ):
        print("FAIL: mesh_apply_stale > 2x 141350")
        return 2
    return 0 if not fails else 2


if __name__ == "__main__":
    raise SystemExit(main())
