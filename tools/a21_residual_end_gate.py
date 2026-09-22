#!/usr/bin/env python3
"""A21 residual end-of-flight black / admit gate from perf JSONL periods."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def load_periods(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        row = json.loads(line)
        if row.get("kind") == "period":
            rows.append(row)
    return rows


def f(row: dict, key: str, default: float = 0.0) -> float:
    try:
        v = row.get(key)
        return float(v) if v is not None else default
    except (TypeError, ValueError):
        return default


def evaluate_end_gate(periods: list[dict], *, tail_n: int = 3) -> dict:
    if not periods:
        return {
            "end_gate_pass": False,
            "end_gate_fails": ["no_periods"],
            "tail": [],
        }
    tail = periods[-tail_n:]
    fails: list[str] = []
    rows = []
    for i, row in enumerate(tail):
        vb = f(row, "visible_black_focus_n")
        legal = f(row, "visible_black_legal_dark_n")
        debt = max(0.0, vb - legal)
        admit = f(row, "dirty_admit_budget_end")
        dropped = f(row, "dirty_dropped")
        fd = f(row, "fully_dark_census_n")
        kick = f(row, "mark_relit_prefer_kick_n")
        enqueue = f(row, "fm_dirty_enqueue_n")
        rows.append(
            {
                "period_index": len(periods) - len(tail) + i,
                "visible_black_focus_n": vb,
                "visible_black_legal_dark_n": legal,
                "debt_excl_legal": debt,
                "fully_dark_census_n": fd,
                "dirty_admit_budget_end": admit,
                "dirty_dropped": dropped,
                "mark_relit_prefer_kick_n": kick,
                "fm_dirty_enqueue_n": enqueue,
            }
        )
        if debt > 0:
            fails.append(f"tail[{i}]_debt_excl_legal={debt}")
        if fd > 0 and admit <= 0 and kick <= 0 and enqueue <= 0:
            fails.append(f"tail[{i}]_fd_without_admit_or_kick")
    # Focus-class drop growth without enqueue across tail.
    if len(rows) >= 2:
        d0 = rows[0]["dirty_dropped"]
        d1 = rows[-1]["dirty_dropped"]
        enq = sum(r["fm_dirty_enqueue_n"] for r in rows)
        if d1 > d0 and enq <= 0:
            fails.append("dirty_dropped_grew_without_enqueue")
    return {
        "end_gate_pass": len(fails) == 0,
        "end_gate_fails": fails,
        "tail_n": len(tail),
        "tail": rows,
    }


def dump_job_traces(path: Path, *, limit: int = 32) -> list[dict]:
    out: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        row = json.loads(line)
        if row.get("kind") != "job_trace":
            continue
        out.append(
            {
                "cx": row.get("cx"),
                "cy": row.get("cy"),
                "cz": row.get("cz"),
                "stage": row.get("stage"),
                "attempt_id": row.get("attempt_id"),
                "desired_rev": row.get("desired_rev"),
                "source_rev": row.get("source_rev"),
                "published_rev": row.get("published_rev"),
                "queue_reason": row.get("queue_reason"),
                "stage_ms": row.get("stage_ms"),
            }
        )
    return out[-limit:]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("perf_jsonl", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--label", default="residual")
    ap.add_argument("--tail-n", type=int, default=3)
    args = ap.parse_args()
    periods = load_periods(args.perf_jsonl)
    gate = evaluate_end_gate(periods, tail_n=args.tail_n)
    payload = {
        "label": args.label,
        "perf_jsonl": str(args.perf_jsonl),
        "periods": len(periods),
        "end_gate": gate,
        "job_trace_tail": dump_job_traces(args.perf_jsonl),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload, indent=2))
    return 0 if gate["end_gate_pass"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
