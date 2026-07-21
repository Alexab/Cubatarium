#!/usr/bin/env python3
"""Go/no-go gate check for a flight_sim analyze JSON report.

Exit codes:
  0 — go
  2 — no-go (metrics)
  3 — hang / infra
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"

# phase_id -> list of (metric_key under metrics or top-level, op, limit)
PHASE_GATES: dict[str, list[tuple[str, str, float]]] = {
    "H": [],  # smoke only: hang_killed false checked always
    "R": [
        ("post_stop_black_sticky_max", "le", 1.0),
        ("post_stop_not_ready_end", "le", 80.0),
        ("stop_wall_med", "le", 120.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "V2a": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("stop_wall_med", "le", 90.0),
    ],
    "V2b": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_pending_med", "le", 40.0),
        ("stop_wall_med", "le", 90.0),
    ],
    "V3": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("pending_light_focus_med", "le", 40.0),
        ("post_stop_pending_med", "le", 35.0),
        ("stop_wall_med", "le", 90.0),
    ],
    "F": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_pending_med", "le", 35.0),
        ("stop_wall_med", "le", 90.0),
        ("chunks_traveled", "ge", 3.0),
    ],
}


def metric(data: dict, key: str):
    m = data.get("metrics") or {}
    if key in m:
        return m.get(key)
    return data.get(key)


def check(op: str, val, limit: float) -> bool:
    if val is None:
        return False
    v = float(val)
    if op == "le":
        return v <= limit
    if op == "ge":
        return v >= limit
    if op == "lt":
        return v < limit
    if op == "gt":
        return v > limit
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("--phase-id", required=True)
    ap.add_argument(
        "--baseline",
        type=Path,
        default=None,
        help="optional baseline report for relative checks",
    )
    args = ap.parse_args()

    if not args.report.is_file():
        print(f"FAIL: missing report {args.report}", file=sys.stderr)
        return 3

    data = json.loads(args.report.read_text(encoding="utf-8"))
    if data.get("hang_killed"):
        print("NO-GO: hang_killed=true", file=sys.stderr)
        return 3

    gates = PHASE_GATES.get(args.phase_id, [])
    failed = []
    for key, op, limit in gates:
        val = metric(data, key)
        ok = check(op, val, limit)
        print(f"  {key}={val} {op} {limit} -> {'OK' if ok else 'FAIL'}")
        if not ok:
            failed.append(key)

    if args.baseline and args.baseline.is_file() and args.phase_id in ("V2b", "V3", "F"):
        base = json.loads(args.baseline.read_text(encoding="utf-8"))
        cur_end = metric(data, "post_stop_not_ready_end")
        base_end = metric(base, "post_stop_not_ready_end")
        if cur_end is not None and base_end is not None:
            ok = float(cur_end) < float(base_end)
            print(
                f"  not_ready_end {cur_end} < baseline {base_end} -> "
                f"{'OK' if ok else 'FAIL'}"
            )
            if not ok:
                failed.append("not_ready_end_vs_baseline")

    if failed:
        print(f"NO-GO phase={args.phase_id} failed={failed}", file=sys.stderr)
        return 2
    print(f"GO phase={args.phase_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
