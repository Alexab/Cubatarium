#!/usr/bin/env python3
import json
import statistics as st
from collections import Counter
from pathlib import Path


def load_periods(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return [r for r in rows if r.get("kind") == "period"]


def med(vals):
    return st.median(vals) if vals else 0.0


def p90(vals):
    if not vals:
        return 0.0
    s = sorted(vals)
    return s[max(0, int(0.9 * len(s)) - 1)]


def summarize(name, periods):
    if not periods:
        print(f"=== {name} NO PERIODS ===\n")
        return None
    on = [r.get("opaque_cmd_on") or 0 for r in periods]
    c0 = [r.get("chunk_meshed_culled0") or 0 for r in periods]
    holes = [r.get("unfinished_visual") or 0 for r in periods]
    wall = [r.get("wall_ms") or 0 for r in periods]
    void = [r.get("dark_face_void_near_n") or 0 for r in periods]
    miss = [r.get("focus_missing_mesh") or 0 for r in periods]
    r0, r1 = periods[0], periods[-1]
    out = {
        "name": name,
        "periods": len(periods),
        "focus_start": (r0.get("focus_cx"), r0.get("focus_cz")),
        "focus_end": (r1.get("focus_cx"), r1.get("focus_cz")),
        "opaque_med": med(on),
        "opaque_min": min(on),
        "opaque_max": max(on),
        "blue": sum(1 for v in on if v == 0),
        "culled0_max": max(c0),
        "holes_med": med(holes),
        "holes_max": max(holes),
        "miss_pct": 100 * sum(1 for m in miss if m) / len(miss),
        "void_max": max(void),
        "wall_p50": med(wall),
        "wall_p90": p90(wall),
        "no_mesh_max": max(r.get("column_loaded_no_mesh_n") or 0 for r in periods),
        "not_ready_max": max(r.get("chunk_not_ready") or 0 for r in periods),
        "fifo_drop_last": periods[-1].get("relight_fifo_dropped"),
        "mesh_disc_last": periods[-1].get("mesh_completed_discarded"),
        "underfeet": Counter(r.get("underfeet_reason") for r in periods).most_common(4),
    }
    print(f"=== {name} periods={out['periods']} ===")
    print(f"  focus {out['focus_start']} -> {out['focus_end']}")
    print(
        f"  player ({r0.get('player_x'):.0f},{r0.get('player_y'):.0f},{r0.get('player_z'):.0f})"
        f" -> ({r1.get('player_x'):.0f},{r1.get('player_y'):.0f},{r1.get('player_z'):.0f})"
    )
    print(
        f"  opaque_on med/min/max {out['opaque_med']:.0f}/{out['opaque_min']:.0f}/{out['opaque_max']:.0f}"
        f"  blue={out['blue']}  culled0_max={out['culled0_max']:.0f}"
    )
    print(f"  holes med/max {out['holes_med']:.0f}/{out['holes_max']:.0f}  miss%={out['miss_pct']:.0f}%")
    print(f"  void_near max {out['void_max']}  wall p50/p90 {out['wall_p50']:.1f}/{out['wall_p90']:.1f}")
    print(f"  no_mesh max {out['no_mesh_max']}  not_ready max {out['not_ready_max']}")
    print(f"  fifo_drop last {out['fifo_drop_last']}  mesh_disc last {out['mesh_disc_last']}")
    print(f"  underfeet top {out['underfeet']}")
    by_cz = Counter(r.get("focus_cz") for r in periods)
    print(f"  cz range {min(by_cz)}..{max(by_cz)} unique cz {len(by_cz)}")
    for label, filt in [
        ("moving cz>=51", lambda r: (r.get("focus_cz") or 0) >= 51),
        ("linger cz=50", lambda r: (r.get("focus_cz") or 0) == 50),
        ("late cz>=55", lambda r: (r.get("focus_cz") or 0) >= 55),
    ]:
        seg = [r for r in periods if filt(r)]
        if not seg:
            continue
        mw = [r.get("wall_ms") or 0 for r in seg]
        mo = [r.get("opaque_cmd_on") or 0 for r in seg]
        mh = [r.get("unfinished_visual") or 0 for r in seg]
        print(
            f"  {label} n={len(seg)} wall p50/p90 {med(mw):.1f}/{p90(mw):.1f}"
            f" opaque med {med(mo):.0f} holes med {med(mh):.0f}"
        )
    print()
    return out


def main():
    logs = [
        ("land_manual_210623", Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-210623_30948.jsonl")),
        ("spawn_manual_205340", Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-205340_27764.jsonl")),
        ("autofly_land_200928", Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-200928_30372.jsonl")),
        ("autofly_pre_fix_195725", Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-195725_4816.jsonl")),
    ]
    # optional baseline if present
    base = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260817-083708_*.jsonl")
    bases = sorted(Path(r"E:/Work/Home/Cubatarium/bin/logs").glob("perf_*083708*.jsonl"))
    if bases:
        logs.append(("baseline_083708", bases[0]))
    results = []
    for name, p in logs:
        if not p.exists():
            print(f"=== {name} MISSING {p} ===\n")
            continue
        results.append(summarize(name, load_periods(p)))


if __name__ == "__main__":
    main()
