#!/usr/bin/env python3
"""Analyze enter_lit_*.jsonl for gate duration / TTF / stale deltas (F0)."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def analyze_enter_lit(path: Path) -> dict:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            continue

    gate_active_run = 0
    max_gate_run_ms = 0.0
    last_active_start = None
    settle_reasons = []
    gate_end_n = 0
    first_presentable = None
    ttf_proxy = None
    stale_delta_sum = 0
    discarded_delta_sum = 0

    for r in rows:
        active = bool(
            r.get("enter_lit_gate_active", r.get("streaming_frozen", 0))
        )
        elapsed = float(r.get("elapsed_ms") or 0.0)
        if active:
            if last_active_start is None:
                last_active_start = elapsed
            gate_active_run = 1
        elif last_active_start is not None:
            run_ms = elapsed - last_active_start
            max_gate_run_ms = max(max_gate_run_ms, run_ms)
            last_active_start = None
            gate_active_run = 0
        if int(r.get("gate_end") or 0) == 1:
            gate_end_n += 1
            ge = r.get("gate_elapsed_ms")
            if ge is not None:
                max_gate_run_ms = max(max_gate_run_ms, float(ge))
        reason = r.get("settle_reason") or ""
        if reason and reason not in settle_reasons:
            settle_reasons.append(reason)
        fp = r.get("first_presentable_ms")
        if first_presentable is None and fp is not None and float(fp) >= 0:
            first_presentable = float(fp)
        tt = r.get("ttf_correct_proxy_ms")
        if ttf_proxy is None and tt is not None and float(tt) >= 0:
            ttf_proxy = float(tt)
        stale_delta_sum += int(r.get("mesh_apply_stale_delta") or 0)
        discarded_delta_sum += int(r.get("mesh_discarded_late_delta") or 0)

    if last_active_start is not None and rows:
        max_gate_run_ms = max(
            max_gate_run_ms, float(rows[-1].get("elapsed_ms") or 0) - last_active_start
        )

    last = rows[-1] if rows else {}
    return {
        "samples": len(rows),
        "max_continuous_gate_active_ms": max_gate_run_ms,
        "gate_still_active_at_end": bool(
            last.get("enter_lit_gate_active", last.get("streaming_frozen", 0))
        ),
        "gate_end_n": gate_end_n,
        "settle_reasons": settle_reasons,
        "first_presentable_ms": first_presentable,
        "ttf_correct_proxy_ms": ttf_proxy,
        "mesh_apply_stale_delta_sum": stale_delta_sum,
        "mesh_discarded_late_delta_sum": discarded_delta_sum,
        "last_gate_elapsed_ms": last.get("gate_elapsed_ms"),
        "last_ring_not_ready": last.get("ring_not_ready"),
        "last_visibility_debt": last.get("visibility_debt"),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("enter_lit_jsonl", type=Path)
    ap.add_argument(
        "--fail-if-no-settle",
        action="store_true",
        help="Exit 2 if gate closed without settle_reason (or still active).",
    )
    args = ap.parse_args()
    if not args.enter_lit_jsonl.exists():
        print(f"missing: {args.enter_lit_jsonl}")
        return 1
    out = analyze_enter_lit(args.enter_lit_jsonl)
    print(f"=== EnterLit {args.enter_lit_jsonl.name} ===")
    for k, v in out.items():
        print(f"  {k}={v}")
    if args.fail_if_no_settle:
        if out["gate_still_active_at_end"]:
            print("FAIL: enter_lit_gate still active at end of log")
            return 2
        if out["gate_end_n"] > 0 and not out["settle_reasons"]:
            print("FAIL: gate_end without settle_reason")
            return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
