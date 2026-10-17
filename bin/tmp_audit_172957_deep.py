#!/usr/bin/env python3
"""Deeper 172957: all segments + spike histogram + stream lag proxies."""
from __future__ import annotations

import json
import statistics as st
from collections import Counter
from pathlib import Path

PATH = Path("bin/logs/perf_20260909-172957_8616.jsonl")
BASE = Path("bin/logs/perf_20260909-131116_27772.jsonl")
BAD = Path("bin/logs/perf_20260909-153045_20028.jsonl")


def load(path: Path):
    rows = []
    with path.open(encoding="utf-8") as f:
        for line in f:
            if '"kind":"period"' not in line and '"kind": "period"' not in line:
                continue
            r = json.loads(line)
            if r.get("kind") == "period":
                rows.append(r)
    return rows


def seg(rows):
    cruise = [r for r in rows if float(r.get("movement_speed") or 0) > 2.0]
    stand = [r for r in rows if float(r.get("movement_speed") or 0) <= 1.5]
    return cruise, stand


def med(xs):
    return st.median(xs) if xs else None


def series(rows, key):
    out = []
    for r in rows:
        if key == "fps":
            w = r.get("wall_ms")
            if w and w > 0:
                out.append(1000.0 / float(w))
            continue
        v = r.get(key)
        if v is not None:
            out.append(float(v))
    return out


def pct(xs, p):
    if not xs:
        return None
    xs = sorted(xs)
    k = (len(xs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def summarize(tag, rows):
    cruise, stand = seg(rows)
    print(f"\n### {tag} n={len(rows)} cruise={len(cruise)} stand={len(stand)}")
    for name, rs in [("ALL", rows), ("cruise", cruise), ("stand", stand)]:
        if not rs:
            continue
        walls = series(rs, "wall_ms")
        fpss = series(rs, "fps")
        holes = series(rs, "visual_holes")
        hf = sum(1 for v in holes if v > 0) / len(holes) if holes else None
        stuck = series(rs, "softdefer_empty_stuck_n")
        ok = series(rs, "mesh_dirty_schedule_ok_n")
        stream = series(rs, "stream_ms")
        emerge = series(rs, "mesh_emerge_ms")
        scene = series(rs, "scene_ms")
        meshing = series(rs, "column_meshing_n")
        unfin = series(rs, "unfinished_visual")
        discard = series(rs, "mesh_discarded_late")
        diffs = [abs(fpss[i] - fpss[i - 1]) for i in range(1, len(fpss))]
        spike_gt160 = sum(1 for w in walls if w > 160) / len(walls) if walls else None
        spike_gt200 = sum(1 for w in walls if w > 200) / len(walls) if walls else None
        print(
            f"  {name:6s} wall med/p90/max={med(walls):.1f}/{pct(walls,90):.1f}/{max(walls):.1f} "
            f"fps med/p10={med(fpss):.2f}/{pct(fpss,10):.2f} "
            f"jitter|d| med/p90={med(diffs):.2f}/{pct(diffs,90):.2f} "
            f"spike>160={spike_gt160:.2%} >200={spike_gt200:.2%}"
        )
        print(
            f"         holes_frac={hf} SoftDefer={med(stuck)} sched_ok={med(ok)} "
            f"stream/emerge/scene={med(stream):.1f}/{med(emerge):.1f}/{med(scene):.1f} "
            f"meshing={med(meshing)} unfin={med(unfin)} discard_late={med(discard)}"
        )


def wall_buckets(rows):
    walls = series(rows, "wall_ms")
    bins = [(0, 50), (50, 80), (80, 120), (120, 160), (160, 200), (200, 9999)]
    c = Counter()
    for w in walls:
        for a, b in bins:
            if a <= w < b:
                c[f"{a}-{b}"] += 1
                break
    return c


def main():
    for tag, path in [
        ("172957", PATH),
        ("131116", BASE),
        ("153045", BAD),
    ]:
        rows = load(path)
        summarize(tag, rows)
        cruise, _ = seg(rows)
        print(f"  cruise wall buckets: {dict(wall_buckets(cruise))}")


if __name__ == "__main__":
    main()
