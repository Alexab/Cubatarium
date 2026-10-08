#!/usr/bin/env python3
"""Audit frames where FM enqueue happened but dirty_fm_n stayed zero."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "bin/logs/perf_20260829-152403_29440.jsonl"


def load(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("kind") == "spike":
            rows.append(row)
    return rows


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    rows = [r for r in load(path) if float(r.get("player_y") or 0) > 10]
    print(f"=== FM enqueue/drain audit: {path.name} cruise n={len(rows)} ===\n")

    enqueue_zero = [
        r
        for r in rows
        if float(r.get("fm_dirty_enqueue_n") or 0) > 0
        and float(r.get("dirty_fm_n") or 0) == 0
    ]
    markrelit_zero = [
        r
        for r in rows
        if float(r.get("mark_relit_schedule_n") or 0) > 0
        and float(r.get("dirty_fm_n") or 0) == 0
    ]
    sched_ok_after_enqueue = 0
    for i, r in enumerate(rows[:-1]):
        if float(r.get("fm_dirty_enqueue_n") or 0) <= 0:
            continue
        nxt = rows[i + 1]
        if float(nxt.get("mesh_dirty_schedule_ok_n") or 0) > 0:
            sched_ok_after_enqueue += 1

    print(
        f"fm_enqueue>0 AND dirty_fm_n=0: {len(enqueue_zero)} "
        f"({100 * len(enqueue_zero) / max(1, len(rows)):.1f}%)"
    )
    print(
        f"mark_relit_schedule>0 AND dirty_fm_n=0: {len(markrelit_zero)} "
        f"({100 * len(markrelit_zero) / max(1, len(rows)):.1f}%)"
    )
    if enqueue_zero:
        sched0 = sum(
            1 for r in enqueue_zero if float(r.get("mesh_dirty_schedule_ok_n") or 0) == 0
        )
        print(f"  of enqueue_zero, schedule_ok=0: {sched0} ({100*sched0/max(1,len(enqueue_zero)):.1f}%)")
        modes = [float(r.get("mesh_admission_mode") or -1) for r in enqueue_zero]
        print(f"  admission_mode med={st.median(modes):.1f}")
        pending = [float(r.get("mesh_dirty_pending_gpu_n") or r.get("pending_gpu") or 0) for r in enqueue_zero]
        if any(pending):
            print(f"  pending_gpu med={st.median(pending):.1f}")

    enq_frames = [r for r in rows if float(r.get("fm_dirty_enqueue_n") or 0) > 0]
    if enq_frames:
        ratio = sched_ok_after_enqueue / max(1, len(enq_frames))
        print(f"enqueue frame followed by schedule_ok>0 next frame: {sched_ok_after_enqueue}/{len(enq_frames)} ({100*ratio:.1f}%)")

    carve = sum(
        float(r.get("admission_carve_out") or r.get("admission_carve_out_frames") or 0)
        for r in rows
    )
    print(f"admission_carve_out sum={carve:.0f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
