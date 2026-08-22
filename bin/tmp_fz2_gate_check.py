#!/usr/bin/env python3
"""FZ2 gate check for a perf log vs plan targets."""
import json
import statistics as st
import sys
from pathlib import Path

GATES = {
    "enter_no_ticket_med": ("visible_black_no_ticket_n", "enter", "med", 30),
    "PL_enter_med": ("pending_light_focus", "enter", "med", 25),
    "PL_steady_med": ("pending_light_focus", "steady", "med", 15),
    "revisit_steady_med": ("dirty_revisit_same_n", "steady", "med", 95),
    "revisit_enter_med": ("dirty_revisit_same_n", "enter", "med", 65),
    "uf_flips_rate": ("underfeet_opaque_present", "all", "flip_rate", 0.05),
    "enter_wall_p90": ("wall_ms", "enter", "p90", 250),
    "enter_fluid_p90": ("fluid_map_cpu_ms", "enter", "p90", 200),
    "VB_steady_med": ("visible_black_focus_n", "steady", "med", 40),
    "unlit_h_steady_med": ("chunk_meshed_unlit_hidden", "steady", "med", 20),
    "no_ticket_peak": ("visible_black_no_ticket_n", "all", "max", 80),
    "black_sticky": ("black_sticky_blink", "all", "sum", 0),
    "stream_steady_med": ("stream_ms", "steady", "med", 35),
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
    return sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1]) / max(1, len(xs))


def seg(rows, end_s):
    return rows[: end_s // 2]


def col(rows, k):
    return [r.get(k) for r in rows]


def gate_val(rows, key, segment, stat):
    n = len(rows)
    if segment == "enter":
        part = seg(rows, 60)
    elif segment == "steady":
        part = rows[60:] if n > 90 else rows[n // 2 :]
    else:
        part = rows
    if stat == "med":
        return med(col(part, key))
    if stat == "p90":
        return p90(col(part, key))
    if stat == "flip_rate":
        return flip_rate(part if segment != "all" else rows, key)
    if stat == "max":
        return max((r.get(key) or 0) for r in rows)
    if stat == "sum":
        return sum(1 for r in rows if r.get(key))
    return None


def analyze(name, path):
    u = load(path)
    n = len(u)
    enter = seg(u, 60)
    steady = u[60:] if n > 90 else u[n // 2 :]
    first15 = u[:8]
    print(f"=== {name} spikes={n} ~{n * 2}s file={path.name} ===")
    for label, part in [("enter", enter), ("steady", steady)]:
        print(
            f"  [{label}] wall_med={med(col(part, 'wall_ms')):.1f} "
            f"wall_p90={p90(col(part, 'wall_ms')):.1f} "
            f"stream={med(col(part, 'stream_ms')):.1f} "
            f"PL={med(col(part, 'pending_light_focus')):.1f} "
            f"VB={med(col(part, 'visible_black_focus_n')):.1f} "
            f"no_ticket={med(col(part, 'visible_black_no_ticket_n')):.1f} "
            f"revisit={med(col(part, 'dirty_revisit_same_n')):.1f} "
            f"unlit_h={med(col(part, 'chunk_meshed_unlit_hidden')):.1f} "
            f"fluid_p90={p90(col(part, 'fluid_map_cpu_ms')):.1f}"
        )
    mx_nt = max((r.get("visible_black_no_ticket_n") or 0) for r in u)
    bs = sum(1 for r in u if r.get("black_sticky_blink"))
    uf = flip_rate(u, "underfeet_opaque_present")
    has_pred = any("underfeet_opaque_present_predicted" in r for r in u)
    uf_pred = flip_rate(u, "underfeet_opaque_present_predicted") if has_pred else None
    nt15 = [r.get("visible_black_no_ticket_n") or 0 for r in first15]
    slope = (nt15[0] - nt15[-1]) / (len(nt15) - 1) if len(nt15) > 1 else 0.0
    print(
        f"  no_ticket_peak={mx_nt} black_sticky={bs} uf_rate={uf:.3f} "
        f"uf_pred_rate={uf_pred} first15_nt={nt15} slope={slope:.1f}/frame"
    )
    print("  Gates:")
    for gname, (key, segment, stat, target) in GATES.items():
        val = gate_val(u, key, segment, stat)
        if stat == "sum":
            ok = val == target
        else:
            ok = val is not None and val < target
        status = "PASS" if ok else "FAIL"
        val_s = f"{val:.2f}" if isinstance(val, float) else str(val)
        print(f"    {gname}: {val_s} (target<{target}) {status}")
    print()


def main():
    root = Path(__file__).resolve().parent / "logs"
    files = sys.argv[1:] or [
        "perf_20260822-173621_33656.jsonl",
        "perf_20260822-164441_33596.jsonl",
        "perf_20260822-151946_3628.jsonl",
    ]
    labels = ["FZ2_173621", "FZ1_164441", "ColdPL_151946"]
    for i, f in enumerate(files):
        label = labels[i] if i < len(labels) else f
        analyze(label, root / Path(f).name)


if __name__ == "__main__":
    main()
