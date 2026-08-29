#!/usr/bin/env python3
"""Audit witness pin vs capture retarget on cruise spikes."""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "bin/logs/perf_20260829-142846_30244.jsonl"


def load(path: Path) -> list[dict]:
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines()
        if line.strip() and json.loads(line).get("kind") == "spike"
    ]


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    rows = [r for r in load(path) if float(r.get("player_y") or 0) > 10]
    print(f"=== Witness bypass audit: {path.name} cruise n={len(rows)} ===\n")
    pin_active = [r for r in rows if float(r.get("softdefer_capture_pin_age") or 0) > 0]
    retarget = [r for r in rows if float(r.get("softdefer_capture_retarget_n") or 0) > 0]
    both = [
        r
        for r in rows
        if float(r.get("softdefer_capture_pin_age") or 0) > 0
        and float(r.get("softdefer_capture_retarget_n") or 0) > 0
    ]
    print(f"pin_age>0: {len(pin_active)} ({100*len(pin_active)/max(1,len(rows)):.1f}%)")
    print(f"capture_retarget>0: {len(retarget)}")
    print(f"pin_age>0 AND capture_retarget>0: {len(both)} (witness bypass signal)")
    if both:
        ages = [float(r.get("softdefer_capture_pin_age") or 0) for r in both]
        rets = [float(r.get("softdefer_capture_retarget_n") or 0) for r in both]
        print(f"  pin_age med={st.median(ages):.1f} retarget med={st.median(rets):.1f}")
    blocked = sum(float(r.get("softdefer_capture_retarget_blocked_n") or 0) for r in rows)
    print(f"retarget_blocked_n sum={blocked:.0f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
