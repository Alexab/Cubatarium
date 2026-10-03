#!/usr/bin/env python3
"""Mesh waterfall audit — decompose mesh_emerge_ms vs stage sum."""

from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

STAGES = [
    "mesh_snapshot_ms",
    "mesh_dirty_schedule_ms",
    "mesh_dirty_drain_ms",
    "mesh_dirty_gpu_ms",
    "mesh_gpu_kick_ms",
    "mesh_gpu_finish_ms",
    "mesh_async_drain_ms",
    "mesh_immediate_ms",
]


def load_periods(path: Path) -> list[dict]:
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs: list) -> float:
    vals = [float(x) for x in xs if x is not None and float(x) >= 0]
    return round(st.median(vals), 3) if vals else 0.0


def audit(path: Path) -> dict:
    rows = load_periods(path)
    fly = []
    for i, r in enumerate(rows):
        if i == 0:
            continue
        if (r.get("player_x"), r.get("player_z")) != (
            rows[i - 1].get("player_x"),
            rows[i - 1].get("player_z"),
        ):
            fly.append(r)
    out: dict = {"path": str(path), "fly_periods": len(fly)}
    stage_meds = {k: med([r.get(k) for r in fly]) for k in STAGES}
    out["stage_med"] = stage_meds
    stage_sum = sum(stage_meds.values())
    emerge_med = med([r.get("mesh_emerge_ms") for r in fly])
    out["mesh_emerge_ms_med"] = emerge_med
    out["stage_sum_med"] = round(stage_sum, 3)
    if emerge_med > 0:
        out["sum_vs_emerge_ratio"] = round(stage_sum / emerge_med, 3)
    dominant = max(stage_meds.items(), key=lambda kv: kv[1])
    out["dominant_stage"] = dominant[0]
    out["dominant_stage_med"] = dominant[1]
    return out


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: mesh_waterfall_audit.py perf.jsonl ...", file=sys.stderr)
        return 2
    for p in sys.argv[1:]:
        r = audit(Path(p))
        print(json.dumps(r, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
