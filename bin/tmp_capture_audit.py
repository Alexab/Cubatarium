#!/usr/bin/env python3
"""Audit relight Capture timing and completed ring from perf jsonl."""
from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def pct(vals, p):
    if not vals:
        return None
    vals = sorted(vals)
    i = min(len(vals) - 1, int(round((len(vals) - 1) * p)))
    return vals[i]


def num(rows, key):
    return [float(r[key]) for r in rows if isinstance(r.get(key), (int, float))]


def cruise(rows):
    fly = [
        r
        for r in rows
        if float(r.get("speed") or r.get("movement_speed") or 0) > 0.5
        or int(r.get("moving") or 0) == 1
    ]
    if len(fly) >= 3:
        return fly
    return [
        r
        for r in rows
        if float(r.get("chunks_traveled_delta") or 0) > 0
        or float(r.get("stream_pressure") or 0) > 0
    ]


def fifo_drop_period(rows):
    out = []
    prev = None
    for r in rows:
        cur = r.get("relight_fifo_dropped")
        if cur is None:
            continue
        cur = int(cur)
        if prev is not None and cur >= prev:
            out.append(cur - prev)
        prev = cur
    return out


def analyze(label: str, path: Path) -> None:
    rows = load(path)
    c = cruise(rows)
    cap = num(c, "relight_capture_ms")
    apply_ms = num(c, "relight_apply_ms")
    drain = num(c, "relight_drain_ms")
    wall = num(c, "wall_ms")
    completed = num(c, "relight_completed_n")
    apply_n = num(c, "relight_apply_n")
    fifo = num(c, "relight_fifo_n")
    pl = num(c, "pending_light_focus")
    cap_h = [
        r.get("relight_capture_col_horiz")
        for r in c
        if r.get("relight_capture_col_horiz") is not None
    ]
    fin = sum(1 for r in c if r.get("relight_capture_finalize") == 1)
    cap_pos = sum(1 for x in cap if x > 0.01)
    bg_cap = num(c, "capture_bg_cap_n")
    band_cy = num(c, "capture_band_cy")
    false_clear = num(c, "relight_false_clear_n")
    skipped_ug = num(c, "relight_skipped_underground_n")

    print(f"=== {label} ===")
    print(f"  file: {path.name}")
    print(f"  cruise rows={len(c)} capture>0={cap_pos} ({100 * cap_pos / max(1, len(c)):.0f}%)")
    print(
        f"  capture_ms med/p90/max="
        f"{pct(cap, 0.5)}/{pct(cap, 0.9)}/{max(cap) if cap else None}"
    )
    print(
        f"  apply_ms   med/p90/max="
        f"{pct(apply_ms, 0.5)}/{pct(apply_ms, 0.9)}/{max(apply_ms) if apply_ms else None}"
    )
    print(
        f"  drain_ms   med/p90/max="
        f"{pct(drain, 0.5)}/{pct(drain, 0.9)}/{max(drain) if drain else None}"
    )
    print(f"  wall_ms    med/p90={pct(wall, 0.5)}/{pct(wall, 0.9)}")
    print(
        f"  relight_completed_n med/max="
        f"{pct(completed, 0.5)}/{max(completed) if completed else None}"
    )
    print(
        f"  relight_apply_n med/max="
        f"{pct(apply_n, 0.5)}/{max(apply_n) if apply_n else None}"
    )
    print(f"  pending_light_focus med={pct(pl, 0.5)} fifo med={pct(fifo, 0.5)}")
    print(f"  capture_bg_cap_n med={pct(bg_cap, 0.5)} band_cy med={pct(band_cy, 0.5)}")
    print(f"  capture_finalize=1 frames={fin} cap_h top={Counter(cap_h).most_common(5)}")
    print(
        f"  false_clear max={max(false_clear) if false_clear else None} "
        f"skipped_underground max={max(skipped_ug) if skipped_ug else None}"
    )
    z = [
        r
        for r in c
        if float(r.get("relight_capture_ms") or 0) < 0.01
        and int(r.get("pending_light_focus") or 0) > 10
    ]
    print(f"  zero_capture+PL>10={len(z)} ({100 * len(z) / max(1, len(c)):.0f}%)")
    hot = [
        r
        for r in c
        if float(r.get("wall_ms") or 0) > 40
        and float(r.get("relight_capture_ms") or 0) < 0.01
    ]
    print(f"  wall>40 & capture=0={len(hot)}")
    # expensive captures
    exp = [x for x in cap if x >= 3.0]
    print(f"  capture>=3ms frames={len(exp)} ({100 * len(exp) / max(1, len(c)):.0f}%)")
    fd = fifo_drop_period(c)
    inflight = [int(r.get("async_relight_inflight") or 0) for r in c]
    print(
        f"  fifo_drop sum={sum(fd)} periods_with_drop={sum(1 for x in fd if x > 0)} "
        f"max_period={max(fd) if fd else 0}"
    )
    print(
        f"  async_relight_inflight med/max="
        f"{pct([float(x) for x in inflight], 0.5)}/{max(inflight) if inflight else 0}"
    )
    print(f"  relight_apply_n dist={Counter(int(r.get('relight_apply_n') or 0) for r in c).most_common(4)}")
    print()


def main():
    root = Path(__file__).resolve().parent
    reports = root / "suite_reports"
    targets = [
        ("F4a-v2 ocean", "20260820-073635_ocean-cruise.json"),
        ("F3ac ocean", "20260820-075606_ocean-cruise.json"),
        ("pre-F4 072930", "20260820-072930_ocean-cruise.json"),
        ("110712 ocean", "20260819-110712_ocean-cruise.json"),
        ("F4a-v2 land", "20260820-073635_land-cruise.json"),
        ("F3ac land", "20260820-075606_land-cruise.json"),
    ]
    for label, name in targets:
        rep = reports / name
        if not rep.exists():
            print(f"MISSING report {name}")
            continue
        data = json.loads(rep.read_text(encoding="utf-8"))
        perf = Path(data["perf_jsonl"])
        if not perf.exists():
            print(f"MISSING perf {perf}")
            continue
        analyze(label, perf)


if __name__ == "__main__":
    main()
