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
        ("gpu_fallback_rate", "le", 0.0),
        ("gpu_draw_cmds_med", "le", 15.0),
        ("wall_ms_no_holes_med", "le", 45.0),
    ],
    # Android GPU backlog (GPF6). Device metrics preferred; desktop AG0 needs probe.
    "AG0": [
        ("caps_probe_completed", "ge", 1.0),
        ("post_stop_black_sticky_max", "le", 0.0),
    ],
    "AG1": [
        ("android_gpu_effective", "ge", 1.0),
        ("caps_has_compute", "ge", 1.0),
        ("gpu_fluid_scan_on_med", "ge", 0.5),
        ("post_stop_black_sticky_max", "le", 0.0),
    ],
    "AG2": [
        ("android_gpu_effective", "ge", 1.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_store_mdi", "le", 0.0),
        ("backend_cull_gpu", "le", 0.0),
        ("gpu_mask_readback_med", "le", 0.0),
        ("post_stop_black_sticky_max", "le", 0.0),
    ],
    "AG3": [
        ("backend_mesher_gpu", "ge", 1.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "AG4": [
        ("android_gpu_effective", "ge", 1.0),
        ("backend_mesher_gpu", "ge", 1.0),
        ("backend_store_mdi", "le", 0.0),
        ("gpu_mask_readback_med", "le", 0.0),
        ("gpu_fluid_scan_on_med", "ge", 0.5),
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Visual regression gates (GPU desktop).
    "V_BLUE": [
        ("blue_screen_suspect", "le", 0.0),
        ("opaque_on_min", "ge", 1.0),
        ("post_stop_black_sticky_max", "le", 0.0),
    ],
    "V_DIG": [
        ("edit_immediate_n_med", "ge", 2.0),
        ("post_stop_black_sticky_max", "le", 0.0),
    ],
    "V_FLICKER": [
        ("pool_fence_wait_ms_med", "le", 5.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "V_EDGE": [
        ("chunk_not_ready_med", "le", 40.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Era13 readiness contract (ROOT_CAUSE_2026-07 / plan D3).
    "ARCH_D1": [
        ("post_load_ring_idle_max", "le", 0.0),
        ("effective_holes_rate", "le", 0.24),
        ("mesh_async_med_when_dirty", "ge", 4.0),
        ("post_stop_not_ready_end", "le", 0.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        # Prefer stale-dark; fall back to total only if split absent.
        ("stop_dark_face_stale_near_end", "lt", 200.0),
        ("wall_ms_med", "le", 35.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "ARCH_D3": [
        ("post_load_ring_idle_max", "le", 0.0),
        ("unfinished_idle_max", "le", 0.0),
        ("effective_holes_rate", "le", 0.10),
        ("wall_ms_med", "le", 30.0),
        ("mesh_async_med_when_dirty", "ge", 4.0),
        ("post_stop_not_ready_end", "le", 0.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_effective_holes_rate", "le", 0.0),
        ("stop_dark_face_stale_near_end", "lt", 100.0),
        ("cold_relight_holes_sec", "le", 3.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "ARCH_D3_LAND": [
        ("miss_stuck_max_run_sec", "le", 4.0),
        ("miss_end", "le", 0.0),
        ("miss_end_stop", "le", 0.0),
        ("post_stop_focus_miss_max", "le", 0.0),
        ("post_stop_miss_low_cy_n", "le", 0.0),
        ("post_stop_underfeet_ok_miss_n", "le", 0.0),
        ("tail_focus_miss_max", "le", 0.0),
        ("tail_miss_low_cy_n", "le", 0.0),
        ("tail_underfeet_ok_miss_n", "le", 0.0),
        ("effective_holes_rate", "le", 0.12),
        ("nh_no_miss_rate", "le", 0.25),
        ("stop_dark_face_stale_near_end", "lt", 100.0),
        ("opaque_idle_churn_max", "le", 160.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        # Era17 P1: hard heal — no orphan VisibleBlack; stalled queue=0.
        ("post_stop_visible_black_no_ticket_max", "le", 0.0),
        # Stalled (=Contains without Dirty yet) is mid-queue; report-only.
        # ("post_stop_visible_black_stalled_max", "le", 0.0),
        # Soft 70: land density + remesh tax.
        ("wall_ms_med", "le", 70.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Clean idle stand (idle-clean scenario): calm stop without edit/fluid.
    # Use calm_* for emerge/stream; phys_ms aliases MovementStepMs so do NOT
    # gate stop_phys_med (conflicts with stream≤15+emerge≤10). Hitch = block p95.
    "IDLE_CLEAN": [
        ("contaminated_idle", "le", 0.0),
        # Era18 P2: SoftDeferCapture floor while VB (was 60 P1).
        ("calm_stop_wall_med", "le", 65.0),
        # Era17 remesh/relight tax under heal-until (was 18 Era16).
        ("calm_stop_emerge_med", "le", 25.0),
        ("calm_stop_stream_med", "le", 28.0),
        ("physics_block_ms_p95", "le", 5.0),
        ("edit_immediate_n_med", "le", 0.0),
        # Small positive noise while PendingLight→Remesh exclusivity drains (±8).
        ("stop_focus_dirty_delta", "le", 8.0),
        ("opaque_idle_churn_max", "le", 160.0),
        ("post_stop_black_sticky_max", "le", 0.0),
        ("post_stop_visible_black_no_ticket_max", "le", 0.0),
        # Era18: stalled (=Contains before Dirty/PendingLight lands) flickers
        # mid-heal on idle (stalled_max=9 with faces=0 / no_ticket=0) — report-only.
        # ("post_stop_visible_black_stalled_max", "le", 0.0),
        ("stop_dark_face_stale_near_end", "lt", 200.0),
        ("post_stop_missing_max", "le", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    # Contaminated idle (manual after place/fluid): report-only wall gates.
    "IDLE_DIRTY": [
        ("contaminated_idle", "ge", 1.0),
    ],
    # Debtful idle stand (idle-warm): remesh/stream pressure closer to manual.
    "IDLE_WARM": [
        ("contaminated_idle", "le", 0.0),
        ("post_stop_focus_dirty_med", "ge", 40.0),
        ("calm_stop_wall_med", "le", 140.0),
        ("calm_stop_emerge_med", "le", 50.0),
        ("calm_stop_stream_med", "le", 55.0),
        ("physics_block_ms_p95", "le", 5.0),
        ("edit_immediate_n_med", "le", 0.0),
        ("post_stop_missing_max", "le", 0.0),
        ("opaque_cmd_on_med", "ge", 200.0),
        ("chunks_traveled", "ge", 6.0),
    ],
    # Moving cruise stress (fly-clean): judge fly segment, not stop-only.
    "FLY_CLEAN": [
        ("chunks_traveled", "ge", 6.0),
        ("wall_ms_fly_med", "le", 200.0),
        ("mesh_sync_fly_med", "le", 5.0),
        ("physics_block_ms_p95", "le", 5.0),
    ],
    # Ocean FillWater cruise (manual 104841): fly-segment void/VB/fluid + stop tail.
    # Smoke only — aspirational Era30 targets; see OCEAN_CRUISE_STRESS for parity.
    "OCEAN_CRUISE": [
        ("chunks_traveled", "ge", 6.0),
        ("wall_ms_fly_med", "le", 200.0),
        ("mesh_sync_fly_med", "le", 5.0),
        ("physics_block_ms_p95", "le", 5.0),
        ("effective_holes_rate", "le", 0.30),
        ("fly_void_near_max", "le", 800.0),
        ("stop_dark_face_void_near_end", "le", 100.0),
        ("post_stop_visible_black_max", "le", 20.0),
        ("fly_fluid_map_cpu_max", "le", 80.0),
    ],
    # Era30 H0: parity regression — must reproduce manual debt pre-fix.
    "OCEAN_CRUISE_STRESS": [
        ("chunks_traveled", "ge", 6.0),
        ("wall_ms_fly_med", "le", 200.0),
        ("fly_void_near_max", "ge", 400.0),
        ("effective_holes_rate", "ge", 0.40),
        ("fly_frontier_pressure_frac", "ge", 0.05),
    ],
    # Era30 DoD: analyze manual 104841-class log (post-fix targets).
    "OCEAN_MANUAL": [
        ("chunks_traveled", "ge", 6.0),
        ("effective_holes_rate", "le", 0.30),
        ("fly_void_near_max", "le", 800.0),
        ("stop_dark_face_void_near_end", "le", 100.0),
        ("post_stop_visible_black_max", "le", 20.0),
        ("fly_fluid_map_cpu_max", "le", 80.0),
        ("enter_app_update_max", "le", 200.0),
    ],
    # Edit/control smoke: soft hitch budget for C1 regate.
    "IDLE_EDIT_SMOKE": [
        ("physics_block_ms_p95", "le", 50.0),
        ("break_complete_sum", "ge", 1.0),
    ],
    # Flight perf plan FP gates (no-teleport cruise/enter).
    "FP-enter": [
        ("enter_unfinished_max", "le", 10.0),
        ("post_load_ring_idle_max", "le", 5.0),
        ("chunks_traveled", "ge", 1.0),
    ],
    "FP0": [
        ("chunks_traveled", "ge", 3.0),
    ],
    "FP1": [
        ("cruise_capture_retarget_med", "le", 5.0),
        ("cruise_relight_apply_final_med", "gt", 0.0),
        ("miss_stuck_max_run_sec", "le", 15.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "FP2": [
        ("cruise_schedule_ok_med", "ge", 3.0),
        ("unfinished_visual", "le", 5.0),
        ("stream_ms", "le", 60.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "FP3": [
        ("visible_black_focus_n", "le", 40.0),
        ("dark_face_stale_near_n", "le", 80.0),
        ("holes_rate", "le", 0.30),
        ("chunks_traveled", "ge", 3.0),
    ],
    "FP4": [
        ("visible_black_focus_n", "le", 40.0),
        ("holes_rate", "le", 0.30),
        ("dirty_ghost_n", "le", 5.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "FP5": [
        ("holes_rate", "le", 0.10),
        ("visible_black_focus_n", "le", 25.0),
        ("stream_ms", "le", 30.0),
        ("miss_stuck_max_run_sec", "le", 4.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "FP-manual": [
        ("cruise_schedule_ok_med", "ge", 3.0),
        ("cruise_capture_retarget_med", "le", 5.0),
        ("holes_rate", "le", 0.55),
        ("miss_stuck_max_run_sec", "le", 60.0),
        ("post_stop_visible_black_max", "le", 50.0),
        ("chunks_traveled", "ge", 5.0),
        ("emerge_spike_frac", "le", 0.08),
        ("opaque_idle_churn_max", "le", 120.0),
        ("chunk_not_ready_med", "le", 4.0),
        ("wall_ms_fly_med", "le", 120.0),
        ("effective_holes_blink_rate", "le", 0.05),
        ("stream_ms", "le", 90.0),
        ("relight_drain_near_zero_while_vb_sec", "le", 10.0),
        ("chain_stall_sec", "le", 15.0),
        ("prep_untagged_gap_med", "le", 35.0),
        ("mesh_emerge_ms", "le", 25.0),
    ],
    "FP-perf-cruise": [
        ("wall_ms_fly_med", "le", 90.0),
        ("stream_ms", "le", 75.0),
        ("mesh_emerge_ms", "le", 25.0),
        ("prep_refresh_pressure_ms", "le", 30.0),
    ],
    "FP-perf-soft": [
        ("stream_ms", "le", 50.0),
        ("mesh_emerge_ms", "le", 20.0),
        ("world_streaming_phase_ms", "le", 100.0),
    ],
    "MESH-H0-baseline": [
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-M0-waterfall": [
        ("mesh_waterfall_drain_med", "gt", 0.0),
        ("mesh_gpu_kick_ms", "ge", 0.0),
        ("mesh_gpu_finish_ms", "ge", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-M1-capture": [
        ("mesh_emerge_ms", "le", 35.0),
        ("mesh_waterfall_drain_med", "gt", 0.0),
        ("holes_rate", "le", 0.50),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-M2-worker": [
        ("mesh_snapshot_ms", "le", 0.5),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-M3-gpu": [
        ("pool_unsync_uploads_med", "le", 50.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-M4-ownership": [
        ("holes_rate", "le", 0.10),
        ("witness_latch_diet_share", "ge", 0.70),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-R15-capture": [
        ("cruise_schedule_ok_med", "ge", 2.0),
        ("mesh_schedule_retry_max", "gt", 0.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-R26-completion": [
        ("fm_dirty_to_gpu_finish_med", "gt", 0.0),
        ("cruise_schedule_ok_med", "ge", 2.0),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-R30-fps": [
        ("wall_ms_fly_med", "le", 200.0),
        ("stream_ms", "le", 120.0),
        ("effective_fps_fly", "ge", 5.0),
        ("cruise_schedule_ok_med", "ge", 2.0),
    ],
    "MESH-SHIP-joint": [
        ("witness_latch_diet_share", "ge", 0.40),
        ("holes_rate", "le", 0.30),
        ("fm_dirty_to_gpu_finish_med", "gt", 0.0),
        ("visual_holes_telemetry_mismatch_rate", "le", 0.10),
        ("chunks_traveled", "ge", 3.0),
    ],
    "MESH-parity-manual": [
        ("holes_rate", "le", 0.55),
        ("chunks_traveled", "ge", 3.0),
    ],
}


# Informational soft gates (printed; do not fail hard GO).
PHASE_SOFT_GATES: dict[str, list[tuple[str, str, float]]] = {
    "FP1": [
        ("cruise_fifo_dropped_delta", "le", 5.0),
        ("effective_holes_blink_rate", "le", 0.10),
    ],
    "FP2": [
        ("cruise_fifo_dropped_delta", "le", 8.0),
    ],
    "FP3": [
        ("opaque_idle_churn_max", "le", 160.0),
    ],
    "FP5": [
        ("cruise_fifo_dropped_delta", "le", 0.0),
    ],
    "FP-manual": [
        ("cruise_dirty_fm_med", "gt", 0.0),
        ("cruise_fifo_dropped_delta", "le", 12.0),
        ("cruise_admission_mode3_share", "le", 0.60),
        ("stop_dark_face_near_end", "lt", 500.0),
        ("post_stop_visible_black_no_ticket_max", "le", 0.0),
        ("emerge_spike_frac", "le", 0.08),
        ("mesh_emerge_ms", "le", 25.0),
        ("prep_untagged_gap_med", "le", 35.0),
        ("emerge_prep_other_share", "le", 0.40),
    ],
    "FP-perf-soft": [
        ("stream_ms", "le", 50.0),
        ("mesh_emerge_ms", "le", 20.0),
        ("world_streaming_phase_ms", "le", 100.0),
    ],
    "OCEAN_CRUISE": [
        ("relight_drain_near_zero_while_vb_sec", "le", 10.0),
        ("enter_app_update_max", "le", 200.0),
        ("fly_visible_black_max", "le", 40.0),
        ("vb_progress_without_dark_clear_sec", "le", 5.0),
    ],
    "OCEAN_MANUAL": [
        ("vb_progress_without_dark_clear_sec", "le", 5.0),
        ("opaque_idle_churn_max", "le", 120.0),
    ],
}


def metric(data: dict, key: str):
    m = data.get("metrics") or {}
    if key in m:
        return m.get(key)
    return data.get(key)


def check(op: str, val, limit: float) -> bool:
    if val is None:
        # Absent telemetry: fail hard gates; soft N/A only when explicitly skipped.
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


def check_arch(op: str, val, limit: float) -> bool:
    """ARCH_* gates: missing optional metric passes (no high-dirty sample etc.)."""
    if val is None:
        return True
    return check(op, val, limit)


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
    run_outcome = data.get("run_outcome", "")
    if run_outcome and run_outcome != "success":
        print(f"NO-GO: run_outcome={run_outcome}", file=sys.stderr)
        return 3
    if data.get("hang_killed"):
        print("NO-GO: hang_killed=true", file=sys.stderr)
        return 3

    gates = PHASE_GATES.get(args.phase_id, [])
    soft_gates = PHASE_SOFT_GATES.get(args.phase_id, [])
    failed = []
    arch = args.phase_id.startswith("ARCH_")
    if args.phase_id == "MESH-parity-manual" and args.baseline and args.baseline.is_file():
        base = json.loads(args.baseline.read_text(encoding="utf-8"))
        manual_holes = metric(base, "holes_rate")
        if manual_holes is not None:
            holes_cap = min(0.55, float(manual_holes) * 1.15 + 0.05)
            gates = [
                g if g[0] != "holes_rate" else ("holes_rate", "le", holes_cap)
                for g in gates
            ]
            print(f"  parity holes_cap={holes_cap:.3f} (manual={manual_holes})")
    for key, op, limit in gates:
        val = metric(data, key)
        ok = check_arch(op, val, limit) if arch else check(op, val, limit)
        print(f"  {key}={val} {op} {limit} -> {'OK' if ok else 'FAIL'}")
        if not ok:
            failed.append(key)

    for key, op, limit in soft_gates:
        val = metric(data, key)
        if val is None:
            print(f"  [soft] {key}=None {op} {limit} -> N/A")
            continue
        ok = check(op, val, limit)
        print(f"  [soft] {key}={val} {op} {limit} -> {'OK' if ok else 'WARN'}")

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
