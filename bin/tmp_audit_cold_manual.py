#!/usr/bin/env python3
"""Compare newest ColdApply manual vs 123613 / 104125."""
from __future__ import annotations

import json
import statistics
from collections import Counter
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")


def load(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    if not xs:
        return None
    return statistics.median(xs)


def p90(xs):
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return None
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def flips(xs):
    n = 0
    prev = None
    for v in xs:
        if prev is not None and v != prev:
            n += 1
        prev = v
    return n


def fmt(v, nd=1):
    if v is None:
        return "-"
    if isinstance(v, float):
        if nd == 0:
            return f"{v:.0f}"
        return f"{v:.{nd}f}"
    return str(v)


def analyze(label: str, path: Path):
    if not path.exists():
        print(f"MISSING {label} {path}")
        return None
    rows = load(path)
    spikes = [r for r in rows if r.get("kind") == "spike"]
    periods = [r for r in rows if r.get("kind") == "period"]
    use = spikes if spikes else periods
    n = len(use)
    dur = n * 2.0
    c0 = float(use[0].get("chunk_count") or 0)
    c1 = float(use[-1].get("chunk_count") or 0)
    fill = (c1 - c0) / max(1.0, dur)

    def col(k):
        out = []
        for r in use:
            if k not in r:
                continue
            try:
                out.append(float(r[k]))
            except (TypeError, ValueError):
                pass
        return out

    pl = col("pending_light_focus")
    fifo = col("relight_fifo_n")
    fifo_drop = col("relight_fifo_dropped")
    apply_n = col("relight_apply_n")
    apply_ms = col("relight_apply_ms")
    emerge = col("mesh_emerge_ms")
    wall = col("wall_ms")
    stream = col("stream_ms")
    bs = col("black_sticky")
    fpd = col("focus_pending_dark")
    fdm = col("focus_dark_mesh")
    vb = col("visible_black_focus_n")
    unlit = col("chunk_meshed_unlit")
    uf = col("underfeet_opaque_present")
    cap_ms = col("relight_capture_ms")
    cap_full = col("relight_capture_full_n")
    dirty = col("dirty_n")
    unfinished = col("unfinished_visual")
    vbs = col("visible_black_stalled_n")
    cap_over = col("capture_over_budget")
    softdefer = col("softdefer_capture_budget")
    floor_hits = col("softdefer_capture_floor_hits")
    completed = col("relight_completed_n")
    completed_cap = col("relight_completed_cap")
    apply_partial = col("relight_apply_partial_n")
    apply_final = col("relight_apply_final_n")
    band = col("relight_capture_band_cy_span")
    col_horiz = col("relight_capture_col_horiz")

    bs_pct = (
        100.0 * sum(1 for v in bs if v > 0.5) / max(1, len(bs)) if bs else None
    )
    cap_active = [r for r in use if float(r.get("relight_capture_ms") or 0) > 0.01]
    cap_partial = sum(
        1 for r in cap_active if int(r.get("relight_capture_finalize") or 0) == 0
    )
    cap_final = sum(
        1 for r in cap_active if int(r.get("relight_capture_finalize") or 0) == 1
    )
    uf_i = [int(v) for v in uf]
    uf_fl = flips(uf_i)
    ufp = [int(r.get("underfeet_pending_light") or 0) for r in use]
    mid = max(1, n // 2)

    out = {
        "label": label,
        "file": path.name,
        "n": n,
        "dur": dur,
        "fill": fill,
        "c0": c0,
        "c1": c1,
        "pl_med": med(pl),
        "pl_p90": p90(pl),
        "pl_max": max(pl) if pl else None,
        "pl_end": pl[-1] if pl else None,
        "pl_early": med(pl[:mid]),
        "pl_late": med(pl[mid:]),
        "fifo_med": med(fifo),
        "fifo_p90": p90(fifo),
        "fifo_max": max(fifo) if fifo else None,
        "fifo_drop_delta": (fifo_drop[-1] - fifo_drop[0]) if fifo_drop else None,
        "apply_n_med": med(apply_n),
        "apply_n_p90": p90(apply_n),
        "apply_n_max": max(apply_n) if apply_n else None,
        "apply_n_early": med(apply_n[:mid]),
        "apply_n_late": med(apply_n[mid:]),
        "apply_ms_med": med(apply_ms),
        "apply_ms_p90": p90(apply_ms),
        "emerge_med": med(emerge),
        "emerge_p90": p90(emerge),
        "wall_med": med(wall),
        "wall_p90": p90(wall),
        "wall_early": med(wall[:mid]),
        "wall_late": med(wall[mid:]),
        "stream_med": med(stream),
        "fpd_med": med(fpd),
        "fpd_p90": p90(fpd),
        "fpd_max": max(fpd) if fpd else None,
        "fdm_med": med(fdm),
        "unfin_med": med(unfinished),
        "unfin_max": max(unfinished) if unfinished else None,
        "bs_med": med(bs),
        "bs_max": max(bs) if bs else None,
        "bs_pct": bs_pct,
        "vb_med": med(vb),
        "vb_max": max(vb) if vb else None,
        "vbs_med": med(vbs),
        "unlit_med": med(unlit),
        "dirty_med": med(dirty),
        "uf_flips": uf_fl,
        "uf_rate": uf_fl / max(1, n),
        "uf_med": med(uf),
        "ufp_pct": 100.0 * sum(ufp) / max(1, len(ufp)),
        "cap_ms_med": med(cap_ms),
        "cap_active": len(cap_active),
        "cap_final": cap_final,
        "cap_partial": cap_partial,
        "cap_partial_rate": (cap_partial / len(cap_active)) if cap_active else None,
        "cap_over_sum": sum(cap_over) if cap_over else 0,
        "cap_full_med": med(cap_full),
        "completed_med": med(completed),
        "completed_cap_med": med(completed_cap),
        "apply_partial_med": med(apply_partial),
        "apply_final_med": med(apply_final),
        "softdefer_med": med(softdefer),
        "floor_delta": (floor_hits[-1] - floor_hits[0]) if floor_hits else None,
        "band_med": med(band),
        "col_horiz_med": med(col_horiz),
        "uf_reason": dict(
            sorted(Counter(int(r.get("underfeet_reason") or -1) for r in use).items())
        ),
    }

    print(f"=== {label} ({path.name}) ===")
    print(f"  spikes={len(spikes)} periods={len(periods)} ~{dur:.0f}s rows_use={n}")
    print(f"  chunks {c0:.0f}->{c1:.0f} (+{c1 - c0:.0f}, {fill:.2f}/s)")
    print(
        f"  PL med/p90/max/end={fmt(out['pl_med'],0)}/{fmt(out['pl_p90'],0)}/"
        f"{fmt(out['pl_max'],0)}/{fmt(out['pl_end'],0)} "
        f"early/late={fmt(out['pl_early'],0)}/{fmt(out['pl_late'],0)}"
    )
    print(
        f"  fifo_n med/p90/max={fmt(out['fifo_med'],0)}/{fmt(out['fifo_p90'],0)}/"
        f"{fmt(out['fifo_max'],0)} drop_delta={fmt(out['fifo_drop_delta'],0)}"
    )
    print(
        f"  apply_n med/p90/max={fmt(out['apply_n_med'],1)}/{fmt(out['apply_n_p90'],1)}/"
        f"{fmt(out['apply_n_max'],1)} early/late={fmt(out['apply_n_early'],1)}/"
        f"{fmt(out['apply_n_late'],1)}"
    )
    print(
        f"  apply_ms med/p90={fmt(out['apply_ms_med'],2)}/{fmt(out['apply_ms_p90'],2)}"
    )
    print(
        f"  emerge_ms med/p90={fmt(out['emerge_med'],1)}/{fmt(out['emerge_p90'],1)}"
    )
    print(
        f"  wall_ms med/p90={fmt(out['wall_med'],1)}/{fmt(out['wall_p90'],1)} "
        f"early/late={fmt(out['wall_early'],1)}/{fmt(out['wall_late'],1)} "
        f"stream_med={fmt(out['stream_med'],1)}"
    )
    print(
        f"  focus_pending_dark med/p90/max={fmt(out['fpd_med'],0)}/"
        f"{fmt(out['fpd_p90'],0)}/{fmt(out['fpd_max'],0)}"
    )
    print(
        f"  focus_dark_mesh med={fmt(out['fdm_med'],0)} unfinished med/max="
        f"{fmt(out['unfin_med'],0)}/{fmt(out['unfin_max'],0)}"
    )
    print(
        f"  black_sticky med/max BS%={fmt(out['bs_med'],0)}/{fmt(out['bs_max'],0)} "
        f"{fmt(out['bs_pct'],1)}%"
    )
    print(
        f"  VB med/max stalled_med={fmt(out['vb_med'],0)}/{fmt(out['vb_max'],0)} "
        f"{fmt(out['vbs_med'],0)}"
    )
    print(f"  unlit med={fmt(out['unlit_med'],0)} dirty med={fmt(out['dirty_med'],0)}")
    print(
        f"  uf_flips={uf_fl}/{n} ({out['uf_rate']:.3f}/row) present_med="
        f"{fmt(out['uf_med'],0)} ufp_on%={out['ufp_pct']:.0f}"
    )
    print(
        f"  capture_ms med={fmt(out['cap_ms_med'],2)} active={out['cap_active']} "
        f"final={cap_final} partial={cap_partial} "
        f"partial_rate={fmt(out['cap_partial_rate'],3)} "
        f"over_budget_sum={out['cap_over_sum']}"
    )
    print(
        f"  capture_full_n med={fmt(out['cap_full_med'],1)} completed_n med="
        f"{fmt(out['completed_med'],1)} completed_cap med="
        f"{fmt(out['completed_cap_med'],0)}"
    )
    print(
        f"  apply_partial/final med={fmt(out['apply_partial_med'],1)}/"
        f"{fmt(out['apply_final_med'],1)}"
    )
    print(
        f"  softdefer_budget med={fmt(out['softdefer_med'],0)} "
        f"floor_hits_delta={out['floor_delta']} "
        f"band_cy_span med={fmt(out['band_med'],0)} "
        f"col_horiz med={fmt(out['col_horiz_med'],0)}"
    )
    print(f"  underfeet_reason {out['uf_reason']}")
    print()
    return out


def timeline(path: Path, step: int = 10):
    rows = load(path)
    spikes = [r for r in rows if r.get("kind") == "spike"]
    print(f"TIMELINE {path.name} every {step}th spike:")
    print("  idx PL fifo an am emerge wall fpd bs uf vb chunks dirty")
    for i in range(0, len(spikes), step):
        r = spikes[i]
        print(
            f"  {i:3d} PL={r.get('pending_light_focus')} "
            f"fifo={r.get('relight_fifo_n')} "
            f"an={r.get('relight_apply_n')} "
            f"am={float(r.get('relight_apply_ms') or 0):.1f} "
            f"em={float(r.get('mesh_emerge_ms') or 0):.0f} "
            f"w={float(r.get('wall_ms') or 0):.0f} "
            f"fpd={r.get('focus_pending_dark')} "
            f"bs={r.get('black_sticky')} "
            f"uf={r.get('underfeet_opaque_present')} "
            f"vb={r.get('visible_black_focus_n')} "
            f"ch={r.get('chunk_count')} "
            f"d={r.get('dirty_n')}"
        )
    print()


def main():
    p104 = list(LOGS.glob("perf_20260821-104125*.jsonl"))
    print("104125:", [p.name for p in p104] or "NOT FOUND")
    # also list nearby 10xx manuals
    for p in sorted(LOGS.glob("perf_20260821-10*.jsonl")):
        print("  10xx", p.name, p.stat().st_size)
    print()

    targets = [
        ("NEW manual 133345", LOGS / "perf_20260821-133345_9472.jsonl"),
        ("worse baseline 123613", LOGS / "perf_20260821-123613_28584.jsonl"),
    ]
    if p104:
        targets.append(("pre-Flicker 104125", p104[0]))

    results = []
    for label, path in targets:
        r = analyze(label, path)
        if r:
            results.append(r)

    timeline(LOGS / "perf_20260821-133345_9472.jsonl", step=8)
    timeline(LOGS / "perf_20260821-123613_28584.jsonl", step=12)

    # compact comparison table
    if len(results) >= 2:
        keys = [
            ("dur", "dur_s", 0),
            ("fill", "fill_ch/s", 2),
            ("pl_med", "PL_med", 0),
            ("pl_p90", "PL_p90", 0),
            ("pl_late", "PL_late", 0),
            ("fifo_med", "fifo_med", 0),
            ("fifo_drop_delta", "fifo_drop_d", 0),
            ("apply_n_med", "apply_n", 1),
            ("apply_ms_med", "apply_ms", 2),
            ("emerge_med", "emerge_ms", 1),
            ("wall_med", "wall_ms", 1),
            ("wall_p90", "wall_p90", 1),
            ("stream_med", "stream_ms", 1),
            ("fpd_med", "fpd_med", 0),
            ("bs_pct", "BS%", 1),
            ("vb_med", "VB_med", 0),
            ("unfin_med", "unfin", 0),
            ("uf_rate", "uf_flip/row", 3),
            ("ufp_pct", "uf_pend%", 0),
            ("cap_ms_med", "cap_ms", 2),
            ("cap_partial_rate", "cap_partial", 3),
            ("cap_over_sum", "cap_over", 0),
            ("completed_cap_med", "compl_cap", 0),
            ("band_med", "band_cy", 0),
        ]
        header = f"{'metric':18}" + "".join(f"{r['label'][:18]:>20}" for r in results)
        print(header)
        print("-" * len(header))
        for key, name, nd in keys:
            row = f"{name:18}"
            for r in results:
                row += f"{fmt(r.get(key), nd):>20}"
            print(row)


if __name__ == "__main__":
    main()
