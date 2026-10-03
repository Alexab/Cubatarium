#!/usr/bin/env python3
"""I9-A: RefreshStreamingPressure sub-timer audit on steady cruise spikes."""
from __future__ import annotations

import argparse
import json
import statistics as st
from pathlib import Path

REFRESH_FIELDS = [
    ("prep_refresh_pressure_ms", "total"),
    ("prep_refresh_miss_ms", "miss"),
    ("prep_refresh_pending_ms", "pending"),
    ("prep_refresh_sticky_ms", "sticky"),
    ("prep_refresh_unfinished_ms", "unfinished"),
    ("prep_refresh_vb_ms", "vb"),
    ("prep_refresh_darkface_ms", "darkface"),
    ("prep_refresh_facing_ms", "facing"),
    ("prep_refresh_underfeet_ms", "underfeet_col"),
    ("prep_refresh_underfeet_probe_ms", "underfeet_probe"),
    ("prep_refresh_dirty_ms", "dirty"),
    ("prep_refresh_pressure_eval_ms", "eval"),
]


def load_spikes(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)
    return rows


def med(xs: list) -> float:
    vals = [float(x) for x in xs if x is not None]
    return st.median(vals) if vals else 0.0


def p95(xs: list) -> float:
    vals = sorted(float(x) for x in xs if x is not None)
    if not vals:
        return 0.0
    idx = int(0.95 * (len(vals) - 1))
    return vals[idx]


def audit_one(path: Path, label: str) -> None:
    rows = load_spikes(path)
    steady = rows[60:] if len(rows) > 120 else rows
    print(f"\n=== {label}: {path.name} spikes={len(rows)} steady={len(steady)} ===")
    totals = []
    parts_sum = []
    for key, name in REFRESH_FIELDS:
        vals = [r.get(key) for r in steady]
        m = med(vals)
        p = p95(vals)
        print(f"  {name:12s} med={m:7.2f}  p95={p:7.2f}")
        if key == "prep_refresh_pressure_ms":
            totals = [float(v or 0) for v in vals]
        elif key != "prep_refresh_pressure_ms":
            pass
    part_keys = [k for k, _ in REFRESH_FIELDS if k != "prep_refresh_pressure_ms"]
    parts_sum = [
        sum(float(r.get(k) or 0) for k in part_keys) for r in steady
    ]
    if totals and parts_sum:
        ratio = med(parts_sum) / max(med(totals), 0.001)
        untimed = [max(0.0, t - p) for t, p in zip(totals, parts_sum)]
        print(f"  subtimer_sum/total med ratio = {ratio:.2f}")
        print(f"  untimed med = {med(untimed):.2f}")
    for seg_name, seg_rows in [
        ("early", steady[: len(steady) // 3]),
        ("mid", steady[len(steady) // 3 : 2 * len(steady) // 3]),
        ("late", steady[2 * len(steady) // 3 :]),
    ]:
        if not seg_rows:
            continue
        print(
            f"  seg {seg_name}: prep={med([r.get('prep_refresh_pressure_ms') for r in seg_rows]):.1f} "
            f"stream={med([r.get('stream_ms') for r in seg_rows]):.1f}"
        )
    top = max(REFRESH_FIELDS[1:], key=lambda kv: med([r.get(kv[0]) for r in steady]))
    print(f"  top hotspot: {top[1]}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+", type=Path, help="perf jsonl files")
    ap.add_argument("--labels", nargs="*", default=[], help="optional labels")
    args = ap.parse_args()
    for i, path in enumerate(args.logs):
        label = args.labels[i] if i < len(args.labels) else path.stem
        audit_one(path, label)


if __name__ == "__main__":
    main()
