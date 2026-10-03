#!/usr/bin/env python3
"""Audit finalize_pending_gate vs pending/black/deferred relight from perf jsonl."""
from __future__ import annotations

import json
import sys
from pathlib import Path


def load(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return [r for r in rows if r.get("kind") == "period"]


def pct(vals, p):
    if not vals:
        return None
    vals = sorted(vals)
    i = min(len(vals) - 1, int(round((len(vals) - 1) * p)))
    return vals[i]


def audit(label: str, path: Path, warmup: int = 5) -> None:
    periods = load(path)
    steady = periods[warmup:] if len(periods) > warmup else periods
    if not steady:
        print(f"{label}: no steady periods")
        return

    cap = [r for r in steady if float(r.get("relight_capture_ms") or 0) > 0.01]
    partial = sum(1 for r in cap if int(r.get("relight_capture_finalize") or 0) == 0)
    final = sum(1 for r in cap if int(r.get("relight_capture_finalize") or 0) == 1)
    apply_partial = sum(int(r.get("relight_apply_partial_n") or 0) for r in steady)
    apply_final = sum(int(r.get("relight_apply_final_n") or 0) for r in steady)

    vb = [float(r.get("visible_black_focus_n") or 0) for r in steady]
    pl = [float(r.get("pending_light_focus") or 0) for r in steady]
    uf = [float(r.get("unfinished_visual") or 0) for r in steady]
    defer_pending = [
        float(r.get("relight_deferred_far_pending") or 0) for r in steady
        if "relight_deferred_far_pending" in r
    ]

    stuck = 0
    run = 0
    for r in steady:
        pending = float(r.get("pending_light_focus") or 0) >= 5
        captured = float(r.get("relight_capture_ms") or 0) > 0.01
        partial_cap = int(r.get("relight_capture_finalize") or 0) == 0
        vb_pos = float(r.get("visible_black_focus_n") or 0) > 0
        if pending and captured and partial_cap and vb_pos:
            run += 1
            stuck = max(stuck, run)
        else:
            run = 0

    print(f"=== {label} ===")
    print(f"  file: {path.name}  steady={len(steady)}")
    print(
        f"  capture frames={len(cap)} partial={partial} final={final} "
        f"partial_rate={partial / len(cap) if cap else None}"
    )
    print(
        f"  apply partial/final frames={apply_partial}/{apply_final} "
        f"(needs new build telemetry if both 0)"
    )
    print(
        f"  pending_light_focus med/max={pct(pl, 0.5)}/{max(pl) if pl else None} "
        f"visible_black med/max={pct(vb, 0.5)}/{max(vb) if vb else None} "
        f"unfinished_visual max={max(uf) if uf else None}"
    )
    if defer_pending:
        print(
            f"  deferred_far_pending med/max="
            f"{pct(defer_pending, 0.5)}/{max(defer_pending)}"
        )
    else:
        print("  deferred_far_pending: n/a (old perf log)")
    print(f"  pending+partial_capture+vb longest run={stuck * 2.0}s")
    skip_quiesce = [float(r.get("mark_relit_skip_enter_lit_quiesce_n") or 0) for r in steady]
    suppress_settled = [
        float(r.get("mark_relit_suppress_enter_settled_n") or 0) for r in steady
    ]
    schedule_n = [float(r.get("mark_relit_schedule_n") or 0) for r in steady]
    sticky_ins = [float(r.get("sticky_insert_stale_after_apply_n") or 0) for r in steady]
    if skip_quiesce or suppress_settled or schedule_n:
        print(
            f"  mark_relit skip_quiesce med/max={pct(skip_quiesce, 0.5)}/"
            f"{max(skip_quiesce) if skip_quiesce else None} "
            f"suppress_settled med/max={pct(suppress_settled, 0.5)}/"
            f"{max(suppress_settled) if suppress_settled else None} "
            f"schedule med/max={pct(schedule_n, 0.5)}/"
            f"{max(schedule_n) if schedule_n else None}"
        )
        print(
            f"  sticky_insert_stale_after_apply sum="
            f"{sum(sticky_ins) if sticky_ins else 0}"
        )
    else:
        print("  mark_relit settle telemetry: n/a (needs Era22 build)")
    print()


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    logs = root / "bin" / "logs"
    targets = sorted(logs.glob("perf_20260820-09*.jsonl"))
    if len(sys.argv) > 1:
        targets = [Path(p) for p in sys.argv[1:]]
    if not targets:
        print("No perf logs found")
        return 1
    for p in targets:
        audit(p.stem, p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
