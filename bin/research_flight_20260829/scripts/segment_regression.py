#!/usr/bin/env python3
"""Compare perf segment medians between two manual flight logs."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

FIELDS = [
    "stream_ms",
    "prep_refresh_pressure_ms",
    "prep_refresh_unfinished_ms",
    "prep_refresh_facing_ms",
    "prep_refresh_dirty_ms",
    "mesh_emerge_ms",
    "relight_drain_ms",
    "world_streaming_phase_ms",
    "admission_carve_out_frames",
    "mesh_admission_mode",
    "mesh_dirty_pending_gpu_n",
    "softdefer_capture_retarget_n",
    "dirty_fm_n",
    "fm_dirty_enqueue_n",
    "mesh_dirty_schedule_ok_n",
    "miss_completion_stuck_frames",
    "rim_witness_latched",
]


def load_cruise(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("kind") != "spike":
            continue
        if float(row.get("player_y") or 0) > 10:
            rows.append(row)
    return rows


def med(rows: list[dict], key: str) -> float | None:
    vals = [float(r[key]) for r in rows if r.get(key) is not None]
    return round(st.median(vals), 3) if vals else None


def carve_audit(rows: list[dict], label: str) -> None:
    print(f"\n--- carve-out audit ({label}) n={len(rows)} ---")
    would = sum(
        1
        for r in rows
        if float(r.get("mesh_admission_mode") or 0) == 3
        and float(r.get("dirty_fm_n") or 0) == 0
    )
    pending_le8 = sum(
        1
        for r in rows
        if float(r.get("dirty_fm_n") or 0) == 0
        and float(r.get("mesh_dirty_pending_gpu_n") or r.get("pending_gpu") or 99) <= 8
    )
    carve_sum = sum(
        float(r.get("admission_carve_out") or r.get("admission_carve_out_frames") or 0)
        for r in rows
    )
    print(f"  HoleDrain+dirty_fm=0 frames: {would} ({100*would/max(1,len(rows)):.1f}%)")
    print(f"  dirty_fm=0 AND pending_gpu<=8: {pending_le8}")
    print(f"  admission_carve_out sum: {carve_sum:.0f}")


def main() -> int:
    a = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "bin/logs/perf_20260829-142846_30244.jsonl"
    b = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "bin/logs/perf_20260829-152403_29440.jsonl"
    ra, rb = load_cruise(a), load_cruise(b)
    print(f"=== Perf regression: {a.name} vs {b.name} ===\n")
    print(f"{'field':<32} {'142846':>12} {'152403':>12} {'delta':>10}")
    for key in FIELDS:
        va, vb = med(ra, key), med(rb, key)
        if va is None and vb is None:
            continue
        delta = ""
        if va is not None and vb is not None and va != 0:
            delta = f"{100 * (vb - va) / va:+.0f}%"
        elif va is not None and vb is not None:
            delta = f"{vb - va:+.3f}"
        print(f"{key:<32} {str(va):>12} {str(vb):>12} {delta:>10}")
    carve_audit(ra, a.name)
    carve_audit(rb, b.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
