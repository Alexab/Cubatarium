#!/usr/bin/env python3
"""Post-PASS metric validity audit (FZ2.6)."""
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


def blink_rate(rows: list[dict], key: str) -> float:
    xs = [r.get(key) for r in rows]
    return sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1]) / max(1, len(xs))


def audit(path: Path, manual_path: Path | None) -> tuple[list[str], list[str]]:
    u = load(path)
    steady = seg(u, 120, 99999)
    fails: list[str] = []
    hints: list[str] = []

    apply_n = med([r.get("relight_apply_n") for r in steady]) or 0
    fifo_start = steady[0].get("relight_fifo_n") if steady else None
    fifo_end = steady[-1].get("relight_fifo_n") if steady else None
    if apply_n >= 2 and fifo_start is not None and fifo_end is not None:
        if fifo_end > fifo_start + 5:
            fails.append(
                "A1: apply_n>=2 but fifo grew — add fifo_delta_steady gate"
            )

    raw_present = any("visible_black_focus_raw_n" in r for r in steady)
    if raw_present:
        diffs = []
        for r in steady:
            pub = r.get("visible_black_focus_n")
            raw = r.get("visible_black_focus_raw_n")
            if pub is not None and raw is not None:
                diffs.append(abs(int(raw) - int(pub)))
        if diffs and med(diffs) > 2:
            fails.append("A2: VB hysteresis hides debt — gate on raw_n")

    pl = med([r.get("pending_light_focus") for r in steady])
    vb_blink = blink_rate(steady, "visible_black_focus_n")
    if pl is not None and pl < 15 and vb_blink > 0.15:
        fails.append("A3: PL PASS but vb_blink high — visual debt masked")

    sim = med([r.get("sim_ms") for r in steady])
    phase_pct = sum(int(r.get("phase_budget_over") or 0) for r in steady) / max(
        1, len(steady)
    )
    if sim is not None and sim < 135 and phase_pct > 0.5:
        fails.append("A5: sim PASS but phase_budget_over>50% — movement clamp")

    if manual_path and manual_path.is_file():
        m = load(manual_path)
        m_steady = seg(m, 120, 99999)
        for key in ("relight_apply_n", "stream_ms", "visible_black_focus_n"):
            c = med([r.get(key) for r in steady])
            b = med([r.get(key) for r in m_steady])
            if c is not None and b is not None and b != 0:
                if abs(c - b) / b > 0.15:
                    fails.append(
                        f"A6: {key} diverges >15% vs manual baseline"
                    )

    if not fails:
        hints.append("All audit checks PASS")
    return fails, hints


def main() -> int:
    perf = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    manual = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    if perf is None or not perf.is_file():
        print("usage: tmp_fz26_metric_audit.py <perf.jsonl> [manual_baseline.jsonl]")
        return 2
    fails, hints = audit(perf, manual)
    print(f"=== FZ26 metric audit: {perf.name} ===")
    for f in fails:
        print(f"  FAIL: {f}")
    for h in hints:
        print(f"  {h}")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
