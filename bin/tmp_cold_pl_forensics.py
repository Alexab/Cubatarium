#!/usr/bin/env python3
"""ColdPL Phase 0: PL plateau + underfeet telem forensics."""
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

    uf = [int(x) for x in col(u, "underfeet_opaque_present")]
    draw_ok = [int(x) for x in col(u, "underfeet_draw_ok")]
    uf_pl = [int(x) for x in col(u, "underfeet_pending_light")]
    print(f"  uf_flips={flips(uf)} rate={flips(uf) / max(1, n):.3f}")

    telem_only = 0
    for i in range(1, n):
        if uf[i] != uf[i - 1]:
            if draw_ok[i] == 1 and draw_ok[i - 1] == 1:
                telem_only += 1
    print(f"  uf_flip with draw_ok=1 (telem-ish): {telem_only}/{max(1, flips(uf))}")

    for i in range(1, min(n, 200)):
        if uf[i] != uf[i - 1]:
            print(
                f"  flip@{i}: opaque {uf[i - 1]}->{uf[i]} "
                f"draw_ok={draw_ok[i]} pl_feet={uf_pl[i]} "
                f"PL={pl[i] if i < len(pl) else '?'}"
            )
            if telem_only > 0 and sum(1 for j in range(1, n) if uf[j] != uf[j - 1]) > 5:
                break

    steady = u[90:] if len(u) > 120 else u[n // 2 :]
    print(
        f"  steady: wall={med(col(steady, 'wall_ms')):.1f} "
        f"stream={med(col(steady, 'stream_ms')):.1f} "
        f"PL={med(col(steady, 'pending_light_focus')):.1f} "
        f"revisit={med(col(steady, 'dirty_revisit_same_n')):.1f}"
    )


if __name__ == "__main__":
    default = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260822-093018_25864.jsonl")
    for p in sys.argv[1:] or [str(default)]:
        analyze(Path(p))
        print()
