#!/usr/bin/env python3
"""Summarize cruise wall A1/A2 telemetry from perf_*.jsonl (stage A3)."""
from __future__ import annotations

import argparse
import json
import pathlib
import sys


def med_p90_max(vals):
  if not vals:
    return None
  vals = sorted(vals)
  n = len(vals)
  return vals[n // 2], vals[min(n - 1, int(n * 0.9))], vals[-1]


def main() -> int:
  ap = argparse.ArgumentParser()
  ap.add_argument("perf_jsonl", type=pathlib.Path)
  ap.add_argument("--min-chunks", type=int, default=100)
  args = ap.parse_args()
  rows = []
  with args.perf_jsonl.open(encoding="utf-8", errors="replace") as f:
    for line in f:
      line = line.strip()
      if not line:
        continue
      r = json.loads(line)
      if (r.get("chunk_count") or 0) > args.min_chunks:
        rows.append(r)
  if not rows:
    print("no warmup rows", file=sys.stderr)
    return 1

  keys = [
      "wall_ms",
      "world_streaming_phase_ms",
      "mesh_emerge_ms",
      "mesh_dirty_tick_ms",
      "mesh_dirty_prune_ms",
      "mesh_dirty_sort_ms",
      "mesh_dirty_drain_ms",
      "mesh_dirty_schedule_ms",
      "mesh_dirty_gpu_ms",
      "mesh_dirty_sync_ms",
      "mesh_emerge_prep_ms",
      "mesh_emerge_prep_unfinished_ms",
      "dirty_touch_n",
      "dirty_revisit_same_n",
      "mesh_dirty_schedule_ok_n",
      "mesh_dirty_schedule_skip_n",
      "prep_unfinished_calls_n",
      "prep_unfinished_full_n",
      "prep_unfinished_incremental_n",
      "phase_budget_over",
      "phase_miss_carve_out",
      "miss_reserved_ms",
      "emerge_budget_ms",
      "dirty_fm_n",
      "dirty_remesh_n",
      "render_preset",
      "async_meshing",
      "dirty",
      "visible_black_focus_n",
      "dark_face_void_near_n",
      "visual_holes",
      "pending_light_focus",
      "stream_pressure",
  ]
  print(f"file={args.perf_jsonl.name} n={len(rows)}")
  present = 0
  for k in keys:
    vals = [r[k] for r in rows if isinstance(r.get(k), (int, float))]
    if not vals:
      print(f"  {k}: MISSING")
      continue
    present += 1
    m, p90, mx = med_p90_max(vals)
    print(f"  {k}: med={m} p90={p90} max={mx}")

  # Substage share of mesh_dirty_tick
  shares = []
  for r in rows:
    tick = r.get("mesh_dirty_tick_ms") or 0.0
    if tick < 1.0:
      continue
    parts = [
        r.get("mesh_dirty_prune_ms") or 0.0,
        r.get("mesh_dirty_sort_ms") or 0.0,
        r.get("mesh_dirty_drain_ms") or 0.0,
        r.get("mesh_dirty_schedule_ms") or 0.0,
        r.get("mesh_dirty_gpu_ms") or 0.0,
        r.get("mesh_dirty_sync_ms") or 0.0,
    ]
    shares.append(sum(parts) / tick)
  if shares:
    shares.sort()
    print(
        f"  substages_sum/tick: med={shares[len(shares)//2]:.2f} "
        f"p90={shares[int(len(shares)*0.9)]:.2f} (expect ~1.0)"
    )
  else:
    print("  substages_sum/tick: n/a (no A1 fields or tick<1)")

  # Dominant substage by median ms
  sub = [
      "mesh_dirty_prune_ms",
      "mesh_dirty_sort_ms",
      "mesh_dirty_drain_ms",
      "mesh_dirty_schedule_ms",
      "mesh_dirty_gpu_ms",
      "mesh_dirty_sync_ms",
  ]
  meds = []
  for k in sub:
    vals = [r[k] for r in rows if isinstance(r.get(k), (int, float))]
    if vals:
      meds.append((sorted(vals)[len(vals) // 2], k))
  if meds:
    meds.sort(reverse=True)
    print("  dominant_dirty_substages:", ", ".join(f"{k}={m}" for m, k in meds[:3]))
  a1 = [
      "mesh_dirty_prune_ms",
      "mesh_dirty_sort_ms",
      "mesh_dirty_drain_ms",
      "mesh_dirty_schedule_ms",
      "mesh_dirty_gpu_ms",
      "dirty_revisit_same_n",
      "phase_budget_over",
      "render_preset",
  ]
  a1_ok = all(
      any(isinstance(r.get(k), (int, float)) for r in rows) for k in a1
  )
  if not a1_ok:
    print(
        "NOTE: A1/A2 fields missing — rebuild + new manual cruise required.",
        file=sys.stderr,
    )
    return 2
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
