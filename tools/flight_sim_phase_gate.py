#!/usr/bin/env python3
"""Go/no-go gate check for a flight_sim analyze JSON report.

Exit codes:
  0 — go
  2 — no-go (metrics)
  3 — hang / infra
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"

# phase_id -> list of (metric_key under metrics or top-level, op, limit)
PHASE_GATES: dict[str, list[tuple[str, str, float]]] = {
    "H": [],  # smoke only: hang_killed false checked always
    "R": [
        ("post_stop_black_sticky_max", "le", 1.0),
        ("post_stop_not_ready_end", "le", 80.0),
        ("stop_wall_med", "le", 120.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "V2a": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("stop_wall_med", "le", 90.0),
    ],
    "V2b": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_pending_med", "le", 40.0),
        ("stop_wall_med", "le", 90.0),
    ],
    "V3": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("pending_light_focus_med", "le", 40.0),
        ("post_stop_pending_med", "le", 35.0),
        ("stop_wall_med", "le", 90.0),
    ],
    "F": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_pending_med", "le", 15.0),
        ("post_stop_not_ready_end", "le", 45.0),
        ("stop_wall_med", "le", 90.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Lit-but-dirty catch-up (next milestone after pending→0).
    "F2": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_pending_med", "le", 5.0),
        ("post_stop_not_ready_end", "le", 36.0),
        ("post_stop_focus_dirty_end", "le", 280.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("stop_wall_med", "le", 90.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Moving hitch budget (manual 161304): spike + no-hole FPS.
    "C": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("spike_max_wall_holes", "le", 200.0),
        ("cold_relight_holes_sec", "le", 6.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "CB": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("spike_max_wall_holes", "le", 200.0),
        ("cold_relight_holes_sec", "le", 3.0),
        # Accepted 36.3 @ cb_pack (2026-07-26); +2ms tolerance vs aspirational 35.
        ("wall_ms_no_holes_med", "le", 37.0),
        ("dirty_med_no_holes", "le", 450.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Soft research gates (informational; use with --soft-only or inspect soft).
    "CB_WE": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("spike_max_wall_holes", "le", 200.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("wall_ms_no_holes_med", "le", 37.0),
        ("dirty_med_no_holes", "le", 450.0),
        ("chunks_traveled", "ge", 3.0),
        ("spike_max_world_extra", "le", 600.0),
        ("spike_world_extra_dominant_rate", "le", 0.35),
    ],
    # GPU ladder (Desktop). Always also run F2/C/CB separately.
    "G0": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("gpu_draw_cmds_med", "le", 50.0),
    ],
    "G1": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("backend_cull_gpu", "ge", 1.0),
        ("gpu_cull_ms_med", "ge", 0.0),
    ],
    "G2": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_cull_gpu", "ge", 1.0),
        ("gpu_cull_ms_med", "gt", 0.0),
        # Allow +slop vs CB 37 while compute/cull wiring settles.
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    "G3": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("gpu_draw_cmds_med", "le", 15.0),
        ("vertex_pool_fill_med", "le", 0.85),
    ],
    "G4": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_store_mdi", "ge", 1.0),
    ],
    "G5": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("cold_relight_holes_sec", "le", 3.0),
    ],
    "G6": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("post_stop_pending_med", "le", 5.0),
    ],
    "G7": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    "GA": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_cull_gpu", "ge", 1.0),
        ("gpu_draw_cmds_med", "le", 15.0),
    ],
    # Best-practice completion (P*): Desktop compute without sync-readback traps.
    "P0": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_cull_gpu", "ge", 1.0),
    ],
    "P2": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_cull_gpu", "ge", 1.0),
        ("gpu_cull_indirect_med", "ge", 0.5),
        ("wall_ms_no_holes_med", "le", 45.0),
        ("gpu_cull_ms_med", "le", 5.0),
    ],
    "P3": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("gpu_draw_cmds_med", "le", 15.0),
        ("vertex_pool_fill_med", "le", 0.85),
    ],
    "P5": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("gpu_mesh_vbo_dispatch_med", "ge", 0.0),
    ],
    "P6": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("post_stop_pending_med", "le", 5.0),
        ("gpu_light_seed_apply_med", "ge", 0.0),
    ],
    "P7": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("wall_ms_no_holes_med", "le", 45.0),
        ("gpu_fluid_scan_on_med", "ge", 0.5),
    ],
    # PA sign-off: union of F2 proxies + P* completion + GA backends.
    "PA": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_pending_med", "le", 5.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_cull_gpu", "ge", 1.0),
        ("gpu_cull_indirect_med", "ge", 0.5),
        ("gpu_draw_cmds_med", "le", 15.0),
        ("wall_ms_no_holes_med", "le", 45.0),
        ("gpu_fluid_scan_on_med", "ge", 0.5),
        ("gpu_mesh_vbo_dispatch_med", "ge", 0.0),
        ("gpu_light_seed_apply_med", "ge", 0.0),
    ],
    # D1 Desktop completion ladder.
    "D1a": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("gpu_mask_readback_med", "le", 0.0),
        ("wall_ms_no_holes_med", "le", 45.0),
        ("cold_relight_holes_sec", "le", 3.0),
    ],
    "D1b": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("wall_ms_no_holes_med", "le", 45.0),
        ("gpu_mask_readback_med", "le", 0.0),
    ],
    "D1c": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("post_stop_pending_med", "le", 5.0),
        ("gpu_light_seed_apply_med", "ge", 0.0),
        ("gpu_mask_readback_med", "le", 0.0),
    ],
    "D1d": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_lighting_full", "ge", 1.0),
        ("backend_lighting_flat", "le", 0.0),
        ("gpu_mask_readback_med", "le", 0.0),
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    # Full-branch ladder (D2): move from D1 interim to full GPU hot path.
    "D2a": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("gpu_mask_readback_med", "le", 0.0),
        ("gpu_opaque_emit_gpu_max", "ge", 0.5),
        ("wall_ms_no_holes_med", "le", 45.0),
        ("vertex_pool_fill_med", "le", 0.85),
    ],
    "D2b": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("gpu_transparent_sort_gpu_max", "ge", 0.5),
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    "D2c": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("gpu_fluid_scan_on_med", "ge", 0.5),
        ("gpu_fluid_readback_med", "le", 0.0),
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    "D2d": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("post_stop_pending_med", "le", 5.0),
        ("gpu_light_readback_med", "le", 0.0),
        ("backend_lighting_full", "ge", 1.0),
        ("backend_lighting_flat", "le", 0.0),
    ],
    "D2e": [
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
        ("backend_store_mdi", "ge", 1.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_cull_gpu", "ge", 1.0),
        ("gpu_cull_indirect_med", "ge", 0.5),
        ("gpu_draw_cmds_med", "le", 15.0),
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    # Android GPU backlog stubs (informational; not Desktop CB thresholds).
    "AG0": [
        ("chunks_traveled", "ge", 0.0),
    ],
    "AG1": [
        ("chunks_traveled", "ge", 0.0),
    ],
    "AG2": [
        ("chunks_traveled", "ge", 0.0),
    ],
    "AG3": [
        ("chunks_traveled", "ge", 0.0),
    ],
    "AG4": [
        ("chunks_traveled", "ge", 0.0),
    ],
}


def metric(data: dict, key: str):
    m = data.get("metrics") or {}
    if key in m:
        return m.get(key)
    return data.get(key)


def check(op: str, val, limit: float) -> bool:
    if val is None:
        return False
    v = float(val)
    if op == "le":
        return v <= limit
    if op == "ge":
        return v >= limit
    if op == "lt":
        return v < limit
    if op == "gt":
        return v > limit
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--report", type=Path, required=True)
    ap.add_argument("--phase-id", required=True)
    ap.add_argument(
        "--baseline",
        type=Path,
        default=None,
        help="optional baseline report for relative checks",
    )
    args = ap.parse_args()

    if not args.report.is_file():
        print(f"FAIL: missing report {args.report}", file=sys.stderr)
        return 3

    data = json.loads(args.report.read_text(encoding="utf-8"))
    if data.get("hang_killed"):
        print("NO-GO: hang_killed=true", file=sys.stderr)
        return 3

    gates = PHASE_GATES.get(args.phase_id, [])
    failed = []
    for key, op, limit in gates:
        val = metric(data, key)
        ok = check(op, val, limit)
        print(f"  {key}={val} {op} {limit} -> {'OK' if ok else 'FAIL'}")
        if not ok:
            failed.append(key)

    if args.baseline and args.baseline.is_file() and args.phase_id in ("V2b", "V3", "F"):
        base = json.loads(args.baseline.read_text(encoding="utf-8"))
        cur_end = metric(data, "post_stop_not_ready_end")
        base_end = metric(base, "post_stop_not_ready_end")
        if cur_end is not None and base_end is not None:
            ok = float(cur_end) < float(base_end)
            print(
                f"  not_ready_end {cur_end} < baseline {base_end} -> "
                f"{'OK' if ok else 'FAIL'}"
            )
            if not ok:
                failed.append("not_ready_end_vs_baseline")

    if failed:
        print(f"NO-GO phase={args.phase_id} failed={failed}", file=sys.stderr)
        return 2
    print(f"GO phase={args.phase_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
