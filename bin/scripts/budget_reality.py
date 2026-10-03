#!/usr/bin/env python3
"""Binding constraint SoT — count-cap vs time-cap vs measured wall (arch roadmap Phase 0)."""
from __future__ import annotations

import argparse
import json
import statistics as st
from pathlib import Path


def load_spikes(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)
    return rows


def med(xs: list) -> float | None:
    vals = [float(x) for x in xs if x is not None]
    return st.median(vals) if vals else None


def analyze(path: Path) -> None:
    rows = load_spikes(path)
    if not rows:
        print(f"No spike rows in {path}")
        return
    steady = rows[60:] if len(rows) > 120 else rows
    print(f"=== budget_reality: {path.name} spikes={len(rows)} steady={len(steady)} ===")
    fields = [
        ("wall_ms", "wall"),
        ("stream_ms", "stream"),
        ("mesh_emerge_ms", "mesh_emerge"),
        ("mesh_emerge_prep_other_ms", "prep_other"),
        ("prep_admission_ms", "prep_admission"),
        ("prep_schedule_clamp_ms", "prep_schedule_clamp"),
        ("prep_isolated_miss_ms", "prep_isolated_miss"),
        ("prep_refresh_pressure_ms", "prep_refresh"),
        ("prep_refresh_miss_ms", "refresh_miss"),
        ("prep_refresh_pending_ms", "refresh_pending"),
        ("prep_refresh_sticky_ms", "refresh_sticky"),
        ("prep_refresh_unfinished_ms", "refresh_unfinished"),
        ("prep_refresh_vb_ms", "refresh_vb"),
        ("prep_refresh_darkface_ms", "refresh_darkface"),
        ("prep_refresh_facing_ms", "refresh_facing"),
        ("mesh_dirty_schedule_ok_n", "schedule_ok"),
        ("first_mesh_schedule_cap", "fm_cap"),
        ("first_mesh_schedule_effective_cap", "fm_eff_cap"),
        ("fm_dirty_enqueue_reserve_n", "fm_reserve"),
        ("fm_dirty_enqueue_n", "fm_enqueue"),
        ("dirty_fm_n", "dirty_fm"),
        ("phase_budget_over", "phase_budget_over"),
        ("relight_apply_n", "apply_n"),
        ("relight_apply_ms", "apply_ms"),
        ("visible_black_focus_n", "vb"),
        ("visible_black_no_ticket_n", "vb_nt"),
    ]
    for key, label in fields:
        m = med([r.get(key) for r in steady])
        if m is not None:
            print(f"  {label:24s} med={m:.2f}" if isinstance(m, float) else f"  {label:24s} med={m}")
    pbo = sum(1 for r in steady if int(r.get("phase_budget_over") or 0))
    print(f"  phase_budget_over_pct     {100 * pbo / max(1, len(steady)):.1f}%")
    sched1 = sum(1 for r in steady if int(r.get("mesh_dirty_schedule_ok_n") or 0) <= 1)
    print(f"  schedule_ok<=1_pct        {100 * sched1 / max(1, len(steady)):.1f}%")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("perf_jsonl", type=Path)
    args = ap.parse_args()
    analyze(args.perf_jsonl)


if __name__ == "__main__":
    main()
