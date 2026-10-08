#!/usr/bin/env python3
import json
import sys
from pathlib import Path
from statistics import median
from collections import Counter


def load(p):
    return [
        json.loads(l)
        for l in Path(p).read_text(encoding="utf-8", errors="replace").splitlines()
        if l.startswith("{")
    ]


def use(rows):
    s = [r for r in rows if r.get("kind") == "spike"]
    return s if s else [r for r in rows if r.get("kind") == "period"]


def col(u, k):
    return [float(r[k]) for r in u if k in r and r[k] is not None]


def p90(xs):
    if not xs:
        return None
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def flips(xs):
    n = 0
    prev = None
    for v in xs:
        if prev is not None and v != prev:
            n += 1
        prev = v
    return n


def analyze(label, path):
    u = use(load(path))
    n = len(u)
    dur = n * 2
    print(f"=== {label} ({Path(path).name}) n={n} ~{dur}s ===")
    keys = [
        "wall_ms",
        "sim_ms",
        "stream_ms",
        "mesh_emerge_ms",
        "mesh_dirty_tick_ms",
        "relight_apply_ms",
        "relight_capture_ms",
        "dirty_n",
        "dirty_revisit_same_n",
        "pending_light_focus",
        "visible_black_focus_n",
        "chunk_meshed_unlit_hidden",
        "underfeet_opaque_present",
        "black_sticky",
        "fluid_map_cpu_ms",
    ]
    for k in keys:
        xs = col(u, k)
        if not xs:
            continue
        print(f"  {k:28s} med={median(xs):7.2f} p90={p90(xs):7.2f} max={max(xs):7.2f}")
    cls = Counter(r.get("dominant_spike_class") or r.get("spike_class") or "?" for r in u)
    print("  spike_class:", dict(cls))
    mid = max(1, n // 2)
    print(
        f"  early wall/stream/PL="
        f"{median(col(u[:mid], 'wall_ms')):.1f}/"
        f"{median(col(u[:mid], 'stream_ms')):.1f}/"
        f"{median(col(u[:mid], 'pending_light_focus')):.1f}"
    )
    print(
        f"  late  wall/stream/PL="
        f"{median(col(u[mid:], 'wall_ms')):.1f}/"
        f"{median(col(u[mid:], 'stream_ms')):.1f}/"
        f"{median(col(u[mid:], 'pending_light_focus')):.1f}"
    )
    uf = [int(x) for x in col(u, "underfeet_opaque_present")]
    print(f"  uf_flips={flips(uf)} rate={flips(uf)/max(1,n):.3f}")
    tops = sorted(u, key=lambda r: float(r.get("wall_ms") or 0), reverse=True)[:3]
    for i, r in enumerate(tops, 1):
        print(
            f"  top{i} wall={r.get('wall_ms'):.1f} stream={r.get('stream_ms')} "
            f"emerge={r.get('mesh_emerge_ms')} apply={r.get('relight_apply_ms')} "
            f"fluid={r.get('fluid_map_cpu_ms')} class={r.get('dominant_spike_class')}"
        )
    return u


def delta(a, b, key):
    xa, xb = col(a, key), col(b, key)
    if not xa or not xb:
        return None
    return median(xa) - median(xb)


if __name__ == "__main__":
    paths = [
        ("NEW093018", "E:/Work/Home/Cubatarium/bin/logs/perf_20260822-093018_25864.jsonl"),
        ("091818", "E:/Work/Home/Cubatarium/bin/logs/perf_20260822-091818_28920.jsonl"),
        ("220205", "E:/Work/Home/Cubatarium/bin/logs/perf_20260821-220205_23988.jsonl"),
        ("195128", "E:/Work/Home/Cubatarium/bin/logs/perf_20260821-195128_18004.jsonl"),
    ]
    if len(sys.argv) > 1:
        paths = [(Path(p).stem, p) for p in sys.argv[1:]]

    results = {}
    for label, p in paths:
        results[label] = analyze(label, p)
        print()

    if "NEW091818" in results and "220205" in results:
        a, b = results["NEW091818"], results["220205"]
        print("=== DELTA NEW091818 - 220205 (ColdWall target) ===")
        for k in [
            "wall_ms",
            "stream_ms",
            "mesh_emerge_ms",
            "dirty_n",
            "dirty_revisit_same_n",
            "pending_light_focus",
            "relight_apply_ms",
            "visible_black_focus_n",
        ]:
            d = delta(a, b, k)
            if d is not None:
                print(f"  {k}: {d:+.2f}")
