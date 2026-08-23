#!/usr/bin/env python3
"""Compare two perf logs by segment (FZ2.6 delta vs baseline)."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path


def load(p: Path) -> list[dict]:
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs: list) -> float | None:
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def seg(rows: list[dict], t0: int, t1: int) -> list[dict]:
    return rows[t0 // 2 : t1 // 2]


def col(rows: list[dict], k: str) -> list:
    return [r.get(k) for r in rows]


def unit_apply(rows: list[dict], ms_key: str) -> float | None:
    units = []
    for r in rows:
        n = int(r.get("relight_apply_n") or 0)
        ms = float(r.get(ms_key) or 0)
        if n > 0 and ms > 0:
            units.append(ms / n)
    return med(units) if units else None


def summarize(label: str, rows: list[dict]) -> dict:
    steady = seg(rows, 120, 99999)
    enter = seg(rows, 0, 60)
    tail = rows[-15:] if len(rows) >= 15 else rows
    apply_n = [int(r.get("relight_apply_n") or 0) for r in steady]
    return {
        "label": label,
        "spikes": len(rows),
        "sim_steady": med(col(steady, "sim_ms")),
        "stream_steady": med(col(steady, "stream_ms")),
        "apply_n_steady": med(apply_n),
        "apply_util": (med(apply_n) or 0) / 20.0,
        "unit_apply_ms": unit_apply(steady, "relight_apply_ms"),
        "unit_light_ms": unit_apply(steady, "relight_apply_light_ms"),
        "unit_install_ms": unit_apply(steady, "relight_apply_install_ms"),
        "VB_steady": med(col(steady, "visible_black_focus_n")),
        "stalled_tail": max((r.get("visible_black_stalled_n") or 0) for r in tail)
        if tail
        else 0,
        "wall_enter_p90": sorted(float(x) for x in col(enter, "wall_ms") if x is not None)[
            min(len(enter) - 1, int(0.9 * max(1, len(enter) - 1)))
        ]
        if enter
        else None,
        "phase_over_pct": sum(int(r.get("phase_budget_over") or 0) for r in steady)
        / max(1, len(steady)),
        "opaque_refs": med(col(steady, "opaque_refs_cpu_vis")),
        "fifo": med(col(steady, "relight_fifo_n")),
    }


def fmt_delta(a: float | None, b: float | None) -> str:
    if a is None or b is None:
        return "n/a"
    if a == 0:
        return f"{b - a:+.1f}"
    return f"{b - a:+.1f} ({(b - a) / a * 100:+.1f}%)"


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: tmp_fz26_delta.py <baseline.jsonl> <current.jsonl>")
        return 2
    base = summarize("A", load(Path(sys.argv[1])))
    cur = summarize("B", load(Path(sys.argv[2])))
    print(f"=== FZ26 delta: {base['label']} -> {cur['label']} ===")
    keys = [
        "sim_steady",
        "stream_steady",
        "apply_n_steady",
        "apply_util",
        "unit_apply_ms",
        "unit_light_ms",
        "unit_install_ms",
        "VB_steady",
        "stalled_tail",
        "wall_enter_p90",
        "phase_over_pct",
        "opaque_refs",
        "fifo",
    ]
    for k in keys:
        a, b = base.get(k), cur.get(k)
        print(f"  {k}: {a} -> {b}  d={fmt_delta(a, b)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
