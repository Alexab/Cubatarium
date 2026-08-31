#!/usr/bin/env python3
"""I17-P0: correlate unfinished_visual blink windows with mesh/witness telemetry."""
from __future__ import annotations

import argparse
import json
import statistics as st
from pathlib import Path

BLINK_FIELDS = [
    "unfinished_visual",
    "visual_holes",
    "miss_cx",
    "miss_cy",
    "miss_cz",
    "miss_horiz",
    "focus_cx",
    "focus_cz",
    "mesh_apply_stale_delta",
    "mesh_discarded_late",
    "softdefer_witness_horiz",
    "softdefer_witness_retarget_delta",
    "softdefer_capture_retarget_blocked_n",
    "mesh_dirty_schedule_ok_n",
    "dirty_fm_n",
    "visible_black_stalled_n",
    "prep_refresh_gap_ms",
    "stream_ms",
    "wall_ms",
]


def load_periods(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "period":
            rows.append(r)
    return rows


def load_blinks(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "blink":
            rows.append(r)
    return rows


def find_blink_windows(periods: list[dict]) -> list[tuple[int, dict]]:
    out = []
    prev = 0.0
    for i, p in enumerate(periods):
        cur = float(p.get("unfinished_visual") or 0)
        if (prev <= 0 and cur > 0) or (prev > 0 and cur >= prev + 2):
            out.append((i, p))
        prev = cur
    return out


def classify_window(p: dict) -> str:
    stale = float(p.get("mesh_apply_stale_delta") or 0)
    witness = float(p.get("softdefer_witness_retarget_delta") or 0)
    discarded = float(p.get("mesh_discarded_late_delta") or 0)
    vb_stall = float(p.get("visible_black_stalled_n") or 0)
    if stale > 0:
        return "stale_apply"
    if witness > 0:
        return "witness_hop"
    if discarded > 0:
        return "discarded_late"
    if vb_stall >= 15:
        return "vb_stall"
    return "unknown"


def audit(path: Path, label: str) -> None:
    periods = load_periods(path)
    blinks = load_blinks(path)
    windows = find_blink_windows(periods)
    print(f"\n=== {label}: {path.name} periods={len(periods)} "
          f"blink_windows={len(windows)} blink_events={len(blinks)} ===")
    if not windows:
        print("  (no blink windows)")
        return
    classes: dict[str, int] = {}
    for rank, (idx, p) in enumerate(windows[:8]):
        cls = classify_window(p)
        classes[cls] = classes.get(cls, 0) + 1
        print(
            f"  #{rank + 1} i={idx} class={cls} unf={p.get('unfinished_visual')} "
            f"miss=({p.get('miss_cx')},{p.get('miss_cz')}) nh={p.get('miss_horiz')} "
            f"stale_d={p.get('mesh_apply_stale_delta')} wit_d={p.get('softdefer_witness_retarget_delta')} "
            f"sched_ok={p.get('mesh_dirty_schedule_ok_n')} gap={p.get('prep_refresh_gap_ms'):.1f}"
            if p.get("prep_refresh_gap_ms") is not None
            else f"  #{rank + 1} i={idx} class={cls}"
        )
    print("  class histogram:", dict(sorted(classes.items(), key=lambda kv: -kv[1])))
    if blinks:
        print(f"  embedded blink events: {len(blinks)}")


def compare(baseline: Path, current: Path) -> None:
    b = find_blink_windows(load_periods(baseline))
    c = find_blink_windows(load_periods(current))
    print(f"\n=== delta {current.name} vs {baseline.name} ===")
    print(f"  blink windows: {len(b)} -> {len(c)}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    ap.add_argument("--baseline", type=Path, default=None)
    ap.add_argument("--label", default="")
    args = ap.parse_args()
    audit(args.log, args.label or args.log.stem)
    if args.baseline:
        compare(args.baseline, args.log)


if __name__ == "__main__":
    main()
