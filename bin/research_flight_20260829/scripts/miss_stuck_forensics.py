#!/usr/bin/env python3
"""Timeline forensics for miss stuck slices (MissCx/Cz, schedule, pin triggers)."""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "bin/logs/perf_20260830-100455_33852.jsonl"


def load(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("kind") == "spike":
            rows.append(row)
    return rows


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    rows = [r for r in load(path) if float(r.get("player_y") or 0) > 10]
    print(f"=== Miss stuck forensics: {path.name} n={len(rows)} ===\n")

    stuck_run = 0
    stuck_max = 0
    completion_run = 0
    completion_max = 0
    stuck_slices: list[tuple[int, dict]] = []
    completion_slices: list[tuple[int, dict]] = []
    for i, r in enumerate(rows):
        miss = float(r.get("focus_missing_mesh") or 0) > 0
        sched = float(r.get("mesh_dirty_schedule_ok_n") or 0)
        completion = float(r.get("miss_completion_stuck_frames") or 0) > 0
        if miss and sched == 0:
            stuck_run += 1
            stuck_max = max(stuck_max, stuck_run)
        else:
            if stuck_run >= 60:
                stuck_slices.append((stuck_run, rows[i - 1]))
            stuck_run = 0
        if completion:
            completion_run += 1
            completion_max = max(completion_max, completion_run)
        else:
            if completion_run >= 60:
                completion_slices.append((completion_run, rows[i - 1]))
            completion_run = 0
    if stuck_run >= 60:
        stuck_slices.append((stuck_run, rows[-1]))
    if completion_run >= 60:
        completion_slices.append((completion_run, rows[-1]))

    print(f"miss_stuck_max_run_frames: {stuck_max} (~{stuck_max / 60:.1f}s)")
    print(
        f"miss_completion_stuck_max_run_frames: {completion_max} "
        f"(~{completion_max / 60:.1f}s)"
    )
    print(f"stuck slices schedule_ok=0 (>=60f): {len(stuck_slices)}")
    print(f"stuck slices completion (>=60f): {len(completion_slices)}\n")

    for run_len, r in stuck_slices[:4]:
        print(
            f"[schedule=0] run={run_len}f miss=({r.get('miss_cx')},{r.get('miss_cz')}) "
            f"nh={r.get('miss_horiz')} schedule_ok={r.get('mesh_dirty_schedule_ok_n')} "
            f"dirty_fm={r.get('dirty_fm_n')} miss_age={r.get('miss_witness_age_frames')} "
            f"sla_kick={r.get('miss_sla_kick_n')}"
        )
    for run_len, r in completion_slices[:4]:
        print(
            f"[completion] run={run_len}f miss=({r.get('miss_cx')},{r.get('miss_cz')}) "
            f"nh={r.get('miss_horiz')} schedule_ok={r.get('mesh_dirty_schedule_ok_n')} "
            f"gpu_finish={r.get('gpu_finish_n')} mark_relit={r.get('mark_relit_schedule_n')} "
            f"clnm={r.get('column_loaded_no_mesh_n')} "
            f"completion_stuck={r.get('miss_completion_stuck_frames')}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
