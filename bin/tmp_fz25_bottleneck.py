#!/usr/bin/env python3
"""FZ2.5 Perf-0: pipeline bottleneck forensics — stage latency + apply_util."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

# Spike field → pipeline stage (see FZ2.5 plan)
STAGE_FIELDS = {
    "compute": ["mesh_async_n"],
    "capture": ["relight_capture_finalize", "capture_bg_cap_n"],
    "apply": ["relight_apply_n", "relight_apply_ms", "relight_drain_ms"],
    "install_prep": ["mark_relit_schedule_n", "mark_relit_suppress_enter_settled_n"],
    "mesh_schedule": ["stream_ms", "dirty_revisit_same_n", "mesh_async_n"],
    "vb_scan": ["visible_black_focus_n", "visible_black_stalled_n"],
}

INFERRED_DRAIN_BUDGET = 20  # idle plateau boost cap from TickAsyncChunkSystems


def load_spikes(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
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


def col(rows: list[dict], k: str) -> list:
    return [r.get(k) for r in rows]


def seg_by_time(rows: list[dict], t0: int, t1: int) -> list[dict]:
    return rows[t0 // 2 : t1 // 2]


def unit_apply_ms(rows: list[dict]) -> float | None:
    pairs = [
        (float(r.get("relight_apply_ms") or 0), int(r.get("relight_apply_n") or 0))
        for r in rows
        if r.get("relight_apply_n") is not None
    ]
    units = [ms / max(1, n) for ms, n in pairs if n > 0]
    return med(units) if units else None


def apply_util(rows: list[dict], budget: int = INFERRED_DRAIN_BUDGET) -> float | None:
    apply_n = [int(r.get("relight_apply_n") or 0) for r in rows]
    if not apply_n:
        return None
    return med(apply_n) / budget if budget > 0 else None


def vb_ticketed_med(rows: list[dict]) -> float | None:
    vals = []
    for r in rows:
        vb = r.get("visible_black_focus_n")
        nt = r.get("visible_black_no_ticket_n")
        if vb is not None and nt is not None:
            vals.append(float(vb) - float(nt))
    return med(vals) if vals else None


def stage_p90(rows: list[dict], stage: str) -> dict[str, float | None]:
    out = {}
    for field in STAGE_FIELDS.get(stage, []):
        out[field] = p90(col(rows, field))
    return out


def rank_stages(rows: list[dict]) -> list[tuple[str, float]]:
    """Rough bottleneck rank: dominant p90 latency proxy per stage."""
    scores: dict[str, float] = {}
    scores["apply"] = p90(col(rows, "relight_apply_ms")) or 0.0
    scores["mesh_schedule"] = p90(col(rows, "stream_ms")) or 0.0
    scores["capture"] = p90(col(rows, "relight_drain_ms")) or 0.0
    mr = col(rows, "mark_relit_schedule_n")
    scores["install_prep"] = 0.0 if med(mr) == 0 else 8.0  # starved when med≈0
    return sorted(scores.items(), key=lambda x: -x[1])


def analyze_segment(label: str, rows: list[dict]) -> None:
    if len(rows) < 3:
        print(f"  [{label}] skip n={len(rows)}")
        return
    util = apply_util(rows)
    uam = unit_apply_ms(rows)
    vbt = vb_ticketed_med(rows)
    apply_n_med = med(col(rows, "relight_apply_n"))
    stream_med = med(col(rows, "stream_ms"))
    revisit_med = med(col(rows, "dirty_revisit_same_n"))
    pl_med = med(col(rows, "pending_light_focus"))
    vb_med = med(col(rows, "visible_black_focus_n"))
    nt_med = med(col(rows, "visible_black_no_ticket_n"))
    stalled_max = max((r.get("visible_black_stalled_n") or 0) for r in rows)
    mr_med = med(col(rows, "mark_relit_schedule_n"))
    print(f"  [{label}] n={len(rows)}")
    print(
        f"    apply_n_med={apply_n_med:.1f} apply_util={util:.3f} "
        f"unit_apply_ms={uam:.2f} stream_med={stream_med:.1f} "
        f"revisit_med={revisit_med:.1f}"
    )
    print(
        f"    PL={pl_med:.1f} VB={vb_med:.1f} no_ticket={nt_med:.1f} "
        f"vb_ticketed_med={vbt:.1f} stalled_max={stalled_max} "
        f"mark_relit_sched={mr_med:.1f}"
    )
    ranks = rank_stages(rows)
    print(f"    bottleneck_rank: {', '.join(f'{s}={v:.1f}' for s, v in ranks[:3])}")


def main() -> int:
    root = Path(__file__).resolve().parent / "logs"
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "perf_20260823-114401_15212.jsonl"
    if not path.is_file():
        print(f"missing: {path}", file=sys.stderr)
        return 2
    u = load_spikes(path)
    n = len(u)
    print(f"=== FZ2.5 Perf-0 bottleneck: {path.name} spikes={n} ~{n * 2}s ===")
    print(f"  inferred_drain_budget={INFERRED_DRAIN_BUDGET}")
    print()
    print("Hypothesis check (114401 baseline):")
    util_steady = apply_util(seg_by_time(u, 120, 99999))
    apply_steady = med(col(seg_by_time(u, 120, 99999), "relight_apply_n"))
    print(f"  B1 apply_util_steady={util_steady:.3f} (expect ~0.05) — count/time misaligned")
    print(f"  B1 relight_apply_n_steady={apply_steady:.1f} (expect 1)")
    mr_steady = med(col(seg_by_time(u, 120, 99999), "mark_relit_schedule_n"))
    print(f"  B2 mark_relit_schedule_steady={mr_steady:.1f} (expect ~0 — install starved)")
    stream_steady = med(col(seg_by_time(u, 120, 99999), "stream_ms"))
    print(f"  B4 stream_steady={stream_steady:.1f} (expect ~63 — mesh_schedule tax)")
    vbt = vb_ticketed_med(seg_by_time(u, 120, 99999))
    nt_steady = med(col(seg_by_time(u, 120, 99999), "visible_black_no_ticket_n"))
    print(f"  B5 vb_ticketed_steady={vbt:.1f} no_ticket={nt_steady:.1f} (all ticketed debt)")
    print()
    for label, t0, t1 in [
        ("enter_0_60", 0, 60),
        ("mid_60_120", 60, 120),
        ("steady_120_plus", 120, 99999),
        ("tail30", max(0, n * 2 - 60), n * 2),
    ]:
        analyze_segment(label, seg_by_time(u, t0, t1))
    tail = u[-15:] if len(u) >= 15 else u
    stalled_tail = max((r.get("visible_black_stalled_n") or 0) for r in tail)
    print(f"\n  stalled_tail_max (last ~30s): {stalled_tail}")
    print()
    print("Stage field map:")
    for stage, fields in STAGE_FIELDS.items():
        print(f"  {stage}: {', '.join(fields)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
