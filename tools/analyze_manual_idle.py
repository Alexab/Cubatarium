#!/usr/bin/env python3
"""Analyze manual idle session: pending plateau and likely stuck columns."""
import json
import math
import os
import statistics
import sys
from pathlib import Path

FOCUS = (-526, 49)
FOCUS_RADIUS = 6  # RD=5 typical; override via argv


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def ring_columns(center, radius):
    cx, cz = center
    out = []
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            if max(abs(dx), abs(dz)) == radius:
                out.append((cx + dx, cz + dz))
    return sorted(out)


def disk_columns(center, radius):
    cx, cz = center
    out = []
    for dx in range(-radius, radius + 1):
        for dz in range(-radius, radius + 1):
            if cheb((cx + dx, cz + dz), center) <= radius:
                out.append((cx + dx, cz + dz))
    return sorted(out)


def load_disk_cols(world_dir: Path):
    chunks = world_dir / "chunks"
    if not chunks.is_dir():
        return set()
    cols = set()
    for fn in os.listdir(chunks):
        if not fn.endswith(".cchunk"):
            continue
        parts = fn.replace(".cchunk", "").split("_")
        if len(parts) != 3:
            continue
        x, cy, z = map(int, parts)
        if cy == 0:
            cols.add((x, z))
    return cols


def main() -> int:
    perf = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
        "bin/logs/perf_20260721-135222_4972.jsonl"
    )
    radius = int(sys.argv[2]) if len(sys.argv) > 2 else FOCUS_RADIUS
    world = Path(sys.argv[3]) if len(sys.argv) > 3 else Path("bin/worlds/World_164")

    rows = [json.loads(l) for l in perf.read_text(encoding="utf-8").splitlines() if l.strip()]
    periods = [r for r in rows if r.get("kind") == "period"]
    seg = [r for r in periods if (r.get("focus_cx"), r.get("focus_cz")) == FOCUS]

    print(f"perf={perf}")
    print(f"focus={FOCUS} radius={radius} idle_periods={len(seg)} (~{len(seg)*2}s)")

    if seg:
        pend = [float(r.get("pending_light_focus") or 0) for r in seg]
        print(
            "pending: start", pend[0],
            "min", min(pend), "med_last20", statistics.median(pend[-20:]),
        )
        for target in [25, 10, 5]:
            for i, p in enumerate(pend):
                if p <= target:
                    print(f"  first pend<={target} at idle ~{i*2}s")
                    break

    disk = disk_columns(FOCUS, radius)
    outer = ring_columns(FOCUS, radius)
    saved = load_disk_cols(world)

    # Movement vector: first focus in session vs stop focus
    starts = [(r.get("focus_cx"), r.get("focus_cz")) for r in periods if r.get("focus_cx")]
    uniq = []
    for pt in starts:
        if not uniq or pt != uniq[-1]:
            uniq.append(pt)
    move_from = uniq[0] if uniq else FOCUS
    print(f"movement: {move_from} -> {FOCUS} waypoints={len(uniq)}")

    def score_col(col):
        x, z = col
        fx, fz = FOCUS
        # trailing = opposite of movement (columns entered focus last)
        mx, mz = FOCUS[0] - move_from[0], FOCUS[1] - move_from[1]
        ox, oz = x - fx, z - fz
        trail_dot = ox * mx + oz * mz if (mx or mz) else 0
        dist = cheb(col, FOCUS)
        on_disk = col in saved
        # north edge (user moved +z)
        north = z - fz
        return {
            "col": col,
            "dist": dist,
            "trail_dot": trail_dot,
            "north": north,
            "on_disk": on_disk,
        }

    scored = [score_col(c) for c in disk if cheb(c, FOCUS) > 0]
    scored.sort(key=lambda s: (-s["dist"], -s["trail_dot"]))

    print("\n=== Outer ring (dist=radius) — likely stuck candidates ===")
    outer_scored = [score_col(c) for c in outer]
    outer_scored.sort(key=lambda s: (-s["trail_dot"], -s["north"]))
    for s in outer_scored[:20]:
        print(
            f"  {s['col']} dist={s['dist']} trail_dot={s['trail_dot']:.0f}"
            f" north={s['north']:+d} disk={s['on_disk']}"
        )

    # Heuristic: pick 5 columns = highest trail_dot on outer 2 rings
    cand = sorted(scored, key=lambda s: (-s["trail_dot"], -s["dist"]))[:12]
    print("\n=== Top 12 trailing-edge candidates (heuristic for 5 stuck) ===")
    for s in cand:
        print(
            f"  {s['col']} dist={s['dist']} trail_dot={s['trail_dot']:.0f}"
            f" north={s['north']:+d} disk={s['on_disk']}"
        )

    print("\n=== Suggested 5 stuck columns (heuristic) ===")
    pick = sorted(
        [s for s in scored if s["dist"] >= radius - 1],
        key=lambda s: (-s["trail_dot"], -s["dist"]),
    )[:5]
    for s in pick:
        wx, wz = s["col"][0] * 16, s["col"][1] * 16
        print(f"  ({s['col'][0]}, {s['col'][1]}) world~({wx}, ?, {wz}) dist={s['dist']}")

    # Exit gen from glog pattern: (-531, 45-53) during quit
    quit_gen = [(x, z) for x in range(-531, -530) for z in range(45, 54)]
    quit_gen += [( -531, z) for z in range(45, 54)]
    quit_cols = sorted(set([(-531, z) for z in range(45, 54)]))
    print("\n=== Columns generated during quit-save (from logs) ===")
    for c in quit_cols:
        print(f"  {c} dist={cheb(c, FOCUS)} trail={score_col(c)['trail_dot']:.0f}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
