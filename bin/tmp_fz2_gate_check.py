#!/usr/bin/env python3
"""FZ2 gate check for a perf log vs plan targets (FZ2.5 segment gates)."""
import json
import statistics as st
import sys
from pathlib import Path

GATES = {
    "enter_no_ticket_med": ("visible_black_no_ticket_n", "enter_0_60", "med", 30),
    "PL_enter_med": ("pending_light_focus", "enter_0_60", "med", 25),
    "PL_mid_med": ("pending_light_focus", "mid_60_120", "med", 30),
    "PL_steady_med": ("pending_light_focus", "steady_120_plus", "med", 15),
    "revisit_steady_med": ("dirty_revisit_same_n", "steady_120_plus", "med", 95),
    "revisit_mid_med": ("dirty_revisit_same_n", "mid_60_120", "med", 120),
    "revisit_enter_med": ("dirty_revisit_same_n", "enter_0_60", "med", 65),
    "uf_flips_rate": ("underfeet_opaque_present", "all", "flip_rate", 0.05),
    "enter_wall_p90": ("wall_ms", "enter_0_60", "p90", 250),
    "enter_fluid_p90": ("fluid_map_cpu_ms", "enter_0_60", "p90", 200),
    "VB_steady_med": ("visible_black_focus_n", "steady_120_plus", "med", 40),
    "VB_mid_med": ("visible_black_focus_n", "mid_60_120", "med", 85),
    "unlit_h_steady_med": ("chunk_meshed_unlit_hidden", "steady_120_plus", "med", 20),
    "no_ticket_peak": ("visible_black_no_ticket_n", "all", "max", 80),
    "black_sticky": ("black_sticky_blink", "all", "sum", 0),
    "stream_steady_med": ("stream_ms", "steady_120_plus", "med", 35),
    "relight_apply_n_steady": ("relight_apply_n", "steady_120_plus", "med_min", 2),
    "apply_util_steady": ("relight_apply_n", "steady_120_plus", "apply_util", 0.15),
    "stalled_tail_max": ("visible_black_stalled_n", "tail30", "max", 10),
    # FZ2.6 gates
    "sim_steady_med": ("sim_ms", "steady_120_plus", "med", 135),
    "vb_blink_steady": ("visible_black_focus_n", "steady_120_plus", "flip_rate", 0.15),
    "phase_budget_over_pct": ("phase_budget_over", "steady_120_plus", "pct_true", 0.50),
    "unit_apply_light_ms": ("relight_apply_light_ms", "steady_120_plus", "unit_med", 5),
    "unit_apply_install_ms": ("relight_apply_install_ms", "steady_120_plus", "unit_med", 8),
    # FZ2.7 post-B2d: CountCap share (not FZ2.6 TimeSlice/FatUnit BRM).
    "apply_binding_countcap_pct": ("apply_binding", "steady_120_plus", "pct_countcap_min", 0.50),
    "opaque_refs_steady": ("opaque_refs_cpu_vis", "steady_120_plus", "med", 400),
    "mark_relit_steady": ("mark_relit_schedule_n", "steady_120_plus", "med_min", 0.5),
}

INFERRED_DRAIN_BUDGET = 20
APPLY_BINDING_TIME_SLICE = 1
APPLY_BINDING_COUNT_CAP = 2
APPLY_BINDING_FAT_UNIT = 3


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


