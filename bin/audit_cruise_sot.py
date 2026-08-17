#!/usr/bin/env python3
"""Audit cruise perf jsonl vs SoT SLA (docs/streaming_cruise_sot.md)."""
from __future__ import annotations

import argparse
import json
import statistics as st
from collections import Counter
from pathlib import Path

LOGS = Path(__file__).resolve().parent / "logs"

DEFAULT_LOGS = [
    ("perf_20260817-100319_6148.jsonl", "100319 inland closeout"),
    ("perf_20260816-184340_26004.jsonl", "184340 MANUAL"),
    ("perf_20260816-201626_4992.jsonl", "201626 pre-fix"),
    ("perf_20260815-203518_21932.jsonl", "203518 baseline"),
]


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
    return [r[key] for r in rows if isinstance(r.get(key), (int, float))]


def prep_hot(r):
    keys = [
        "prep_pending_light_ms",
        "prep_black_sticky_ms",
        "prep_dirty_count_ms",
        "prep_softdefer_setup_ms",
    ]
    s = 0.0
    ok = False
    for k in keys:
        if isinstance(r.get(k), (int, float)):
            s += float(r[k])
            ok = True
    if ok:
        return s
    if isinstance(r.get("mesh_emerge_prep_unfinished_ms"), (int, float)):
        return float(r["mesh_emerge_prep_unfinished_ms"])
    return None


def summarize(label, rows, min_chunks=80):
    cruise = [r for r in rows if (r.get("chunk_count") or 0) >= min_chunks]
    print(f"\n===== {label} raw={len(rows)} cruise={len(cruise)} =====")
    if not cruise:
        print("  (empty)")
        return cruise
    r0, r1 = cruise[0], cruise[-1]
    print(
        f"  focus ({r0.get('focus_cx')},{r0.get('focus_cz')}) -> "
        f"({r1.get('focus_cx')},{r1.get('focus_cz')}) "
        f"chunks {r0.get('chunk_count')}->{r1.get('chunk_count')}"
    )
    keys = [
        "wall_ms",
        "mesh_emerge_ms",
        "prep_softdefer_setup_ms",
        "softdefer_empty_scan_ms",
        "softdefer_empty_own_ms",
        "prep_pending_light_ms",
        "prep_black_sticky_ms",
        "prep_dirty_count_ms",
        "dirty",
        "dirty_fm_n",
        "dirty_remesh_n",
        "mesh_dirty_schedule_ok_n",
        "focus_missing_mesh",
        "visual_holes",
        "visible_black_focus_n",
        "opaque_cmd_on",
        "opaque_gpu_packed_n",
        "opaque_draw_n",
        "underfeet_has_mesh",
    ]
    for k in keys:
        vals = num(cruise, k)
        if not vals:
            continue
        print(
            f"  {k}: med={pct(vals,0.5)} p90={pct(vals,0.9)} "
            f"max={max(vals)} min={min(vals)}"
        )
    ph = [v for v in (prep_hot(r) for r in cruise) if v is not None]
    if ph:
        print(f"  prep_hot_sum: med={pct(ph,0.5)} p90={pct(ph,0.9)}")
    miss = 100.0 * sum(1 for r in cruise if r.get("focus_missing_mesh")) / len(cruise)
    holes = 100.0 * sum(1 for r in cruise if r.get("visual_holes")) / len(cruise)
    uf0 = 100.0 * sum(1 for r in cruise if not r.get("underfeet_has_mesh")) / len(cruise)
    print(f"  miss_frame%={miss:.1f} holes_frame%={holes:.1f} underfeet_missing%={uf0:.1f}")
    print("  underfeet_reason", Counter(r.get("underfeet_reason") for r in cruise).most_common(8))
    late = [r for r in cruise if (r.get("focus_cz") or 0) >= 55]
    if late:
        o = num(late, "opaque_draw_n") or num(late, "opaque_cmd_on")
        print(f"  late cz>=55 opaque_draw med/min={pct(o,0.5)}/{min(o) if o else None} n={len(late)}")
    return cruise


def sla_check(cruise):
    print("\n===== SLA vs docs/streaming_cruise_sot.md =====")
    wall = num(cruise, "wall_ms")
    ph = [v for v in (prep_hot(r) for r in cruise) if v is not None]
    opaque = num(cruise, "opaque_draw_n") or num(cruise, "opaque_cmd_on")
    vb = num(cruise, "visible_black_focus_n")
    ok_n = num(cruise, "mesh_dirty_schedule_ok_n")
    remesh = num(cruise, "dirty_remesh_n")
    print(f"  wall med/p90 target <=130/220 => {pct(wall,0.5)}/{pct(wall,0.9)}")
    print(f"  prep hot med target <=20 => {pct(ph,0.5) if ph else None}")
    print(f"  schedule_ok med (holes proxy >=8) => {pct(ok_n,0.5)}")
    print(f"  dirty_remesh med => {pct(remesh,0.5)}")
    print(f"  opaque_draw med/min => {pct(opaque,0.5)}/{min(opaque) if opaque else None}")
    print(f"  visible_black med target <=25 => {pct(vb,0.5)}")
    scan = num(cruise, "softdefer_empty_scan_ms")
    setup = num(cruise, "prep_softdefer_setup_ms")
    if scan or setup:
        print(
            f"  SoftDefer split: setup med={pct(setup,0.5)} "
            f"scan med={pct(scan,0.5)} "
            f"own med={pct(num(cruise,'softdefer_empty_own_ms'),0.5)}"
        )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", nargs="*", help="perf jsonl filenames under bin/logs")
    ap.add_argument("--logs-dir", type=Path, default=LOGS)
    args = ap.parse_args()
    pairs = [(n, n) for n in args.logs] if args.logs else DEFAULT_LOGS
    first_cruise = None
    for name, label in pairs:
        p = args.logs_dir / name
        if not p.exists() or p.stat().st_size == 0:
            print(f"\n===== {label} MISSING {p} =====")
            continue
        cruise = summarize(label, load(p))
        if first_cruise is None and cruise:
            first_cruise = cruise
            sla_check(cruise)


if __name__ == "__main__":
    main()
