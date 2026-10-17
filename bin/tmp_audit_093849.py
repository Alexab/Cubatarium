#!/usr/bin/env python3
"""Audit manual 093849 (R7.2) vs 172957/131116/153045."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FLIGHTS = [
    ("093849_R72", "bin/logs/perf_20260910-093849_26976.jsonl"),
    ("172957_R71", "bin/logs/perf_20260909-172957_8616.jsonl"),
    ("131116_R6", "bin/logs/perf_20260909-131116_27772.jsonl"),
    ("153045_R7bad", "bin/logs/perf_20260909-153045_20028.jsonl"),
]


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


def pct(xs, p):
    if not xs:
        return None
    xs = sorted(xs)
    k = (len(xs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)


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


def summarize(tag, rows):
    cruise, stand = seg(rows)
    print(f"\n### {tag} n={len(rows)} cruise={len(cruise)} stand={len(stand)}")
    out = {}
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
        cull = series(rs, "scene_opaque_cull_ms")
        transp = series(rs, "scene_transparent_ms")
        meshing = series(rs, "column_meshing_n")
        unfin = series(rs, "unfinished_visual")
        discard = series(rs, "mesh_discarded_late")
        dirtyfm = series(rs, "dirty_fm_n")
        missh = series(rs, "miss_horiz")
        vb = series(rs, "visible_black_focus_n")
        cullskip = series(rs, "opaque_cull_skipped")
        spd = series(rs, "movement_speed")
        fifo = series(rs, "relight_fifo_n")
        kick = series(rs, "gpu_kick_n")
        focus = series(rs, "focus_missing_mesh")
        diffs = [abs(fpss[i] - fpss[i - 1]) for i in range(1, len(fpss))]
        spike160 = sum(1 for w in walls if w > 160) / len(walls) if walls else None
        spike200 = sum(1 for w in walls if w > 200) / len(walls) if walls else None
        print(
            f"  {name:6s} wall med/p90/max={med(walls):.1f}/{pct(walls,90):.1f}/{max(walls):.1f} "
            f"fps med/p10={med(fpss):.2f}/{pct(fpss,10):.2f} "
            f"|d| med/p90={med(diffs):.2f}/{pct(diffs,90):.2f} "
            f"spike>160={spike160:.1%} >200={spike200:.1%}"
        )
        print(
            f"         holes={hf:.3f} SoftDefer={med(stuck)} sched_ok={med(ok)} "
            f"dirty_fm={med(dirtyfm)} cull={med(cull):.1f} "
            f"transp={(med(transp) if transp else float('nan')):.1f}"
        )
        print(
            f"         stream/emerge/scene={med(stream):.1f}/{med(emerge):.1f}/{med(scene):.1f} "
            f"meshing={med(meshing)} unfin={med(unfin)} discard={med(discard)} "
            f"miss_h={med(missh)} vb={med(vb)} fifo={med(fifo)} kick={med(kick)} "
            f"spd={med(spd):.2f} cull_skip={med(cullskip)} focus_miss={med(focus)}"
        )
        if name == "cruise":
            out = {
                "wall": med(walls),
                "wall_p90": pct(walls, 90),
                "fps": med(fpss),
                "fps_p10": pct(fpss, 10),
                "d_med": med(diffs),
                "d_p90": pct(diffs, 90),
                "spike160": spike160,
                "holes": hf,
                "softdefer": med(stuck),
                "sched_ok": med(ok),
                "dirty_fm": med(dirtyfm),
                "cull": med(cull),
                "transp": med(transp),
                "stream": med(stream),
                "emerge": med(emerge),
                "scene": med(scene),
                "meshing": med(meshing),
                "unfin": med(unfin),
                "discard": med(discard),
                "miss_h": med(missh),
                "vb": med(vb),
                "fifo": med(fifo),
                "cull_skip": med(cullskip),
            }
    return out


def main():
    all_m = {}
    for tag, rel in FLIGHTS:
        all_m[tag] = summarize(tag, load(ROOT / rel))

    print("\n=== TABLE cruise spd>2 ===")
    keys = [
        "wall",
        "wall_p90",
        "fps",
        "fps_p10",
        "d_med",
        "d_p90",
        "spike160",
        "holes",
        "softdefer",
        "sched_ok",
        "dirty_fm",
        "cull",
        "transp",
        "stream",
        "emerge",
        "scene",
        "meshing",
        "unfin",
        "discard",
        "miss_h",
        "vb",
        "fifo",
        "cull_skip",
    ]
    header = f"{'metric':16s}" + "".join(f"{t:>14s}" for t in all_m)
    print(header)
    for k in keys:
        row = f"{k:16s}"
        for tag in all_m:
            v = all_m[tag].get(k)
            if v is None:
                row += f"{'None':>14s}"
            elif isinstance(v, float) and k in ("spike160", "holes"):
                row += f"{v:13.1%}"
            elif isinstance(v, float):
                row += f"{v:14.2f}"
            else:
                row += f"{v!s:>14s}"
        print(row)


if __name__ == "__main__":
    main()
