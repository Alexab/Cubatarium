#!/usr/bin/env python3
import json
from pathlib import Path
from statistics import median


def load_spikes(path):
    rows = []
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)
    return rows


def col(u, k):
    return [float(r[k]) for r in u if k in r and r[k] is not None]


def p90(xs):
    if not xs:
        return None
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def summarize(label, u):
    print(f"--- {label} n={len(u)} ---")
    keys = [
        "wall_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "dirty_n",
        "dirty_revisit_same_n",
        "pending_light_focus",
        "relight_apply_ms",
        "visible_black_focus_n",
        "chunk_meshed_unlit_hidden",
        "fluid_map_cpu_ms",
    ]
    for k in keys:
        xs = col(u, k)
        if xs:
            print(f"  {k:26s} med={median(xs):6.1f} p90={p90(xs):6.1f} max={max(xs):6.1f}")


def main():
    p = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260822-093018_25864.jsonl")
    u = load_spikes(p)
    n = len(u)
    print(f"total spikes={n}, chunks {u[0].get('chunk_count')}->{u[-1].get('chunk_count')}")
    summarize("ALL", u)
    summarize("skip enter (spikes 6..)", u[5:])
    summarize("first 60s (~30 spikes)", u[:30])
    summarize("60-180s (30..90)", u[30:90])
    summarize("180-420s (90..210)", u[90:210])
    summarize("420s+ (210..)", u[210:])
    summarize("last 60s (~30 spikes)", u[-30:])

    # PL distribution buckets
    pl = col(u, "pending_light_focus")
    buckets = {"0-10": 0, "11-25": 0, "26-40": 0, "41+": 0}
    for v in pl:
        if v <= 10:
            buckets["0-10"] += 1
        elif v <= 25:
            buckets["11-25"] += 1
        elif v <= 40:
            buckets["26-40"] += 1
        else:
            buckets["41+"] += 1
    print("PL buckets:", buckets)


if __name__ == "__main__":
    main()
