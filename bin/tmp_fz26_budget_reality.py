#!/usr/bin/env python3
"""Why algorithm budget expectations != measured wall time."""
from __future__ import annotations

import json
import statistics as st
from pathlib import Path

MISS_RESERVED_MS = 8.0
INFERRED_DRAIN_BUDGET = 20


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


def p90(xs: list) -> float | None:
    xs = sorted(float(x) for x in xs if x is not None)
    if not xs:
        return None
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))]


def seg(rows: list[dict], t0: int, t1: int) -> list[dict]:
    return rows[t0 // 2 : t1 // 2]


def analyze(label: str, path: Path) -> dict:
    u = load(path)
    steady = seg(u, 120, 99999)
    apply_n = [int(r.get("relight_apply_n") or 0) for r in steady]
    pos = [r for r in steady if int(r.get("relight_apply_n") or 0) > 0]
    zero = [r for r in steady if int(r.get("relight_apply_n") or 0) == 0]
    unit = [
        float(r.get("relight_apply_ms") or 0) / max(1, int(r.get("relight_apply_n") or 0))
        for r in pos
    ]
    wall_m = med([r.get("wall_ms") for r in steady]) or 1.0
    out = {
        "label": label,
        "spikes": len(u),
        "apply_n_med": med(apply_n),
        "apply_n_max": max(apply_n) if apply_n else 0,
        "apply_zero_pct": 100 * len(zero) / max(1, len(steady)),
        "unit_apply_ms": med(unit),
        "unit_apply_p90": p90(unit),
        "relight_drain_ms": med([r.get("relight_drain_ms") for r in steady]),
        "mesh_emerge_ms": med([r.get("mesh_emerge_ms") for r in steady]),
        "stream_ms": med([r.get("stream_ms") for r in steady]),
        "sim_ms": med([r.get("sim_ms") for r in steady]),
        "wall_ms": wall_m,
        "async_inflight": med([r.get("async_relight_inflight") for r in steady]),
        "fifo_n": med([r.get("relight_fifo_n") for r in steady]),
        "completed_when_zero": med([r.get("relight_completed_n") for r in zero]),
        "completed_when_pos": med([r.get("relight_completed_n") for r in pos]),
        "inflight_when_zero": med([r.get("async_relight_inflight") for r in zero]),
        "theoretical_apply_cap_slice": int(MISS_RESERVED_MS / max(0.1, med(unit) or 20)),
        "theoretical_apply_cap_budget": INFERRED_DRAIN_BUDGET,
        "actual_util_vs_budget": (med(apply_n) or 0) / INFERRED_DRAIN_BUDGET,
        "actual_util_vs_slice": (med(apply_n) or 0)
        / max(1, int(MISS_RESERVED_MS / max(0.1, med(unit) or 20))),
    }
    print(f"=== {label} ({path.name}) steady n={len(steady)} ===")
    print(f"  COUNT budget (inferred): {INFERRED_DRAIN_BUDGET}/frame")
    print(f"  TIME budget MissReservedMs: {MISS_RESERVED_MS}ms")
    print(f"  apply_n med={out['apply_n_med']} max={out['apply_n_max']} zero_frames={out['apply_zero_pct']:.0f}%")
    print(f"  unit_apply_ms med={out['unit_apply_ms']:.1f} p90={out['unit_apply_p90']:.1f}")
    print(
        f"  THEORETICAL max apply/frame by TIME: {out['theoretical_apply_cap_slice']} "
        f"(8ms/{out['unit_apply_ms']:.1f}ms bundled)"
    )
    print(
        f"  THEORETICAL max apply/frame by COUNT: {out['theoretical_apply_cap_budget']}"
    )
    print(
        f"  ACTUAL util vs count budget: {out['actual_util_vs_budget']:.1%} "
        f"vs slice budget: {out['actual_util_vs_slice']:.1%}"
    )
    print(f"  wall med={wall_m:.0f} sim={out['sim_ms']:.0f} stream={out['stream_ms']:.0f}")
    for k in ("relight_drain_ms", "mesh_emerge_ms", "stream_ms"):
        v = out[k]
        if v:
            print(f"    {k} {v:.0f}ms ({100*v/wall_m:.0f}% of wall)")
    print(f"  async_inflight med={out['async_inflight']} fifo med={out['fifo_n']}")
    print(
        f"  apply_n=0: completed med={out['completed_when_zero']} inflight={out['inflight_when_zero']}"
    )
    print(
        f"  apply_n>0: completed med={out['completed_when_pos']} "
        f"(queue starved when zero?)"
    )
    # If light were 3ms fantasy vs 20ms reality
    fantasy = int(MISS_RESERVED_MS / 3.0)
    print(f"  FZ2.5 FANTASY (3ms unit): would fit {fantasy} applies — ACTUAL unit makes 0-1")
    print()
    return out


def main() -> int:
    root = Path(__file__).resolve().parent / "logs"
    a = analyze("114401 FZ24", root / "perf_20260823-114401_15212.jsonl")
    b = analyze("125933 FZ25", root / "perf_20260823-125933_8084.jsonl")

    print("=== MISMATCH SUMMARY ===")
    print("1. Count budget (drain_budget=20) is NEVER the binding constraint.")
    print("2. Time budget (MissReservedMs=8) binds because bundled unit~20ms >> 8ms.")
    print("3. Async workers can be busy but main-thread Apply is serial + fat.")
    print("4. stream_ms~65 dominates wall; relight_drain~21 is only ~16% of wall.")
    print("5. FZ2.5 EarnedRelightApplyCap assumed wrong unit cost - no effect.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