def seg_by_time(rows, t0, t1):
    """Spike index i ≈ t=i*2 seconds."""
    return rows[t0 // 2 : t1 // 2]


def col(rows, k):
    return [r.get(k) for r in rows]


def vb_ticketed_med(rows):
    vals = []
    for r in rows:
        vb = r.get("visible_black_focus_n")
        nt = r.get("visible_black_no_ticket_n")
        if vb is not None and nt is not None:
            vals.append(float(vb) - float(nt))
    return med(vals) if vals else None


def apply_util(rows, budget=INFERRED_DRAIN_BUDGET):
    apply_n = [int(r.get("relight_apply_n") or 0) for r in rows]
    m = med(apply_n)
    return m / budget if m is not None and budget > 0 else None


def unit_apply_ms(rows):
    units = []
    for r in rows:
        n = int(r.get("relight_apply_n") or 0)
        ms = float(r.get("relight_apply_ms") or 0)
        if n > 0:
            units.append(ms / n)
    return med(units) if units else None


def unit_field_ms(rows, key):
    units = []
    for r in rows:
        n = int(r.get("relight_apply_n") or 0)
        ms = float(r.get(key) or 0)
        if n > 0 and ms > 0:
            units.append(ms / n)
    return med(units) if units else None


def pct_true(rows, key):
    if not rows:
        return None
    xs = [int(r.get(key) or 0) for r in rows]
    return sum(1 for x in xs if x) / len(xs)


def pct_binding_countcap(rows):
    """Share of CountCap among frames that applied at least one column."""
    xs = []
    for r in rows:
        if r.get("apply_binding") is None:
            continue
        if int(r.get("relight_apply_n") or 0) < 1:
            continue
        xs.append(int(r.get("apply_binding") or 0))
    if not xs:
        return None
    return sum(1 for x in xs if x == APPLY_BINDING_COUNT_CAP) / len(xs)


def honest_apply_util(rows):
    """apply_n / min(ready, budget) when ready is present; else None."""
    vals = []
    for r in rows:
        n = int(r.get("relight_apply_n") or 0)
        ready = r.get("relight_completed_n")
        if ready is None:
            ready = r.get("relight_ready_n")
        if ready is None:
            continue
        denom = max(1, min(int(ready) or 0, INFERRED_DRAIN_BUDGET))
        vals.append(n / denom)
    return med(vals) if vals else None


def pct_binding_time(rows):
    if not rows:
        return None
    xs = [int(r.get("apply_binding") or 0) for r in rows if r.get("apply_binding") is not None]
    if not xs:
        return None
    time_like = sum(
        1 for x in xs if x in (APPLY_BINDING_TIME_SLICE, APPLY_BINDING_FAT_UNIT)
    )
    return time_like / len(xs)


def gate_val(rows, key, segment, stat):
    n = len(rows)
    if segment == "enter_0_60":
        part = seg_by_time(rows, 0, 60)
    elif segment == "mid_60_120":
        part = seg_by_time(rows, 60, 120)
    elif segment == "steady_120_plus":
        part = seg_by_time(rows, 120, 99999)
    elif segment == "tail30":
        part = seg_by_time(rows, max(0, n * 2 - 60), n * 2)
    elif segment == "enter":
        part = seg_by_time(rows, 0, 60)
    elif segment == "steady":
        part = seg_by_time(rows, 120, 99999) if n > 90 else rows[n // 2 :]
    else:
        part = rows
    if stat == "med":
        return med(col(part, key))
    if stat == "med_min":
        return med(col(part, key))
    if stat == "p90":
        return p90(col(part, key))
    if stat == "flip_rate":
        return flip_rate(part if segment != "all" else rows, key)
    if stat == "max":
        return max((r.get(key) or 0) for r in part) if part else 0
    if stat == "sum":
        return sum(1 for r in rows if r.get(key))
    if stat == "apply_util":
        return apply_util(part)
    if stat == "unit_med":
        return unit_field_ms(part, key)
    if stat == "pct_true":
        return pct_true(part, key)
    if stat == "pct_binding_time":
        return pct_binding_time(part)
    if stat == "pct_binding_min":
        return pct_binding_time(part)
    if stat == "pct_countcap_min":
        return pct_binding_countcap(part)
    return None


def analyze(name, path):
    u = load(path)
    n = len(u)
    short_flight = n < 60
    enter_0_60 = seg_by_time(u, 0, 60)
    mid_60_120 = seg_by_time(u, 60, 120)
    steady_120_plus = seg_by_time(u, 120, 99999)
    print(
        f"=== {name} spikes={n} ~{n * 2}s file={path.name} "
        f"short_flight={short_flight} ==="
    )
    for label, part in [
        ("enter_0_60", enter_0_60),
        ("mid_60_120", mid_60_120),
        ("steady_120_plus", steady_120_plus),
    ]:
        if len(part) < 3:
            print(f"  [{label}] (skip: n={len(part)} < 3)")
            continue
        print(
            f"  [{label}] wall_med={med(col(part, 'wall_ms')):.1f} "
            f"wall_p90={p90(col(part, 'wall_ms')):.1f} "
            f"stream={med(col(part, 'stream_ms')):.1f} "
            f"PL={med(col(part, 'pending_light_focus')):.1f} "
            f"VB={med(col(part, 'visible_black_focus_n')):.1f} "
            f"no_ticket={med(col(part, 'visible_black_no_ticket_n')):.1f} "
            f"vb_ticketed={vb_ticketed_med(part):.1f} "
            f"apply_n={med(col(part, 'relight_apply_n')):.1f} "
            f"apply_util={apply_util(part):.3f} "
            f"unit_apply_ms={unit_apply_ms(part):.2f} "
            f"revisit={med(col(part, 'dirty_revisit_same_n')):.1f} "
            f"mark_relit={med(col(part, 'mark_relit_schedule_n')):.1f} "
            f"unlit_h={med(col(part, 'chunk_meshed_unlit_hidden')):.1f} "
            f"fluid_p90={p90(col(part, 'fluid_map_cpu_ms')):.1f} "
            f"countcap={pct_binding_countcap(part)} "
            f"honest_util={honest_apply_util(part)}"
        )
    first15 = u[:8]
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
    if short_flight:
        print("  SHORT_FLIGHT: skip steady_* gates (compare enter/mid only)")
    print("  Gates:")
    for gname, (key, segment, stat, target) in GATES.items():
        if short_flight and segment == "steady_120_plus":
            print(f"    {gname}: SHORT_FLIGHT skip steady")
            continue
        if segment == "steady_120_plus" and len(steady_120_plus) < 3:
            print(f"    {gname}: (skip: steady n={len(steady_120_plus)} < 3)")
            continue
        if segment == "mid_60_120" and len(mid_60_120) < 3:
            print(f"    {gname}: (skip: mid n={len(mid_60_120)} < 3)")
            continue
        if segment == "tail30" and n < 15:
            print(f"    {gname}: (skip: tail n={n} < 15)")
            continue
        val = gate_val(u, key, segment, stat)
        if val is None and stat in (
            "unit_med",
            "pct_true",
            "pct_binding_time",
            "pct_binding_min",
            "pct_countcap_min",
        ):
            print(f"    {gname}: (skip: field missing in log)")
            continue
        if stat == "sum":
            ok = val == target
        elif stat == "med_min":
            ok = val is not None and val >= target
        elif stat == "apply_util":
            ok = val is not None and val >= target
        elif stat in ("pct_true",):
            ok = val is not None and val < target
        elif stat == "pct_binding_min":
            ok = val is not None and val >= target
        elif stat == "pct_countcap_min":
            ok = val is not None and val >= target
        else:
            ok = val is not None and val < target
        status = "PASS" if ok else "FAIL"
        if stat == "med_min":
            tgt = f">={target}"
        elif stat == "apply_util":
            tgt = f">={target}"
        elif stat == "pct_binding_min":
            tgt = f">={target}"
        elif stat == "pct_countcap_min":
            tgt = f">={target}"
        elif stat in ("pct_true",):
            tgt = f"<{target}"
        else:
            tgt = f"<{target}"
        val_s = f"{val:.2f}" if isinstance(val, float) else str(val)
        print(f"    {gname}: {val_s} (target{tgt}) {status}")
    print()


def main():
    files = sys.argv[1:]
    if not files:
        root = Path(__file__).resolve().parent / "logs"
        files = [str(root / "perf_20260823-114401_15212.jsonl")]
    for i, f in enumerate(files):
        p = Path(f)
        analyze(p.stem, p)


if __name__ == "__main__":
    main()
