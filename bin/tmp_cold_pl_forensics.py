#!/usr/bin/env python3
"""ColdPL / FlickerZero forensics: PL, VB, underfeet, opaque gap."""
from __future__ import annotations

import json
import statistics
import sys
from collections import Counter
from pathlib import Path


def load_spikes(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def p90(xs):
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return None
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def flips(xs):
    n = 0
    prev = None
    for v in xs:
        if prev is not None and v != prev:
            n += 1
        prev = v
    return n


def col(rows, k):
    return [r.get(k) for r in rows if k in r]


def blink_rate(rows, key):
    xs = [int(x) for x in col(rows, key) if x is not None]
    return flips(xs) / max(1, len(xs))


def segment(rows, start_s, end_s):
    i0 = max(0, start_s // 2)
    i1 = min(len(rows), end_s // 2)
    return rows[i0:i1]


def analyze(path: Path):
    u = load_spikes(path)
    n = len(u)
    print(f"=== {path.name} spikes={n} ~{n * 2}s ===")

    partial = col(u, "relight_apply_partial_n")
    final = col(u, "relight_apply_final_n")
    finalize = col(u, "relight_capture_finalize")
    if partial:
        print(
            f"  apply partial med={med(partial):.1f} final med={med(final):.1f} "
            f"finalize_rate={sum(1 for x in finalize if x) / max(1, len(finalize)):.2f}"
        )

    pl = [float(x) for x in col(u, "pending_light_focus") if x is not None]
    buckets = Counter()
    for v in pl:
        if v <= 10:
            buckets["0-10"] += 1
        elif v <= 25:
            buckets["11-25"] += 1
        elif v <= 40:
            buckets["26-40"] += 1
        else:
            buckets["41+"] += 1
    print(f"  PL med={med(pl):.1f} buckets={dict(buckets)}")

    vb = [float(x) for x in col(u, "visible_black_focus_n") if x is not None]
    nt = [float(x) for x in col(u, "visible_black_no_ticket_n") if x is not None]
    if vb:
        print(
            f"  VB med={med(vb):.1f} p90={p90(vb):.1f} "
            f"no_ticket med={med(nt):.1f} p90={p90(nt):.1f}"
        )
        print(f"  vb_blink rate={blink_rate(u, 'visible_black_focus_n'):.3f}")
        print(f"  no_ticket_blink rate={blink_rate(u, 'visible_black_no_ticket_n'):.3f}")

    cpu_vis = col(u, "opaque_refs_cpu_vis")
    rr = col(u, "opaque_refs_render_ready")
    if cpu_vis and rr:
        gaps = [
            float(a) - float(b)
            for a, b in zip(cpu_vis, rr)
            if a is not None and b is not None
        ]
        if gaps:
            print(f"  opaque_gap med={med(gaps):.1f} p90={p90(gaps):.1f}")

    uf = [int(x) for x in col(u, "underfeet_opaque_present")]
    draw_ok = [int(x) for x in col(u, "underfeet_draw_ok")]
    uf_pl = [int(x) for x in col(u, "underfeet_pending_light")]
    print(f"  uf_flips={flips(uf)} rate={flips(uf) / max(1, n):.3f}")

    uf_pred = [int(x) for x in col(u, "underfeet_opaque_present_predicted")]
    if uf_pred:
        print(
            f"  uf_predicted_flips={flips(uf_pred)} "
            f"rate={flips(uf_pred) / max(1, n):.3f}"
        )

    telem_only = 0
    for i in range(1, n):
        if uf[i] != uf[i - 1]:
            if draw_ok[i] == 1 and draw_ok[i - 1] == 1:
                telem_only += 1
    print(f"  uf_flip with draw_ok=1 (telem-ish): {telem_only}/{max(1, flips(uf))}")

    for label, seg in [
        ("enter_0-60s", segment(u, 0, 60)),
        ("mid_60-120s", segment(u, 60, 120)),
        ("steady_120s+", segment(u, 120, n * 2)),
    ]:
        if not seg:
            continue
        print(
            f"  [{label}] wall={med(col(seg, 'wall_ms')):.1f} "
            f"PL={med(col(seg, 'pending_light_focus')):.1f} "
            f"VB={med(col(seg, 'visible_black_focus_n')):.1f} "
            f"no_ticket={med(col(seg, 'visible_black_no_ticket_n')):.1f} "
            f"revisit={med(col(seg, 'dirty_revisit_same_n')):.1f} "
            f"fluid={med(col(seg, 'fluid_map_cpu_ms')):.1f}"
        )

    steady = u[60:] if len(u) > 90 else u[n // 2 :]
    print(
        f"  steady: wall={med(col(steady, 'wall_ms')):.1f} "
        f"stream={med(col(steady, 'stream_ms')):.1f} "
        f"PL={med(col(steady, 'pending_light_focus')):.1f} "
        f"revisit={med(col(steady, 'dirty_revisit_same_n')):.1f}"
    )


if __name__ == "__main__":
    default = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260822-151946_3628.jsonl")
    for p in sys.argv[1:] or [str(default)]:
        analyze(Path(p))
        print()
