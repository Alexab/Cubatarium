#!/usr/bin/env python3
"""Audit stop-segment VB drain: median/p95 VB/no_ticket/stalled and drain rate."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "bin/logs/perf_20260830-100455_33852.jsonl"


def load(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("kind") == "spike":
            rows.append(row)
    return rows


def pct(vals: list[float], p: float) -> float | None:
    if not vals:
        return None
    s = sorted(vals)
    idx = int(round((len(s) - 1) * p))
    return s[max(0, min(len(s) - 1, idx))]


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    rows = load(path)
    stop = [
        r
        for r in rows
        if float(r.get("player_y") or 0) > 10
        and float(r.get("movement_speed") or 0) < 0.5
    ]
    print(f"=== VB stop drain audit: {path.name} stop n={len(stop)} ===\n")
    if not stop:
        print("No stop segments found.")
        return 1

    vb = [float(r.get("visible_black_focus_n") or 0) for r in stop]
    nt = [float(r.get("visible_black_no_ticket_n") or 0) for r in stop]
    stalled = [float(r.get("visible_black_stalled_n") or 0) for r in stop]
    consume = [float(r.get("ticketed_vb_consume_n") or 0) for r in stop]
    gpu_finish = [float(r.get("gpu_finish_n") or 0) for r in stop]
    drain_frames = [float(r.get("stop_vb_drain_frames") or 0) for r in stop]

    print(
        f"VB med/p95: {st.median(vb):.0f} / {pct(vb, 0.95):.0f}  "
        f"no_ticket med/p95: {st.median(nt):.0f} / {pct(nt, 0.95):.0f}  "
        f"stalled med/p95: {st.median(stalled):.0f} / {pct(stalled, 0.95):.0f}"
    )
    vb_delta = vb[-1] - vb[0] if len(vb) >= 2 else 0.0
    consume_sum = sum(consume)
    gpu_sum = sum(gpu_finish)
    denom = consume_sum + gpu_sum
    print(
        f"stop tail VB delta: {vb_delta:.0f}  "
        f"ticketed_vb_consume sum: {consume_sum:.0f}  "
        f"gpu_finish sum: {gpu_sum:.0f}  "
        f"stop_vb_drain_frames max: {max(drain_frames):.0f}"
    )
    if denom > 0:
        print(f"drain efficiency (VB delta / (consume+gpu_finish)): {vb_delta / denom:.2f}")
    stalled_seg = [r for r in stop if float(r.get("visible_black_stalled_n") or 0) > 0]
    nt_seg = [r for r in stop if float(r.get("visible_black_no_ticket_n") or 0) > 0]
    print(
        f"stalled frames: {len(stalled_seg)}  no_ticket frames: {len(nt_seg)}  "
        f"stalled VB med: {st.median([float(r.get('visible_black_focus_n') or 0) for r in stalled_seg]) if stalled_seg else 0:.0f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
