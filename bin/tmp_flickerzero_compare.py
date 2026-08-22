#!/usr/bin/env python3
"""Compare FlickerZero flight vs baselines with gate pass/fail."""
import json
import statistics as st
from pathlib import Path

GATES = {
    "enter_no_ticket_med": ("visible_black_no_ticket_n", "enter", "med", 30, "lt"),
    "PL_enter_med": ("pending_light_focus", "enter", "med", 25, "lt"),
    "PL_steady_med": ("pending_light_focus", "steady", "med", 15, "lt"),
    "revisit_steady_med": ("dirty_revisit_same_n", "steady", "med", 95, "lt"),
    "uf_flips_rate": ("underfeet_opaque_present", "all", "flip_rate", 0.05, "lt"),
    "enter_wall_p90": ("wall_ms", "enter", "p90", 250, "lt"),
    "enter_fluid_p90": ("fluid_map_cpu_ms", "enter", "p90", 200, "lt"),
    "VB_steady_med": ("visible_black_focus_n", "steady", "med", 40, "lt"),
}


def load(p):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def p90(xs):
    xs = sorted(float(x) for x in xs if x is not None)
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))] if xs else None


def flip_rate(rows, key):
    xs = [r.get(key) for r in rows]
    flips = sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1])
    return flips / max(1, len(xs))


def seg(rows, end_s):
    return rows[: end_s // 2]


def col(rows, k):
    return [r.get(k) for r in rows]


def gate_value(rows, key, segment, stat):
    n = len(rows)
    if segment == "enter":
        part = seg(rows, 60)
    elif segment == "steady":
        part = rows[60:] if n > 90 else rows[n // 2 :]
    else:
        part = rows
    vals = col(part, key)
    if stat == "med":
        return med(vals)
    if stat == "p90":
        return p90(vals)
    if stat == "flip_rate":
        return flip_rate(part, key)
    return None


def check_gates(rows):
    results = []
    for name, (key, segment, stat, target, op) in GATES.items():
        val = gate_value(rows, key, segment, stat)
        if val is None:
            ok = None
        elif op == "lt":
            ok = val < target
        else:
            ok = val <= target
        results.append((name, val, target, ok))
    return results


files = {
    "FZ_164441": Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260822-164441_33596.jsonl"),
    "ColdPL_151946": Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260822-151946_3628.jsonl"),
    "ColdWall_093018": Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260822-093018_25864.jsonl"),
}

for name, p in files.items():
    u = load(p)
    n = len(u)
    print(f"=== {name} spikes={n} ~{n * 2}s commit=FZ if 164441 else prior ===")
    enter = seg(u, 60)
    steady = u[60:] if n > 90 else u[n // 2 :]
    for label, part in [("enter_0-60s", enter), ("steady_120s+", steady)]:
        if not part:
            continue
        print(
            f"  [{label}] wall={med(col(part, 'wall_ms')):.1f} "
            f"p90={p90(col(part, 'wall_ms')):.1f} "
            f"stream={med(col(part, 'stream_ms')):.1f} "
            f"PL={med(col(part, 'pending_light_focus')):.1f} "
            f"VB={med(col(part, 'visible_black_focus_n')):.1f} "
            f"no_ticket={med(col(part, 'visible_black_no_ticket_n')):.1f} "
            f"revisit={med(col(part, 'dirty_revisit_same_n')):.1f} "
            f"unlit_h={med(col(part, 'chunk_meshed_unlit_hidden')):.1f} "
            f"fluid_p90={p90(col(part, 'fluid_map_cpu_ms')):.1f}"
        )
    bs = sum(1 for r in u if r.get("black_sticky_blink"))
    mx_nt = max((r.get("visible_black_no_ticket_n") or 0) for r in u)
    mx_vb = max((r.get("visible_black_focus_n") or 0) for r in u)
    print(f"  black_sticky_blink={bs} no_ticket_peak={mx_nt} vb_peak={mx_vb}")
    uf_rate = flip_rate(u, "underfeet_opaque_present")
    print(f"  uf_flips rate={uf_rate:.3f}")
    print("  Gates (FZ2 targets):")
    for gname, val, target, ok in check_gates(u):
        if val is None:
            status = "n/a"
        elif ok:
            status = "PASS"
        else:
            status = "FAIL"
        val_s = f"{val:.2f}" if val is not None else "—"
        print(f"    {gname}: {val_s} (target<{target}) {status}")
    print()
