#!/usr/bin/env python3
"""Validate sticky-miss drain fix vs manual 182139 GO gates."""
import json
import statistics as st
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")


def load(name):
    p = LOGS / name
    rows = [json.loads(l) for l in p.read_text(encoding="utf-8").splitlines() if l.strip()]
    return [r for r in rows if r.get("kind") == "period"], [
        r for r in rows if r.get("kind") == "spike"
    ]


def fnum(r, k, d=0.0):
    try:
        return float(r.get(k) if r.get(k) is not None else d)
    except Exception:
        return d


def med(xs):
    xs = [x for x in xs if x is not None]
    return st.median(xs) if xs else None


def med_or(xs, default=999.0):
    """Median with default; do not use `med(xs) or default` (0.0 is valid)."""
    m = med(xs)
    return default if m is None else m


def mid_slice(periods):
    n = len(periods)
    a = max(1, int(0.12 * n))
    b = max(a + 1, int(0.88 * n))
    return periods[a:b]


def go_check(periods, label):
    mid = mid_slice(periods)
    stand = [
        r
        for r in periods
        if fnum(r, "focus_missing_mesh") == 0
        and fnum(r, "visual_holes") == 0
        and fnum(r, "unfinished_visual") <= 1
    ]
    pending = [fnum(r, "pending_gpu_applies_n") for r in mid]
    queued = [fnum(r, "pending_gpu_queued_n") for r in mid]
    kicked = [fnum(r, "pending_gpu_kicked_n") for r in mid]
    miss_frac = (
        sum(1 for r in mid if fnum(r, "focus_missing_mesh") > 0) / len(mid) if mid else 1
    )
    uv = [fnum(r, "unfinished_visual") for r in mid]
    stale = [fnum(r, "dark_face_stale_near_n") for r in mid]
    wall = [fnum(r, "wall_ms") for r in mid]
    share = [
        100 * fnum(r, "mesh_emerge_ms") / fnum(r, "wall_ms")
        for r in mid
        if fnum(r, "wall_ms") > 0.5
    ]
    stand_fog = (
        sum(
            1
            for r in stand
            if fnum(r, "focus_missing_mesh") == 0 and fnum(r, "fog_hole_debt") == 0
        )
        / max(1, sum(1 for r in stand if fnum(r, "focus_missing_mesh") == 0))
    )
    stand_op = [fnum(r, "opaque_cmd_on") for r in stand]
    witness_off = 0
    witness_n = 0
    for r in mid:
        if fnum(r, "focus_missing_mesh") > 0 and "miss_cx" in r:
            witness_n += 1
            if (int(r.get("miss_cx") or 0), int(r.get("miss_cz") or 0)) != (
                int(r.get("focus_cx") or 0),
                int(r.get("focus_cz") or 0),
            ):
                witness_off += 1

    print(f"\n=== GO {label} mid={len(mid)} stand={len(stand)} ===")
    checks = [
        ("pending_gpu med<=12", med_or(pending) <= 12, med(pending)),
        ("queued med<=8", med_or(queued) <= 8 if any(queued) else True, med(queued)),
        ("kicked med<=4", med_or(kicked) <= 4 if any(kicked) else True, med(kicked)),
        ("miss_frac<=0.5", miss_frac <= 0.5, round(miss_frac, 3)),
        ("unfinished med<=15", med_or(uv) <= 15, med(uv)),
        ("stale med<=60", med_or(stale) <= 60, med(stale)),
        ("emerge%<=30", med_or(share) <= 30, med(share)),
        ("wall med<=100", med_or(wall) <= 100, med(wall)),
        ("fog stand OK", stand_fog >= 0.95, round(stand_fog, 3)),
        (
            "opaque stand>=350",
            (med(stand_op) if med(stand_op) is not None else 0) >= 350 if stand else True,
            med(stand_op),
        ),
    ]
    ok = True
    for name, passed, val in checks:
        print(f"  {'PASS' if passed else 'FAIL'}  {name}: {val}")
        ok = ok and passed
    if witness_n:
        print(f"  info  witness!=focus: {witness_off}/{witness_n}")
    return ok


if __name__ == "__main__":
    import sys

    names = sys.argv[1:] or sorted(
        [p.name for p in LOGS.glob("perf_*.jsonl")],
        key=lambda n: (LOGS / n).stat().st_mtime,
        reverse=True,
    )[:1]
    for name in names:
        periods, _ = load(name)
        go_check(periods, name)
