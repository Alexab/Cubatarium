#!/usr/bin/env python3
"""Analyze flight-sim perf_*.jsonl against streaming gate thresholds."""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path


def median(xs: list[float]) -> float | None:
    if not xs:
        return None
    return float(statistics.median(xs))


def max_val(xs: list[float]) -> float | None:
    if not xs:
        return None
    return float(max(xs))


def p95(xs: list[float]) -> float | None:
    if not xs:
        return None
    s = sorted(xs)
    if len(s) == 1:
        return float(s[0])
    idx = int(round(0.95 * (len(s) - 1)))
    return float(s[idx])


def binary_transition_stats(xs: list[float]) -> tuple[int, float, int, int]:
    """For 0/1-like samples: transitions, transition_rate, max1run, max0run."""
    if not xs:
        return 0, 0.0, 0, 0
    states = [1 if float(v) > 0.5 else 0 for v in xs]
    transitions = 0
    max_one_run = 0
    max_zero_run = 0
    cur_state = states[0]
    cur_run = 1
    for s in states[1:]:
        if s == cur_state:
            cur_run += 1
            continue
        transitions += 1
        if cur_state == 1:
            max_one_run = max(max_one_run, cur_run)
        else:
            max_zero_run = max(max_zero_run, cur_run)
        cur_state = s
        cur_run = 1
    if cur_state == 1:
        max_one_run = max(max_one_run, cur_run)
    else:
        max_zero_run = max(max_zero_run, cur_run)
    denom = max(1, len(states) - 1)
    return transitions, transitions / denom, max_one_run, max_zero_run


def spike_dominant_bucket(row: dict) -> str:
    """Pick the largest known contributor on a spike row."""
    candidates = {
        "fluid_map": float(row.get("fluid_map_cpu_ms") or 0),
        "world_extra": float(row.get("world_extra_ms") or 0),
        "stream": float(row.get("stream_ms") or 0),
        "relight": float(row.get("relight_drain_ms") or 0),
        "emerge": float(row.get("mesh_emerge_ms") or 0),
        "tick_env": float(row.get("tick_env_ms") or 0),
        "block_input": float(row.get("block_input_ms") or 0),
    }
    # Prefer explicit scopes when they dominate world_extra residue.
    best_name = "other"
    best_val = 0.0
    for name, val in candidates.items():
        if val > best_val:
            best_val = val
            best_name = name
    wall = float(row.get("wall_ms") or row.get("max_wall_ms") or 0)
    if best_val <= 0.0 or (wall > 0 and best_val < 0.15 * wall):
        return "other"
    return best_name


def detect_stop_segment(periods: list[dict], min_len: int = 3) -> list[dict]:
    """Last contiguous run where focus chunk does not move (player stopped)."""
    if len(periods) < min_len:
        return periods[-min_len:] if periods else []
    run_len = 1
    for i in range(len(periods) - 1, 0, -1):
        c0 = periods[i - 1]
        c1 = periods[i]
        dx = abs(int(c1.get("focus_cx") or 0) - int(c0.get("focus_cx") or 0))
        dz = abs(int(c1.get("focus_cz") or 0) - int(c0.get("focus_cz") or 0))
        if dx == 0 and dz == 0:
            run_len += 1
        else:
            break
    if run_len >= min_len:
        return periods[-run_len:]
    return periods[-min_len:] if len(periods) >= min_len else periods


def classify_stop_period(r: dict) -> str:
    """Classify a focus-plateau period: calm / recovery / contaminated stand."""
    pb = float(r.get("physics_block_ms") or 0)
    imm = float(r.get("edit_immediate_n") or 0)
    pend = float(r.get("pending_light_focus") or 0)
    if imm > 0 or pb >= 50.0:
        return "contaminated_stop"
    # Recovery = light debt still clearing; sticky alone is visual, not perf-idle.
    if pend > 0:
        return "recovery_stop"
    if pb < 5.0 and imm == 0:
        return "calm_stop"
    return "recovery_stop"


def segment_metrics(rows: list[dict]) -> dict:
    """Median/p95 wall and sub-timers for a period subset."""

    def col(rs: list[dict], key: str) -> list[float]:
        return [float(r.get(key) or 0) for r in rs]

    if not rows:
        return {
            "n": 0,
            "wall_med": None,
            "wall_p95": None,
            "emerge_med": None,
            "stream_med": None,
            "phys_med": None,
            "relight_med": None,
            "mesh_immediate_med": None,
            "mesh_dirty_tick_med": None,
            "mesh_prep_med": None,
            "mesh_prep_missing_med": None,
            "mesh_prep_unfinished_med": None,
            "mesh_prep_sticky_med": None,
            "mesh_prep_drop_dirty_med": None,
            "mesh_prep_other_med": None,
            "physics_block_p95": None,
        }

    pb = col(rows, "physics_block_ms")
    return {
        "n": len(rows),
        "wall_med": median(col(rows, "wall_ms")),
        "wall_p95": p95(col(rows, "wall_ms")),
        "emerge_med": median(col(rows, "mesh_emerge_ms")),
        "stream_med": median(col(rows, "stream_ms")),
        "phys_med": median(col(rows, "phys_ms")),
        "relight_med": median(col(rows, "relight_drain_ms")),
        "mesh_immediate_med": median(col(rows, "mesh_immediate_ms")),
        "mesh_dirty_tick_med": median(col(rows, "mesh_dirty_tick_ms")),
        "mesh_prep_med": median(col(rows, "mesh_emerge_prep_ms")),
        "mesh_prep_missing_med": median(col(rows, "mesh_emerge_prep_missing_ms")),
        "mesh_prep_unfinished_med": median(
            col(rows, "mesh_emerge_prep_unfinished_ms")
        ),
        "mesh_prep_sticky_med": median(col(rows, "mesh_emerge_prep_sticky_ms")),
        "mesh_prep_drop_dirty_med": median(
            col(rows, "mesh_emerge_prep_drop_dirty_ms")
        ),
        "mesh_prep_other_med": median(col(rows, "mesh_emerge_prep_other_ms")),
        "physics_block_p95": p95(pb) if pb else None,
    }


def detect_longest_stop_segment(periods: list[dict], min_len: int = 5) -> list[dict]:
    """Longest contiguous focus plateau (manual idle / hover)."""
    if len(periods) < min_len:
        return detect_stop_segment(periods, min_len=max(3, min_len // 2))
    best_start = 0
    best_len = 0
    run_start = 0
    run_len = 1
    for i in range(1, len(periods)):
        c0 = periods[i - 1]
        c1 = periods[i]
        dx = abs(int(c1.get("focus_cx") or 0) - int(c0.get("focus_cx") or 0))
        dz = abs(int(c1.get("focus_cz") or 0) - int(c0.get("focus_cz") or 0))
        if dx == 0 and dz == 0:
            run_len += 1
        else:
            if run_len > best_len:
                best_len = run_len
                best_start = run_start
            run_start = i
            run_len = 1
    if run_len > best_len:
        best_len = run_len
        best_start = len(periods) - run_len
    if best_len >= min_len:
        return periods[best_start : best_start + best_len]
    return detect_stop_segment(periods, min_len=max(3, min_len // 2))


def analyze(
    path: Path,
    warmup_sec: float = 5.0,
    stop_tail_periods: int = 5,
    manual_idle: bool = False,
    baseline_manual: Path | None = None,
) -> dict:
    rows = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    periods = [r for r in rows if r.get("kind") == "period"]
    spikes = [r for r in rows if r.get("kind") == "spike"]
    skip = max(2, int(warmup_sec / 2.0))
    steady = periods[skip:] if len(periods) > skip else periods

    def col(rs, key):
        return [float(r.get(key) or 0) for r in rs if key in r]

    unfinished_key = (
        "unfinished_visual"
        if any("unfinished_visual" in r for r in steady)
        else None
    )
    hole_key = (
        "unfinished_visual"
        if unfinished_key
        else (
            "visual_holes"
            if any("visual_holes" in r for r in steady)
            else "near_focus_holes"
        )
    )
    holes = col(steady, hole_key)
    dark_sticky = col(steady, "black_sticky")
    if not dark_sticky and any("focus_dark_mesh" in r for r in steady):
        dark_sticky = col(steady, "focus_dark_mesh")
    unfinished_visual = col(steady, unfinished_key) if unfinished_key else []
    # SoT unfinished / missing only. Heavy pending is soft light debt, not a hole.
    effective_holes = []
    for i, (r, h, d) in enumerate(zip(steady, holes, dark_sticky)):
        unfinished = (
            unfinished_visual[i] > 0 if i < len(unfinished_visual) else False
        ) or h > 0
        effective_holes.append(1.0 if unfinished else 0.0)
    effective_holes_rate = (
        sum(effective_holes) / len(effective_holes) if effective_holes else 1.0
    )
    (
        effective_holes_blink_transitions,
        effective_holes_blink_rate,
        effective_holes_longest_hole_run,
        effective_holes_longest_clear_run,
    ) = binary_transition_stats(effective_holes)
    visible_black = col(steady, "visible_black_focus_n")
    black_sticky_vals = dark_sticky
    visible_black_binary = [1.0 if v > 0.5 else 0.0 for v in visible_black]
    black_sticky_binary = [1.0 if v > 0.5 else 0.0 for v in black_sticky_vals]
    (
        visible_black_blink_transitions,
        visible_black_blink_rate,
        visible_black_longest_run,
        visible_black_longest_clear_run,
    ) = binary_transition_stats(visible_black_binary)
    (
        black_sticky_blink_transitions,
        black_sticky_blink_rate,
        black_sticky_longest_run,
        black_sticky_longest_clear_run,
    ) = binary_transition_stats(black_sticky_binary)
    visual_instability = []
    for i, r in enumerate(steady):
        bad = effective_holes[i] > 0.5 if i < len(effective_holes) else False
        if i < len(visible_black) and visible_black[i] > 0.5:
            bad = True
        if i < len(black_sticky_vals) and black_sticky_vals[i] > 0.5:
            bad = True
        visual_instability.append(1.0 if bad else 0.0)
    (
        visual_instability_blink_transitions,
        visual_instability_blink_rate,
        visual_instability_longest_bad_run,
        visual_instability_longest_clear_run,
    ) = binary_transition_stats(visual_instability)
    capture_periods = [
        r for r in steady if float(r.get("relight_capture_ms") or 0) > 0.01
    ]
    capture_partial_n = sum(
        1
        for r in capture_periods
        if int(r.get("relight_capture_finalize") or 0) == 0
    )
    capture_final_n = sum(
        1
        for r in capture_periods
        if int(r.get("relight_capture_finalize") or 0) == 1
    )
    relight_capture_partial_rate = (
        capture_partial_n / len(capture_periods) if capture_periods else None
    )
    relight_apply_partial_frames = sum(
        int(r.get("relight_apply_partial_n") or 0) for r in steady
    )
    relight_apply_final_frames = sum(
        int(r.get("relight_apply_final_n") or 0) for r in steady
    )
    pending_partial_capture_sec = 0.0
    run = 0
    for r in steady:
        pending = float(r.get("pending_light_focus") or 0)
        captured = float(r.get("relight_capture_ms") or 0) > 0.01
        partial = int(r.get("relight_capture_finalize") or 0) == 0
        vb = float(r.get("visible_black_focus_n") or 0) > 0
        if pending >= 5 and captured and partial and vb:
            run += 1
            pending_partial_capture_sec = max(pending_partial_capture_sec, run * 2.0)
        else:
            run = 0
    deferred_far_pending_max = None
    if any("relight_deferred_far_pending" in r for r in steady):
        deferred_far_pending_max = max(
            col(steady, "relight_deferred_far_pending") or [0.0]
        )
    mesh_async_stuck_idle = 0
    run = 0
    for r in steady:
        async_n = float(r.get("mesh_async") or 0)
        relight = float(r.get("relight_drain_ms") or 0)
        cx = int(r.get("focus_cx") or 0)
        cz = int(r.get("focus_cz") or 0)
        if async_n >= 40 and relight < 0.05:
            run += 1
            mesh_async_stuck_idle = max(mesh_async_stuck_idle, run)
        else:
            run = 0
    mesh_async_stuck_sec = mesh_async_stuck_idle * 2.0

    dirty = col(steady, "dirty")
    pending_f = col(steady, "pending_light_focus")
    pressure = col(steady, "stream_pressure")
    wall = col(steady, "wall_ms")
    mesh_async = col(steady, "mesh_async")

    # GPU pipeline telemetry (G0+ gates).
    from collections import Counter

    gpu_draw_cmds = col(steady, "gpu_draw_cmds")
    gpu_cull_ms = col(steady, "gpu_cull_ms")
    vertex_pool_fill = col(steady, "vertex_pool_fill")
    store_names = [
        str(r.get("backend_store") or "")
        for r in steady
        if r.get("backend_store")
    ]
    mesher_names = [
        str(r.get("backend_mesher") or "")
        for r in steady
        if r.get("backend_mesher")
    ]
    cull_names = [
        str(r.get("backend_cull") or "")
        for r in steady
        if r.get("backend_cull")
    ]
    backend_store_mode = (
        Counter(store_names).most_common(1)[0][0] if store_names else ""
    )
    backend_mesher_mode = (
        Counter(mesher_names).most_common(1)[0][0] if mesher_names else ""
    )
    backend_cull_mode = (
        Counter(cull_names).most_common(1)[0][0] if cull_names else ""
    )
    backend_store_mdi = 1.0 if backend_store_mode == "mdi_vertex_pool" else 0.0
    backend_mesher_gpu = (
        1.0
        if (
            backend_mesher_mode.startswith("gpu_")
            or backend_mesher_mode.startswith("android_gpu")
        )
        else 0.0
    )
    backend_cull_gpu = 1.0 if backend_cull_mode.startswith("gpu_") else 0.0
    caps_has_compute_med = median(col(steady, "caps_has_compute"))
    caps_has_ssbo_med = median(col(steady, "caps_has_ssbo"))
    caps_probe_completed_med = median(col(steady, "caps_probe_completed"))
    android_gpu_user_pref_med = median(col(steady, "android_gpu_user_pref"))
    android_gpu_effective_med = median(col(steady, "android_gpu_effective"))
    deny_names = [
        str(r.get("android_gpu_deny_reason") or "")
        for r in steady
        if r.get("android_gpu_deny_reason")
    ]
    gl_version_names = [
        str(r.get("gl_version") or "") for r in steady if r.get("gl_version")
    ]
    gl_renderer_names = [
        str(r.get("gl_renderer") or "") for r in steady if r.get("gl_renderer")
    ]
    android_gpu_deny_reason = (
        Counter(deny_names).most_common(1)[0][0] if deny_names else ""
    )
    gl_version = (
        Counter(gl_version_names).most_common(1)[0][0]
        if gl_version_names
        else ""
    )
    gl_renderer = (
        Counter(gl_renderer_names).most_common(1)[0][0]
        if gl_renderer_names
        else ""
    )
    # Alias without _med for AG gates that use exact keys from plan.
    caps_probe_completed = caps_probe_completed_med
    caps_has_compute = caps_has_compute_med
    android_gpu_effective = android_gpu_effective_med
    android_gpu_user_pref = android_gpu_user_pref_med
    opaque_cmd_total_med = median(col(steady, "opaque_cmd_total"))
    opaque_cmd_on_med = median(col(steady, "opaque_cmd_on"))
    cross_batch_count_med = median(col(steady, "cross_batch_count"))
    cpu_aabb_would_on_med = median(col(steady, "cpu_aabb_would_on"))
    edit_immediate_n_med = median(col(steady, "edit_immediate_n"))
    edit_dirty_n_med = median(col(steady, "edit_dirty_n"))
    edit_neighbor_pending_frames_med = median(
        col(steady, "edit_neighbor_pending_frames")
    )
    pool_unsync_uploads_med = median(col(steady, "pool_unsync_uploads"))
    pool_fence_wait_ms_med = median(col(steady, "pool_fence_wait_ms"))
    chunk_meshed_culled0_med = median(col(steady, "chunk_meshed_culled0"))
    chunk_meshed_unlit_med = median(col(steady, "chunk_meshed_unlit"))
    chunk_not_ready_med = median(col(steady, "chunk_not_ready"))
    opaque_on_vals = col(steady, "opaque_cmd_on")
    opaque_on_min = min(opaque_on_vals) if opaque_on_vals else None
    if (
        opaque_cmd_total_med is not None
        and opaque_cmd_on_med is not None
        and opaque_cmd_total_med > 0
    ):
        opaque_culled_frac_med = max(
            0.0, (opaque_cmd_total_med - opaque_cmd_on_med) / opaque_cmd_total_med
        )
    else:
        opaque_culled_frac_med = None
    cross_med = cross_batch_count_med or 0.0
    blue_screen_suspect = (
        1.0
        if cross_med > 0 and opaque_on_min is not None and opaque_on_min <= 0
        else 0.0
    )
    gpu_draw_cmds_med = median(gpu_draw_cmds)
    gpu_cull_ms_med = median(gpu_cull_ms)
    vertex_pool_fill_med = median(vertex_pool_fill)
    gpu_cull_indirect_med = median(col(steady, "gpu_cull_indirect"))
    gpu_mesh_vbo_dispatch_med = median(col(steady, "gpu_mesh_vbo_dispatch"))
    gpu_light_seed_apply_med = median(col(steady, "gpu_light_seed_apply"))
    gpu_mask_readback_med = median(col(steady, "gpu_mask_readback"))
    gpu_blocklight_flood_med = median(col(steady, "gpu_blocklight_flood"))
    gpu_fluid_readback_med = median(col(steady, "gpu_fluid_readback"))
    gpu_light_readback_med = median(col(steady, "gpu_light_readback"))
    gpu_opaque_emit_gpu_med = median(col(steady, "gpu_opaque_emit_gpu"))
    gpu_opaque_emit_gpu_max = max_val(col(periods, "gpu_opaque_emit_gpu"))
    gpu_transparent_sort_gpu_med = median(col(steady, "gpu_transparent_sort_gpu"))
    gpu_transparent_sort_gpu_max = max_val(col(periods, "gpu_transparent_sort_gpu"))
    gpu_fallback = col(steady, "gpu_fallback")
    gpu_fallback_rate = (
        sum(1 for v in gpu_fallback if v > 0) / len(gpu_fallback)
        if gpu_fallback
        else 1.0
    )
    gpu_fluid_scan_on_med = median(col(steady, "gpu_fluid_scan_on"))
    fluid_names = [
        str(r.get("backend_fluid") or "")
        for r in steady
        if r.get("backend_fluid")
    ]
    backend_fluid_mode = (
        Counter(fluid_names).most_common(1)[0][0] if fluid_names else ""
    )
    lighting_names = [
        str(r.get("backend_lighting_mode") or "")
        for r in steady
        if r.get("backend_lighting_mode")
    ]
    backend_lighting_mode = (
        Counter(lighting_names).most_common(1)[0][0] if lighting_names else ""
    )
    backend_lighting_flat = 1.0 if backend_lighting_mode == "flat" else 0.0
    backend_lighting_full = (
        1.0
        if backend_lighting_mode in ("full", "gpu_full")
        else 0.0
    )

    holes_rate = (sum(1 for h in holes if h > 0) / len(holes)) if holes else 1.0
    red_rate = (sum(1 for p in pressure if p >= 2) / len(pressure)) if pressure else 1.0

    focus_pts = [
        (int(r.get("focus_cx") or 0), int(r.get("focus_cz") or 0)) for r in steady
    ]
    chunks_traveled = 0
    if focus_pts:
        c0, c1 = focus_pts[0], focus_pts[-1]
        chunks_traveled = max(abs(c1[0] - c0[0]), abs(c1[1] - c0[1]))

    stuck_async_holes = 0
    run = 0
    for r in steady:
        h = float(r.get(hole_key) or 0)
        a = float(r.get("mesh_async") or 0)
        if h > 0 and a >= 40:
            run += 1
            stuck_async_holes = max(stuck_async_holes, run)
        else:
            run = 0
    stuck_async_holes_sec = stuck_async_holes * 2.0

    cold_relight_holes = 0
    run = 0
    for r in steady:
        h = float(r.get(hole_key) or 0)
        a = float(r.get("mesh_async") or 0)
        p = float(r.get("pending_light_focus") or 0)
        rd = float(r.get("relight_drain_ms") or 0)
        if h > 0 and p > 0 and a < 4 and rd < 0.5:
            run += 1
            cold_relight_holes = max(cold_relight_holes, run)
        else:
            run = 0
    cold_relight_holes_sec = cold_relight_holes * 2.0

    # Era18 P0: focus light-debt honesty (manual 165953 stand-in-black).
    # Period≈2s (same scale as miss_stuck / cold_relight).
    def _max_run_sec(pred) -> float:
        run = 0
        best = 0
        for r in steady:
            if pred(r):
                run += 1
                best = max(best, run)
            else:
                run = 0
        return best * 2.0

    vb_without_pending_light_focus_sec = _max_run_sec(
        lambda r: float(r.get("visible_black_focus_n") or 0) > 0
        and float(r.get("pending_light_focus") or 0) <= 0
    )
    relight_drain_near_zero_while_vb_sec = _max_run_sec(
        lambda r: float(r.get("visible_black_focus_n") or 0) > 0
        and float(r.get("relight_drain_ms") or 0) < 0.5
    )
    softdefer_capture_zero_while_vb_sec = _max_run_sec(
        lambda r: float(r.get("visible_black_focus_n") or 0) > 0
        and float(r.get("softdefer_capture_budget") or 0) <= 0
    )
    # Era19: VB heal forced on already-hot frames while tops still missing
    # (manual 191229: wall_med 279 + holes↑ — heal-on-hot feedback).
    heal_on_hot_sec = _max_run_sec(
        lambda r: float(r.get("visible_black_focus_n") or 0) > 0
        and float(r.get("wall_ms") or r.get("max_wall_ms") or 0) > 200.0
        and (
            float(r.get("focus_missing_mesh") or 0) > 0
            or float(r.get("visual_holes") or r.get("near_focus_holes") or 0) > 0
        )
    )
    # Era20: SoftDefer empty stuck (HasGreedy∧!Drawable) while moving/miss.
    soft_defer_empty_stuck_sec = _max_run_sec(
        lambda r: float(r.get("softdefer_empty_stuck_n") or 0) > 0
    )
    # Era21: remesh thrash proxy — discarded_late ramp during cruise (not stop).
    mesh_discarded_late_delta_cruise = 0.0
    # Era21: VB tickets/progress without dark faces clearing (heal stall).
    vb_progress_without_dark_clear_sec = 0.0
    miss_cy_gt1_n = sum(
        1
        for r in steady
        if float(r.get("focus_missing_mesh") or 0) > 0
        and int(r.get("miss_cy") or -1) > 1
    )
    miss_periods_n = sum(
        1 for r in steady if float(r.get("focus_missing_mesh") or 0) > 0
    )
    miss_cy_gt1_frac = (
        float(miss_cy_gt1_n) / float(miss_periods_n) if miss_periods_n else 0.0
    )
    app_updates = [
        float(r.get("app_update_ms") or 0) for r in periods if r.get("app_update_ms") is not None
    ]
    # First InGame periods often carry enter hitch (manual 214034 ≈2097).
    enter_app_update_max = max(app_updates[:3]) if app_updates else None
    if enter_app_update_max is None and periods:
        enter_app_update_max = max(
            (float(r.get("app_update_ms") or 0) for r in periods[:3]),
            default=None,
        )

    # Land-cruise symptoms (manual 131234 / 142306): miss stuck, miss at end,
    # opaque draw-list churn while hovering on one focus chunk.
    miss_key = (
        "focus_missing_mesh"
        if any("focus_missing_mesh" in r for r in steady)
        else hole_key
    )
    miss_stuck_run = 0
    miss_stuck_max = 0
    miss_stuck_frames = 0
    miss_stuck_cx = 0
    miss_stuck_cz = 0
    for r in steady:
        if float(r.get(miss_key) or 0) > 0:
            miss_stuck_run += 1
            if miss_stuck_run > miss_stuck_max:
                miss_stuck_max = miss_stuck_run
                miss_stuck_frames = miss_stuck_run
                miss_stuck_cx = int(r.get("focus_cx") or 0)
                miss_stuck_cz = int(r.get("focus_cz") or 0)
        else:
            miss_stuck_run = 0
    miss_stuck_max_run_sec = miss_stuck_max * 2.0
    miss_end = 0.0
    if periods:
        miss_end = float(periods[-1].get(miss_key) or 0)
    miss_mesh_key = (
        "focus_missing_mesh"
        if any("focus_missing_mesh" in r for r in steady)
        else None
    )
    nh_no_miss_n = 0
    for r in steady:
        nh = float(r.get("near_focus_holes") or 0)
        miss = float(r.get(miss_key) or 0)
        if nh > 0 and miss <= 0:
            nh_no_miss_n += 1
    nh_no_miss_rate = (nh_no_miss_n / len(steady)) if steady else 0.0
    opaque_idle_churn_max = 0.0
    i = 0
    while i < len(periods):
        j = i + 1
        while (
            j < len(periods)
            and int(periods[j].get("focus_cx") or 0)
            == int(periods[i].get("focus_cx") or 0)
            and int(periods[j].get("focus_cz") or 0)
            == int(periods[i].get("focus_cz") or 0)
        ):
            j += 1
        if j - i >= 2:
            ops = [float(r.get("opaque_cmd_on") or 0) for r in periods[i:j]]
            if ops:
                opaque_idle_churn_max = max(
                    opaque_idle_churn_max, max(ops) - min(ops)
                )
        i = j if j > i else i + 1

    dirty_high_run = 0
    run = 0
    for d in dirty:
        if d > 800:
            run += 1
            dirty_high_run = max(dirty_high_run, run)
        else:
            run = 0
    dirty_high_sec = dirty_high_run * 2.0

    def ok_med(val, limit):
        return val is not None and val <= limit

    pending_trend_rising = False
    if len(pending_f) >= 4 and chunks_traveled >= 3:
        mid = len(pending_f) // 2
        first = median(pending_f[:mid]) or 0.0
        second = median(pending_f[mid:]) or 0.0
        pending_trend_rising = second > first + 8.0
    black_proxy_periods = 0
    for r in steady:
        h = float(r.get(hole_key) or 0)
        p = float(r.get("pending_light_focus") or 0)
        if h <= 0 and p >= 20:
            black_proxy_periods += 1
    black_proxy_rate = black_proxy_periods / len(steady) if steady else 0.0

    stop_segment = (
        detect_longest_stop_segment(steady, min_len=8)
        if manual_idle
        else detect_stop_segment(steady, min_len=max(3, stop_tail_periods // 2))
    )
    miss_end_stop = 0.0
    if stop_segment and miss_mesh_key:
        miss_end_stop = float(stop_segment[-1].get(miss_mesh_key) or 0)
    elif stop_segment:
        miss_end_stop = float(stop_segment[-1].get(miss_key) or 0)
    fly_segment = (
        steady[: len(steady) - len(stop_segment)] if stop_segment else steady
    )
    if len(fly_segment) >= 2:
        d0 = float(fly_segment[0].get("mesh_discarded_late") or 0)
        d1 = float(fly_segment[-1].get("mesh_discarded_late") or 0)
        mesh_discarded_late_delta_cruise = max(0.0, d1 - d0)
    vb_progress_without_dark_clear_sec = _max_run_sec(
        lambda r: float(r.get("visible_black_focus_n") or 0) > 0
        and float(r.get("visible_black_progress_n") or 0) > 0
        and float(r.get("visible_black_no_ticket_n") or 0) <= 0
        and float(r.get("dark_face_stale_near_n") or r.get("dark_face_near_n") or 0)
        > 0
        and float(r.get("wall_ms") or r.get("max_wall_ms") or 0) > 80.0
    )
    wall_fly = col(fly_segment, "wall_ms") if fly_segment else wall

    # Moving / fly periods without visual holes: FPS thrash from lit-but-dirty.
    no_hole_fly = [
        r
        for r in fly_segment
        if float(r.get(hole_key) or 0) <= 0
        and float(r.get("focus_missing_mesh") or 0) <= 0
    ]
    wall_no_holes = col(no_hole_fly, "wall_ms")
    dirty_no_holes = col(no_hole_fly, "dirty")
    async_no_holes = col(no_hole_fly, "mesh_async")
    wall_ms_no_holes_med = median(wall_no_holes)
    dirty_med_no_holes = median(dirty_no_holes)
    mesh_async_med_no_holes = median(async_no_holes)

    spike_count = len(spikes)
    spike_walls = [
        float(r.get("wall_ms") or r.get("max_wall_ms") or 0) for r in spikes
    ]
    spike_max_wall = max(spike_walls) if spike_walls else 0.0
    hole_spike_walls = [
        float(r.get("wall_ms") or 0)
        for r in spikes
        if float(r.get(hole_key) or r.get("visual_holes") or 0) > 0
        or float(r.get("focus_missing_mesh") or 0) > 0
    ]
    spike_max_wall_holes = max(hole_spike_walls) if hole_spike_walls else 0.0

    def series(rows: list[dict], key: str) -> list[float]:
        return [float(r.get(key) or 0) for r in rows]

    world_extra_fly = series(fly_segment, "world_extra_ms")
    tick_env_fly = series(fly_segment, "tick_env_ms")
    block_input_fly = series(fly_segment, "block_input_ms")
    world_extra_stop = series(stop_segment, "world_extra_ms")
    tick_env_stop = series(stop_segment, "tick_env_ms")
    block_input_stop = series(stop_segment, "block_input_ms")

    spike_buckets: dict[str, int] = {}
    spike_world_extra_vals: list[float] = []
    heavy_spikes = []
    for r in spikes:
        spike_wall = float(r.get("wall_ms") or r.get("max_wall_ms") or 0)
        bucket = spike_dominant_bucket(r)
        spike_buckets[bucket] = spike_buckets.get(bucket, 0) + 1
        we = float(r.get("world_extra_ms") or 0)
        spike_world_extra_vals.append(we)
        if spike_wall >= 500.0:
            heavy_spikes.append(r)
    heavy_bucket_counts: dict[str, int] = {}
    for r in heavy_spikes:
        b = spike_dominant_bucket(r)
        heavy_bucket_counts[b] = heavy_bucket_counts.get(b, 0) + 1
    dominant_spike_class = (
        max(spike_buckets.items(), key=lambda kv: kv[1])[0]
        if spike_buckets
        else "other"
    )
    dominant_heavy_spike_class = (
        max(heavy_bucket_counts.items(), key=lambda kv: kv[1])[0]
        if heavy_bucket_counts
        else dominant_spike_class
    )
    spike_max_world_extra = (
        max(spike_world_extra_vals) if spike_world_extra_vals else 0.0
    )
    spikes_world_extra_dominant = 0
    for r in spikes:
        spike_wall = float(r.get("wall_ms") or r.get("max_wall_ms") or 0)
        we = float(r.get("world_extra_ms") or 0)
        if spike_wall > 0 and we > 0.5 * spike_wall:
            spikes_world_extra_dominant += 1
    spike_world_extra_dominant_rate = (
        spikes_world_extra_dominant / len(spikes) if spikes else 0.0
    )

    break_complete_sum = sum(int(r.get("break_complete_n") or 0) for r in rows)
    break_race_sum = sum(int(r.get("break_inflight_race_n") or 0) for r in rows)
    break_dark_sum = sum(int(r.get("break_dark_face_n") or 0) for r in rows)
    place_complete_sum = sum(int(r.get("place_complete_n") or 0) for r in rows)

    async_stuck_sec = max(stuck_async_holes_sec, mesh_async_stuck_sec)
    wall_fly_med = median(wall_fly)
    void_fly = col(fly_segment, "dark_face_void_near_n") if fly_segment else []
    vb_fly = col(fly_segment, "visible_black_focus_n") if fly_segment else []
    fluid_fly = col(fly_segment, "fluid_map_cpu_ms") if fly_segment else []
    fly_void_near_max = max(void_fly) if void_fly else None
    fly_visible_black_max = max(vb_fly) if vb_fly else None
    fly_fluid_map_cpu_max = max(fluid_fly) if fluid_fly else None
    void_peak_period_idx = None
    void_drain_rate = None
    if void_fly and fly_segment:
        void_peak_period_idx = int(
            max(range(len(void_fly)), key=lambda i: void_fly[i])
        )
        if len(void_fly) >= 2 and void_peak_period_idx < len(void_fly) - 1:
            tail = void_fly[void_peak_period_idx:]
            duration_sec = (len(tail) - 1) * 2.0
            if duration_sec > 0:
                void_drain_rate = (tail[0] - tail[-1]) / duration_sec
        elif len(void_fly) >= 2:
            duration_sec = (len(void_fly) - 1) * 2.0
            if duration_sec > 0:
                void_drain_rate = (void_fly[0] - void_fly[-1]) / duration_sec
    emerge_spike_frac = (
        float(spike_buckets.get("emerge", 0)) / float(len(spikes))
        if spikes
        else None
    )
    frontier_fly = col(fly_segment, "frontier_pressure") if fly_segment else []
    fly_frontier_pressure_frac = (
        sum(1 for v in frontier_fly if v > 0) / len(frontier_fly)
        if frontier_fly
        else None
    )
    chunk_count_fly = col(fly_segment, "chunk_count") if fly_segment else []
    chunk_count_end = (
        float(chunk_count_fly[-1]) if chunk_count_fly else None
    )
    mesh_sync_fly_med = (
        median(col(fly_segment, "mesh_sync_ms")) if fly_segment else None
    )
    gates = {
        "visual_holes_rate_le_0_10": effective_holes_rate <= 0.10,
        "dirty_med_le_400": ok_med(median(dirty), 400),
        "dirty_not_plateau_gt800_10s": dirty_high_sec <= 10.0,
        "pending_light_focus_med_le_15": ok_med(median(pending_f), 15),
        "stream_pressure_red_rate_le_0_30": red_rate <= 0.30,
        "mesh_async_not_stuck_10s": async_stuck_sec <= 10.0,
        "wall_ms_med_le_25": ok_med(wall_fly_med, 25),
        "chunks_traveled_ge_3": chunks_traveled >= 3,
    }
    gates_pass_count = sum(1 for v in gates.values() if v)

    stop_tail = (
        stop_segment[-stop_tail_periods:]
        if len(stop_segment) >= stop_tail_periods
        else stop_segment
    )
    sticky_key = (
        "black_sticky"
        if any("black_sticky" in r for r in stop_tail)
        else "focus_dark_mesh"
    )
    black_sticky_stop = col(stop_tail, sticky_key)
    missing_stop = col(stop_tail, hole_key)
    focus_miss_stop = (
        col(stop_tail, miss_mesh_key) if miss_mesh_key else []
    )
    unfinished_stop = col(stop_tail, unfinished_key) if unfinished_key else []
    not_ready_stop = col(stop_tail, "focus_not_render_ready")
    pending_stop = col(stop_tail, "pending_light_focus")
    relight_stop = col(stop_tail, "relight_drain_ms")
    post_stop_pending_med = median(pending_stop)
    post_stop_black_sticky_max = (
        max(black_sticky_stop) if black_sticky_stop else None
    )
    vis_black_stop = col(stop_tail, "visible_black_focus_n")
    vis_black_no_ticket_stop = col(stop_tail, "visible_black_no_ticket_n")
    vis_black_progress_stop = col(stop_tail, "visible_black_progress_n")
    vis_black_stalled_stop = col(stop_tail, "visible_black_stalled_n")
    post_stop_visible_black_max = (
        max(vis_black_stop) if vis_black_stop else None
    )
    post_stop_visible_black_no_ticket_max = (
        max(vis_black_no_ticket_stop) if vis_black_no_ticket_stop else None
    )
    post_stop_visible_black_progress_min = (
        min(vis_black_progress_stop) if vis_black_progress_stop else None
    )
    post_stop_visible_black_stalled_max = (
        max(vis_black_stalled_stop) if vis_black_stalled_stop else None
    )
    post_stop_missing_max = max(missing_stop) if missing_stop else None
    post_stop_focus_miss_max = (
        max(focus_miss_stop) if focus_miss_stop else None
    )
    post_stop_miss_low_cy_n = sum(
        1
        for r in stop_tail
        if float(r.get("focus_missing_mesh") or 0) > 0
        and int(r.get("miss_cy") or 99) <= 3
    )
    post_stop_underfeet_ok_miss_n = sum(
        1
        for r in stop_tail
        if int(r.get("underfeet_draw_ok") or 0) == 1
        and float(r.get("focus_missing_mesh") or 0) > 0
    )
    session_tail = (
        periods[-max(3, stop_tail_periods) :] if periods else []
    )
    tail_focus_miss_max = (
        max(float(r.get("focus_missing_mesh") or 0) for r in session_tail)
        if session_tail and miss_mesh_key
        else None
    )
    tail_miss_low_cy_n = sum(
        1
        for r in session_tail
        if float(r.get("focus_missing_mesh") or 0) > 0
        and int(r.get("miss_cy") or 99) <= 3
    )
    tail_underfeet_ok_miss_n = sum(
        1
        for r in session_tail
        if int(r.get("underfeet_draw_ok") or 0) == 1
        and float(r.get("focus_missing_mesh") or 0) > 0
    )
    stop_effective = []
    for i, r in enumerate(stop_tail):
        h = float(r.get(hole_key) or 0)
        d = float(r.get(sticky_key) or 0)
        pend = float(r.get("pending_light_focus") or 0)
        nr = float(r.get("focus_not_render_ready") or 0)
        unfinished = unfinished_stop[i] > 0 if i < len(unfinished_stop) else False
        stop_effective.append(
            1.0
            if (unfinished or h > 0 or d > 0 or pend >= 15 or nr >= 8)
            else 0.0
        )
    post_stop_effective_holes_rate = (
        sum(stop_effective) / len(stop_effective) if stop_effective else 1.0
    )
    (
        post_stop_effective_holes_blink_transitions,
        post_stop_effective_holes_blink_rate,
        post_stop_longest_hole_run,
        post_stop_longest_clear_run,
    ) = binary_transition_stats(stop_effective)
    post_stop_relight_med = median(relight_stop)
    post_stop_not_ready_med = median(not_ready_stop) if not_ready_stop else None
    post_stop_not_ready_end = (
        not_ready_stop[-1] if not_ready_stop else None
    )
    # Full-segment deltas — tail-only hid real recovery (65→42) when the last
    # 5 periods were already flat at ~40.
    not_ready_full = col(stop_segment, "focus_not_render_ready")
    pending_full = col(stop_segment, "pending_light_focus")
    focus_dirty_full = col(stop_segment, "focus_dirty_chunks")
    focus_dirty_tail = col(stop_tail, "focus_dirty_chunks")
    mesh_stale_full = col(stop_segment, "mesh_apply_stale")
    mesh_discard_full = col(stop_segment, "mesh_discarded_late")
    stop_not_ready_delta = None
    if len(not_ready_full) >= 2:
        stop_not_ready_delta = not_ready_full[-1] - not_ready_full[0]
    stop_pending_delta = None
    if len(pending_full) >= 2:
        stop_pending_delta = pending_full[-1] - pending_full[0]
    post_stop_focus_dirty_med = (
        median(focus_dirty_tail) if focus_dirty_tail else None
    )
    post_stop_focus_dirty_end = (
        focus_dirty_tail[-1] if focus_dirty_tail else None
    )
    stop_focus_dirty_delta = None
    if len(focus_dirty_full) >= 2:
        stop_focus_dirty_delta = focus_dirty_full[-1] - focus_dirty_full[0]
    stop_mesh_apply_stale_delta = None
    if len(mesh_stale_full) >= 2:
        stop_mesh_apply_stale_delta = mesh_stale_full[-1] - mesh_stale_full[0]
    stop_mesh_discarded_late_delta = None
    if len(mesh_discard_full) >= 2:
        stop_mesh_discarded_late_delta = (
            mesh_discard_full[-1] - mesh_discard_full[0]
        )

    # Era13 / D3 arch gates (ROOT_CAUSE_2026-07): spawn ring, async floor, dark.
    idle_head = periods[: max(3, int(8.0 / 2.0))] if periods else []
    post_load_ring_idle_max = None
    if any("post_load_ring_not_ready" in r for r in idle_head):
        pl = col(idle_head, "post_load_ring_not_ready")
        post_load_ring_idle_max = max(pl) if pl else 0.0
    unfinished_idle_max = None
    if unfinished_key and idle_head:
        uf = col(idle_head, unfinished_key)
        unfinished_idle_max = max(uf) if uf else 0.0
    enter_void_near_max = None
    if idle_head:
        enter_void = col(idle_head, "dark_face_void_near_n")
        enter_void_near_max = max(enter_void) if enter_void else None
    enter_unfinished_max = unfinished_idle_max
    # Era38 B2: enter vs cruise debt split (before chunks move / while flying).
    enter_pending_vals = col(idle_head, "pending_light_focus")
    enter_pending_max = max(enter_pending_vals) if enter_pending_vals else None
    enter_softdefer_vals = col(idle_head, "softdefer_empty_placeholder_n")
    enter_softdefer_empty_max = (
        max(enter_softdefer_vals) if enter_softdefer_vals else None
    )
    enter_unlit_vals = col(idle_head, "chunk_meshed_unlit")
    enter_unlit_max = max(enter_unlit_vals) if enter_unlit_vals else None
    fly_rows = [
        r
        for r in steady
        if float(r.get("speed") or r.get("movement_speed") or 0) > 0.5
        or int(r.get("moving") or 0) == 1
    ]
    if len(fly_rows) < 3:
        fly_rows = [
            r
            for r in steady
            if float(r.get("chunks_traveled_delta") or 0) > 0
            or float(r.get("stream_pressure") or 0) > 0
        ]
    cruise_src = fly_rows if len(fly_rows) >= 3 else steady
    cruise_pending_med = median(col(cruise_src, "pending_light_focus"))
    cruise_fifo_med = median(col(cruise_src, "relight_fifo_n"))
    cruise_unlit_med = median(col(cruise_src, "chunk_meshed_unlit"))
    cruise_softdefer_empty_med = median(
        col(cruise_src, "softdefer_empty_placeholder_n")
    )
    cruise_relight_completed_med = median(col(cruise_src, "relight_completed_n"))
    cruise_relight_apply_ms_med = median(col(cruise_src, "relight_apply_ms"))
    cruise_fifo_dropped_vals = col(cruise_src, "relight_fifo_dropped")
    cruise_fifo_dropped_delta = None
    if len(cruise_fifo_dropped_vals) >= 2:
        cruise_fifo_dropped_delta = (
            cruise_fifo_dropped_vals[-1] - cruise_fifo_dropped_vals[0]
        )
    cruise_false_clear_vals = col(cruise_src, "relight_false_clear_n")
    cruise_false_clear_delta = None
    if len(cruise_false_clear_vals) >= 2:
        cruise_false_clear_delta = (
            cruise_false_clear_vals[-1] - cruise_false_clear_vals[0]
        )
    # FP flight-perf: cruise spike metrics (moving y>10).
    fly_spikes = [
        r for r in spikes if float(r.get("player_y") or 0) > 10.0
    ]
    cruise_schedule_ok_med = (
        median(col(fly_spikes, "mesh_dirty_schedule_ok_n"))
        if fly_spikes
        else None
    )
    cruise_capture_retarget_med = (
        median(col(fly_spikes, "softdefer_capture_retarget_n"))
        if fly_spikes
        else None
    )
    cruise_witness_pin_age_med = (
        median(col(fly_spikes, "softdefer_capture_pin_age"))
        if fly_spikes
        else None
    )
    cruise_relight_completed_spike_med = (
        median(col(fly_spikes, "relight_completed_n")) if fly_spikes else None
    )
    cruise_relight_completed_throughput = None
    if len(fly_spikes) >= 2:
        rc0 = float(fly_spikes[0].get("relight_completed_n") or 0)
        rc1 = float(fly_spikes[-1].get("relight_completed_n") or 0)
        cruise_relight_completed_throughput = max(
            0.0, (rc1 - rc0) / max(1, len(fly_spikes) - 1)
        )
    unfinished_visual_med = (
        median(unfinished_visual) if unfinished_visual else None
    )
    visible_black_focus_med = median(visible_black) if visible_black else None
    stream_ms_med = median(col(cruise_src, "stream_ms"))
    dark_face_stale_near_med = median(col(cruise_src, "dark_face_stale_near_n"))
    orphan_vals = col(cruise_src, "mesh_dirty_schedule_skip_orphan_n")
    dirty_ghost_n = max(orphan_vals) if orphan_vals else None
    # Era40 P3: FIFO stuck soft-fail (soft-cap + no apply + dropped churn).
    fifo_soft_cap = 96
    miss_end_or_stuck = (miss_end > 0.0) or (miss_stuck_max_run_sec > 4.0)
    relight_fifo_stuck_soft_fail = (
        cruise_fifo_med is not None
        and cruise_fifo_med >= fifo_soft_cap - 1
        and (
            cruise_relight_apply_ms_med is None
            or cruise_relight_apply_ms_med < 0.5
        )
        and cruise_fifo_dropped_delta is not None
        and cruise_fifo_dropped_delta > 0
        and miss_end_or_stuck
    )
    relight_false_clear_soft_fail = (
        cruise_false_clear_delta is not None
        and cruise_false_clear_delta > 8
        and miss_end_or_stuck
        and (
            cruise_relight_apply_ms_med is None
            or cruise_relight_apply_ms_med < 0.5
        )
    )
    # Era22 sticky-settle: stable holes (EH high, no blink) + sticky flicker regress.
    stable_holes_soft_fail = (
        effective_holes_rate > 0.50 and effective_holes_blink_rate < 0.05
    )
    black_sticky_flicker_soft_fail = black_sticky_blink_rate >= 0.15
    visual_instability_soft_fail = visual_instability_blink_rate >= 0.20
    pending_partial_capture_soft_fail = pending_partial_capture_sec >= 12.0
    # When dirty>100 AND unfinished_visual, mesh_async should stay fed.
    # visual_holes alone can latch without unfinished (legacy) and SoftDefer
    # not_render_ready must not demand mesh workers.
    async_when_dirty = [
        float(r.get("mesh_async") or 0)
        for r in steady
        if float(r.get("dirty") or 0) > 100
        and float(r.get("unfinished_visual") or 0) > 0
    ]
    mesh_async_med_when_dirty = (
        median(async_when_dirty) if async_when_dirty else 99.0
    )
    dark_face_stop = col(stop_tail, "dark_face_near")
    if not dark_face_stop:
        dark_face_stop = col(stop_tail, "dark_face_near_n")
    stop_dark_face_near_end = (
        dark_face_stop[-1] if dark_face_stop else None
    )
    stop_dark_face_near_max = (
        max(dark_face_stop) if dark_face_stop else None
    )
    # Prefer stale-dark proxy for ARCH_D3 (void-edge at World_164 rim is expected).
    dark_stale_stop = col(stop_tail, "dark_face_stale_near_n")
    stop_dark_face_stale_near_end = (
        dark_stale_stop[-1] if dark_stale_stop else None
    )
    dark_void_stop = col(stop_tail, "dark_face_void_near_n")
    stop_dark_face_void_near_end = (
        dark_void_stop[-1] if dark_void_stop else None
    )
    gates["post_load_ring_not_ready_eq_0"] = (
        post_load_ring_idle_max is None or post_load_ring_idle_max <= 0.0
    )
    # SoftDefer Capture SLA (TD-ARCH-030): idle-head pending must fall or stay 0
    # while SoftDefer floor ticks (spawn plr sticky → measurable stall).
    softdefer_hits_idle = col(idle_head, "softdefer_capture_floor_hits_delta")
    softdefer_capture_ticks_idle = (
        sum(softdefer_hits_idle) if softdefer_hits_idle else 0.0
    )
    pending_idle = col(idle_head, "pending_light_focus")
    pending_light_idle_delta = None
    pending_light_idle_end = None
    if pending_idle:
        pending_light_idle_end = pending_idle[-1]
        pending_light_idle_delta = pending_idle[-1] - pending_idle[0]
    gates["spawn_soft_defer_progress"] = (
        pending_light_idle_end is None
        or pending_light_idle_end <= 0.0
        or (
            softdefer_capture_ticks_idle > 0
            and pending_light_idle_delta is not None
            and pending_light_idle_delta < 0
        )
        or softdefer_capture_ticks_idle <= 0
    )
    gates["mesh_async_floor_when_dirty"] = (
        mesh_async_med_when_dirty is None or mesh_async_med_when_dirty >= 4.0
    )
    gates["stop_dark_face_near_lt_100"] = (
        (
            stop_dark_face_stale_near_end is not None
            and stop_dark_face_stale_near_end < 100.0
        )
        or (
            stop_dark_face_stale_near_end is None
            and (
                stop_dark_face_near_end is None or stop_dark_face_near_end < 100.0
            )
        )
    )
    # Land rim symptoms (manual 142306): stuck miss, miss at end, idle opaque churn.
    gates["miss_stuck_max_run_sec_le_4"] = miss_stuck_max_run_sec <= 4.0
    gates["miss_end_eq_0"] = miss_end <= 0.0
    gates["miss_end_stop_eq_0"] = miss_end_stop <= 0.0
    gates["post_stop_focus_miss_zero"] = (
        post_stop_focus_miss_max is None or post_stop_focus_miss_max <= 0.5
    )
    gates["post_stop_miss_low_cy_zero"] = post_stop_miss_low_cy_n == 0
    gates["post_stop_underfeet_ok_miss_zero"] = (
        post_stop_underfeet_ok_miss_n == 0
    )
    gates["tail_focus_miss_zero"] = (
        tail_focus_miss_max is None or tail_focus_miss_max <= 0.5
    )
    gates["tail_miss_low_cy_zero"] = tail_miss_low_cy_n == 0
    gates["tail_underfeet_ok_miss_zero"] = tail_underfeet_ok_miss_n == 0
    gates["opaque_idle_churn_max_le_120"] = opaque_idle_churn_max <= 120.0
    # Light-debt holes with miss=0 (land-cruise L1–L4 nh_no_miss 0.39–0.65).
    gates["nh_no_miss_rate_le_025"] = nh_no_miss_rate <= 0.25
    gates_pass_count = sum(1 for v in gates.values() if v)

    # Manual 083042: pendf stuck ~40 for ~30s while wall~22–30 and holes=0.
    stop_wall = col(stop_segment, "wall_ms")
    stop_wall_med = median(stop_wall)

    edit_immediate_max = max(
        (float(r.get("edit_immediate_n") or 0) for r in steady), default=0.0
    )
    physics_block_steady = col(steady, "physics_block_ms")
    physics_block_steady_p95 = p95(physics_block_steady)
    contaminated_idle = (
        edit_immediate_max > 0.0
        or (
            physics_block_steady_p95 is not None
            and physics_block_steady_p95 > 50.0
        )
        or break_complete_sum > 0
        or place_complete_sum > 0
    )

    calm_stop_rows = [
        r for r in stop_segment if classify_stop_period(r) == "calm_stop"
    ]
    recovery_stop_rows = [
        r for r in stop_segment if classify_stop_period(r) == "recovery_stop"
    ]
    contaminated_stop_rows = [
        r for r in stop_segment if classify_stop_period(r) == "contaminated_stop"
    ]
    calm_stop_metrics = segment_metrics(calm_stop_rows)
    recovery_stop_metrics = segment_metrics(recovery_stop_rows)
    contaminated_stop_metrics = segment_metrics(contaminated_stop_rows)
    stop_segment_metrics = segment_metrics(stop_segment)

    calm_stop_wall_med = calm_stop_metrics["wall_med"]
    if calm_stop_wall_med is None or calm_stop_metrics["n"] < 5:
        calm_stop_wall_med = stop_wall_med
    # Era19: recovery-classified stop (pending>0) still has valid emerge/stream;
    # fall back like wall so IDLE_CLEAN does not fail on None while stop_* is GO.
    calm_stop_emerge_med = calm_stop_metrics["emerge_med"]
    calm_stop_stream_med = calm_stop_metrics["stream_med"]
    if calm_stop_emerge_med is None or calm_stop_metrics["n"] < 5:
        calm_stop_emerge_med = stop_segment_metrics["emerge_med"]
    if calm_stop_stream_med is None or calm_stop_metrics["n"] < 5:
        calm_stop_stream_med = stop_segment_metrics["stream_med"]

    physics_block_ms_p95 = calm_stop_metrics["physics_block_p95"]
    if physics_block_ms_p95 is None:
        physics_block_ms_p95 = stop_segment_metrics["physics_block_p95"]

    stop_pending_full = col(stop_segment, "pending_light_focus")
    stop_pending_plateau_sec = 0.0
    plateau_pending_threshold = 5.0 if manual_idle else 20.0
    run = 0
    for i, p in enumerate(stop_pending_full):
        wall_i = stop_wall[i] if i < len(stop_wall) else 999.0
        if p >= plateau_pending_threshold and wall_i < 35.0:
            run += 1
            stop_pending_plateau_sec = max(stop_pending_plateau_sec, run * 2.0)
        else:
            run = 0
    # Healthy FPS with unfinished focus (holes=0 but light debt / dark mesh).
    healthy_unfinished = 0
    for i, r in enumerate(stop_tail):
        wall_r = float(r.get("wall_ms") or 999)
        pend = float(r.get("pending_light_focus") or 0)
        dark = float(r.get(sticky_key) or 0)
        miss = float(r.get(hole_key) or 0)
        unfinished = unfinished_stop[i] > 0 if i < len(unfinished_stop) else False
        if wall_r < 28.0 and (unfinished or pend >= 15 or dark >= 1 or miss >= 1):
            healthy_unfinished += 1
    healthy_unfinished_rate = (
        healthy_unfinished / len(stop_tail) if stop_tail else 1.0
    )

    stop_recovery_ok = (
        (post_stop_black_sticky_max or 0) <= 0.5
        and (post_stop_missing_max or 0) <= 0.5
        and post_stop_effective_holes_rate <= 0.05
        and ok_med(post_stop_pending_med, 5 if manual_idle else 15)
        and stop_pending_plateau_sec <= (60.0 if manual_idle else 8.0)
        and healthy_unfinished_rate <= 0.25
    )
    already_clean_stop = (
        ok_med(post_stop_pending_med, 2 if manual_idle else 15)
        and (post_stop_black_sticky_max or 0) <= 0.5
        and (post_stop_missing_max or 0) <= 0.5
        and post_stop_effective_holes_rate <= 0.05
        and stop_pending_plateau_sec <= (12.0 if manual_idle else 4.0)
    )
    pending_stop_limit = 5 if manual_idle else 15
    gates_stop = {
        "post_stop_pending_med_le_15": ok_med(post_stop_pending_med, pending_stop_limit),
        "post_stop_black_sticky_zero": post_stop_black_sticky_max is not None
        and post_stop_black_sticky_max <= 0.5,
        # Era16 TD-052: report-only in P0 (hard gate added in P1).
        "post_stop_visible_black_no_ticket_zero": (
            post_stop_visible_black_no_ticket_max is None
            or post_stop_visible_black_no_ticket_max <= 0.5
        ),
        # Era17: report-only in P0 (hard stalled gate in P1).
        "post_stop_visible_black_stalled_zero": (
            post_stop_visible_black_stalled_max is None
            or post_stop_visible_black_stalled_max <= 0.5
        ),
        "post_stop_missing_zero": post_stop_missing_max is not None
        and post_stop_missing_max <= 0.5,
        "post_stop_effective_holes_zero": post_stop_effective_holes_rate <= 0.05,
        "post_stop_pending_falling": already_clean_stop
        or (
            stop_pending_delta is not None and stop_pending_delta <= -3.0
        ),
        "post_stop_relight_active": already_clean_stop
        or (
            post_stop_relight_med is not None and post_stop_relight_med > 0.5
        ),
        "post_stop_pending_not_plateau_8s": stop_pending_plateau_sec
        <= (60.0 if manual_idle else 8.0),
        "post_stop_healthy_fps_not_unfinished": healthy_unfinished_rate <= 0.25,
        "post_stop_not_ready_falling": (
            (stop_not_ready_delta is not None and stop_not_ready_delta <= -8.0)
            or (
                post_stop_not_ready_end is not None
                and post_stop_not_ready_end <= 28.0
            )
        ),
        "post_stop_focus_dirty_falling": (
            focus_dirty_tail == []
            or (
                stop_focus_dirty_delta is not None
                and stop_focus_dirty_delta <= -8.0
            )
            or (
                post_stop_focus_dirty_end is not None
                and post_stop_focus_dirty_end <= 16.0
            )
        ),
    }
    gates_stop_pass_count = sum(1 for v in gates_stop.values() if v)
    passed = all(gates.values()) and all(gates_stop.values())

    soft = {
        "pending_trend_rising_while_traveling": pending_trend_rising,
        "black_proxy_rate": black_proxy_rate,
        "black_proxy_soft_fail": black_proxy_rate >= 0.25,
        "holes_rate_raw": holes_rate,
        "mesh_async_stuck_sec": mesh_async_stuck_sec,
        "cold_relight_holes_sec": cold_relight_holes_sec,
        # Era18 P0 report-only (hard floors land in P1/P2).
        "vb_without_pending_light_focus_sec": vb_without_pending_light_focus_sec,
        "relight_drain_near_zero_while_vb_sec": relight_drain_near_zero_while_vb_sec,
        "softdefer_capture_zero_while_vb_sec": softdefer_capture_zero_while_vb_sec,
        "heal_on_hot_sec": heal_on_hot_sec,
        "heal_on_hot_soft_fail": heal_on_hot_sec >= 20.0,
        "soft_defer_empty_stuck_sec": soft_defer_empty_stuck_sec,
        "soft_defer_empty_stuck_soft_fail": soft_defer_empty_stuck_sec >= 20.0,
        "mesh_discarded_late_delta_cruise": mesh_discarded_late_delta_cruise,
        "vb_progress_without_dark_clear_sec": vb_progress_without_dark_clear_sec,
        "miss_cy_gt1_frac": miss_cy_gt1_frac,
        "enter_app_update_max": enter_app_update_max,
        "enter_app_update_soft_fail": (
            enter_app_update_max is not None and enter_app_update_max > 200.0
        ),
        "vb_without_pending_light_focus_soft_fail": (
            vb_without_pending_light_focus_sec >= 30.0
        ),
        "relight_drain_dead_while_vb_soft_fail": (
            relight_drain_near_zero_while_vb_sec >= 30.0
        ),
        "softdefer_capture_dead_while_vb_soft_fail": (
            softdefer_capture_zero_while_vb_sec >= 30.0
        ),
        "relight_fifo_stuck_soft_fail": relight_fifo_stuck_soft_fail,
        "relight_false_clear_soft_fail": relight_false_clear_soft_fail,
        "stable_holes_soft_fail": stable_holes_soft_fail,
        "black_sticky_flicker_soft_fail": black_sticky_flicker_soft_fail,
        "visual_instability_soft_fail": visual_instability_soft_fail,
        "pending_partial_capture_soft_fail": pending_partial_capture_soft_fail,
        "cruise_relight_completed_med": cruise_relight_completed_med,
        "cruise_fifo_dropped_delta": cruise_fifo_dropped_delta,
        "cruise_false_clear_delta": cruise_false_clear_delta,
        "gates_stop": gates_stop,
        "stop_segment_periods": len(stop_segment),
        "stop_pending_delta": stop_pending_delta,
        "stop_not_ready_delta": stop_not_ready_delta,
        "stop_focus_dirty_delta": stop_focus_dirty_delta,
        "stop_mesh_apply_stale_delta": stop_mesh_apply_stale_delta,
        "stop_mesh_discarded_late_delta": stop_mesh_discarded_late_delta,
        "stop_recovery_ok": stop_recovery_ok,
        "dominant_spike_class": dominant_spike_class,
        "dominant_heavy_spike_class": dominant_heavy_spike_class,
        "spike_bucket_counts": spike_buckets,
        "heavy_spike_bucket_counts": heavy_bucket_counts,
        "spike_max_world_extra": spike_max_world_extra,
        "spike_world_extra_dominant_rate": spike_world_extra_dominant_rate,
        "soft_world_extra_ok": spike_max_world_extra <= 600.0
        and spike_world_extra_dominant_rate <= 0.35,
    }

    parity_vs_manual = None
    parity_within_2x = None
    if baseline_manual and baseline_manual.is_file():
        try:
            base = json.loads(baseline_manual.read_text(encoding="utf-8"))
            bm = base.get("metrics") or {}
            cur_m = {
                "fly_void_near_max": fly_void_near_max,
                "effective_holes_rate": effective_holes_rate,
                "wall_ms_fly_med": wall_fly_med,
                "chunk_count_end": chunk_count_end,
                "cruise_pending_med": cruise_pending_med,
                "cruise_fifo_med": cruise_fifo_med,
                "cruise_unlit_med": cruise_unlit_med,
                "pending_light_focus_med": median(pending_f),
                "chunk_meshed_unlit_med": chunk_meshed_unlit_med,
            }
            parity_vs_manual = {}
            for k, cur in cur_m.items():
                base_v = bm.get(k)
                if cur is None or base_v is None:
                    parity_vs_manual[k] = None
                elif float(base_v) == 0.0:
                    parity_vs_manual[k] = 1.0 if float(cur) == 0.0 else None
                else:
                    parity_vs_manual[k] = float(cur) / float(base_v)
            # Era38: soft fail if cruise pending/fifo/unlit > 2× manual.
            def _over_2x(key: str) -> bool:
                ratio = parity_vs_manual.get(key) if parity_vs_manual else None
                return ratio is not None and float(ratio) > 2.0

            parity_within_2x = not (
                _over_2x("cruise_pending_med")
                or _over_2x("cruise_fifo_med")
                or _over_2x("cruise_unlit_med")
                or _over_2x("pending_light_focus_med")
                or _over_2x("chunk_meshed_unlit_med")
            )
        except (json.JSONDecodeError, OSError, TypeError, ValueError):
            parity_vs_manual = None
            parity_within_2x = None
    soft["parity_within_2x"] = parity_within_2x
    soft["parity_within_2x_soft_fail"] = parity_within_2x is False
    out = {
        "perf_jsonl": str(path),
        "periods": len(periods),
        "steady_periods": len(steady),
        "spikes": len(spikes),
        "hole_key": hole_key,
        "unfinished_key": unfinished_key,
        "metrics": {
            "holes_rate": holes_rate,
            "effective_holes_rate": effective_holes_rate,
            "effective_holes_blink_rate": effective_holes_blink_rate,
            "effective_holes_blink_transitions": effective_holes_blink_transitions,
            "effective_holes_longest_hole_run": effective_holes_longest_hole_run,
            "effective_holes_longest_clear_run": effective_holes_longest_clear_run,
            "visible_black_blink_rate": visible_black_blink_rate,
            "visible_black_blink_transitions": visible_black_blink_transitions,
            "visible_black_longest_run": visible_black_longest_run,
            "black_sticky_blink_rate": black_sticky_blink_rate,
            "black_sticky_blink_transitions": black_sticky_blink_transitions,
            "black_sticky_longest_run": black_sticky_longest_run,
            "visual_instability_blink_rate": visual_instability_blink_rate,
            "visual_instability_blink_transitions": visual_instability_blink_transitions,
            "visual_instability_longest_bad_run": visual_instability_longest_bad_run,
            "relight_capture_partial_rate": relight_capture_partial_rate,
            "relight_apply_partial_frames": relight_apply_partial_frames,
            "relight_apply_final_frames": relight_apply_final_frames,
            "pending_partial_capture_sec": pending_partial_capture_sec,
            "relight_deferred_far_pending_max": deferred_far_pending_max,
            "mesh_async_stuck_sec": mesh_async_stuck_sec,
            "dirty_med": median(dirty),
            "dirty_max": max(dirty) if dirty else None,
            "pending_light_focus_med": median(pending_f),
            "red_rate": red_rate,
            "wall_ms_med": median(wall),
            "wall_ms_fly_med": wall_fly_med,
            "fly_void_near_max": fly_void_near_max,
            "fly_visible_black_max": fly_visible_black_max,
            "void_peak_period_idx": void_peak_period_idx,
            "void_drain_rate": void_drain_rate,
            "emerge_spike_frac": emerge_spike_frac,
            "fly_fluid_map_cpu_max": fly_fluid_map_cpu_max,
            "fly_frontier_pressure_frac": fly_frontier_pressure_frac,
            "chunk_count_end": chunk_count_end,
            "enter_void_near_max": enter_void_near_max,
            "enter_unfinished_max": enter_unfinished_max,
            "enter_pending_max": enter_pending_max,
            "enter_softdefer_empty_max": enter_softdefer_empty_max,
            "enter_unlit_max": enter_unlit_max,
            "cruise_pending_med": cruise_pending_med,
            "cruise_fifo_med": cruise_fifo_med,
            "cruise_unlit_med": cruise_unlit_med,
            "cruise_softdefer_empty_med": cruise_softdefer_empty_med,
            "cruise_relight_completed_med": cruise_relight_completed_med,
            "cruise_schedule_ok_med": cruise_schedule_ok_med,
            "cruise_capture_retarget_med": cruise_capture_retarget_med,
            "cruise_witness_pin_age_med": cruise_witness_pin_age_med,
            "cruise_relight_completed_spike_med": cruise_relight_completed_spike_med,
            "cruise_relight_completed_throughput": cruise_relight_completed_throughput,
            "unfinished_visual": unfinished_visual_med,
            "visible_black_focus_n": visible_black_focus_med,
            "stream_ms": stream_ms_med,
            "dark_face_stale_near_n": dark_face_stale_near_med,
            "dirty_ghost_n": dirty_ghost_n,
            "cruise_fifo_dropped_delta": cruise_fifo_dropped_delta,
            "cruise_false_clear_delta": cruise_false_clear_delta,
            "mesh_sync_fly_med": mesh_sync_fly_med,
            "wall_ms_no_holes_med": wall_ms_no_holes_med,
            "dirty_med_no_holes": dirty_med_no_holes,
            "mesh_async_med_no_holes": mesh_async_med_no_holes,
            "spike_count": spike_count,
            "spike_max_wall": spike_max_wall,
            "spike_max_wall_holes": spike_max_wall_holes,
            "spike_max_world_extra": spike_max_world_extra,
            "spike_world_extra_dominant_rate": spike_world_extra_dominant_rate,
            "dominant_spike_class": dominant_spike_class,
            "dominant_heavy_spike_class": dominant_heavy_spike_class,
            "world_extra_fly_med": median(world_extra_fly),
            "world_extra_fly_p95": p95(world_extra_fly),
            "world_extra_fly_max": max(world_extra_fly) if world_extra_fly else None,
            "tick_env_fly_med": median(tick_env_fly),
            "tick_env_fly_p95": p95(tick_env_fly),
            "tick_env_fly_max": max(tick_env_fly) if tick_env_fly else None,
            "block_input_fly_med": median(block_input_fly),
            "block_input_fly_p95": p95(block_input_fly),
            "block_input_fly_max": max(block_input_fly) if block_input_fly else None,
            "world_extra_stop_med": median(world_extra_stop),
            "tick_env_stop_med": median(tick_env_stop),
            "block_input_stop_med": median(block_input_stop),
            "break_complete_sum": break_complete_sum,
            "break_inflight_race_sum": break_race_sum,
            "break_dark_face_sum": break_dark_sum,
            "mesh_async_med": median(mesh_async),
            "mesh_async_med_when_dirty": mesh_async_med_when_dirty,
            "post_load_ring_idle_max": post_load_ring_idle_max,
            "unfinished_idle_max": unfinished_idle_max,
            "softdefer_capture_ticks_idle": softdefer_capture_ticks_idle,
            "pending_light_idle_delta": pending_light_idle_delta,
            "pending_light_idle_end": pending_light_idle_end,
            "stop_dark_face_near_end": stop_dark_face_near_end,
            "stop_dark_face_near_max": stop_dark_face_near_max,
            "stop_dark_face_stale_near_end": stop_dark_face_stale_near_end,
            "stop_dark_face_void_near_end": stop_dark_face_void_near_end,
            "stuck_async_holes_sec": stuck_async_holes_sec,
            "cold_relight_holes_sec": cold_relight_holes_sec,
            "vb_without_pending_light_focus_sec": vb_without_pending_light_focus_sec,
            "relight_drain_near_zero_while_vb_sec": relight_drain_near_zero_while_vb_sec,
            "softdefer_capture_zero_while_vb_sec": softdefer_capture_zero_while_vb_sec,
            "heal_on_hot_sec": heal_on_hot_sec,
            "soft_defer_empty_stuck_sec": soft_defer_empty_stuck_sec,
            "mesh_discarded_late_delta_cruise": mesh_discarded_late_delta_cruise,
            "vb_progress_without_dark_clear_sec": vb_progress_without_dark_clear_sec,
            "miss_cy_gt1_frac": miss_cy_gt1_frac,
            "enter_app_update_max": enter_app_update_max,
            "dirty_high_sec": dirty_high_sec,
            "miss_stuck_max_run_sec": miss_stuck_max_run_sec,
            "miss_stuck_frames": miss_stuck_frames,
            "miss_stuck_cx": miss_stuck_cx,
            "miss_stuck_cz": miss_stuck_cz,
            "miss_end": miss_end,
            "miss_end_stop": miss_end_stop,
            "post_stop_focus_miss_max": post_stop_focus_miss_max,
            "post_stop_miss_low_cy_n": post_stop_miss_low_cy_n,
            "post_stop_underfeet_ok_miss_n": post_stop_underfeet_ok_miss_n,
            "tail_focus_miss_max": tail_focus_miss_max,
            "tail_miss_low_cy_n": tail_miss_low_cy_n,
            "tail_underfeet_ok_miss_n": tail_underfeet_ok_miss_n,
            "opaque_idle_churn_max": opaque_idle_churn_max,
            "nh_no_miss_rate": nh_no_miss_rate,
            "chunks_traveled": chunks_traveled,
            "focus_start": focus_pts[0] if focus_pts else None,
            "focus_end": focus_pts[-1] if focus_pts else None,
            "gates_pass_count": gates_pass_count,
            "gates_total": len(gates),
            "post_stop_pending_med": post_stop_pending_med,
            "post_stop_black_sticky_max": post_stop_black_sticky_max,
            "post_stop_visible_black_max": post_stop_visible_black_max,
            "post_stop_visible_black_no_ticket_max": post_stop_visible_black_no_ticket_max,
            "post_stop_visible_black_progress_min": post_stop_visible_black_progress_min,
            "post_stop_visible_black_stalled_max": post_stop_visible_black_stalled_max,
            "post_stop_missing_max": post_stop_missing_max,
            "post_stop_effective_holes_rate": post_stop_effective_holes_rate,
            "post_stop_effective_holes_blink_rate": post_stop_effective_holes_blink_rate,
            "post_stop_effective_holes_blink_transitions": post_stop_effective_holes_blink_transitions,
            "post_stop_longest_hole_run": post_stop_longest_hole_run,
            "post_stop_longest_clear_run": post_stop_longest_clear_run,
            "gates_stop_pass_count": gates_stop_pass_count,
            "gates_stop_total": len(gates_stop),
            "post_stop_relight_med": post_stop_relight_med,
            "post_stop_not_ready_med": post_stop_not_ready_med,
            "post_stop_not_ready_end": post_stop_not_ready_end,
            "stop_not_ready_delta": stop_not_ready_delta,
            "stop_pending_delta": stop_pending_delta,
            "post_stop_focus_dirty_med": post_stop_focus_dirty_med,
            "post_stop_focus_dirty_end": post_stop_focus_dirty_end,
            "stop_focus_dirty_delta": stop_focus_dirty_delta,
            "stop_mesh_apply_stale_delta": stop_mesh_apply_stale_delta,
            "stop_mesh_discarded_late_delta": stop_mesh_discarded_late_delta,
            "post_stop_unfinished_ahead_med": (
                median(col(stop_tail, "focus_unfinished_ahead"))
                if stop_tail
                else None
            ),
            "post_stop_unfinished_behind_med": (
                median(col(stop_tail, "focus_unfinished_behind"))
                if stop_tail
                else None
            ),
            "stop_segment_periods": len(stop_segment),
            "stop_tail_periods": len(stop_tail),
            "stop_pending_plateau_sec": stop_pending_plateau_sec,
            "stop_wall_med": stop_wall_med,
            "healthy_unfinished_rate": healthy_unfinished_rate,
            "manual_idle": manual_idle,
            "contaminated_idle": 1.0 if contaminated_idle else 0.0,
            "place_complete_sum": place_complete_sum,
            "edit_immediate_max": edit_immediate_max,
            "physics_block_steady_p95": physics_block_steady_p95,
            "calm_stop_periods": calm_stop_metrics["n"],
            "recovery_stop_periods": recovery_stop_metrics["n"],
            "contaminated_stop_periods": contaminated_stop_metrics["n"],
            "calm_stop_wall_med": calm_stop_wall_med,
            "calm_stop_wall_p95": calm_stop_metrics["wall_p95"],
            "calm_stop_emerge_med": calm_stop_emerge_med,
            "calm_stop_stream_med": calm_stop_stream_med,
            "calm_stop_phys_med": calm_stop_metrics["phys_med"],
            "recovery_stop_wall_med": recovery_stop_metrics["wall_med"],
            "contaminated_stop_wall_med": contaminated_stop_metrics["wall_med"],
            "stop_emerge_med": stop_segment_metrics["emerge_med"],
            "stop_stream_med": stop_segment_metrics["stream_med"],
            "stop_phys_med": stop_segment_metrics["phys_med"],
            "stop_relight_med": stop_segment_metrics["relight_med"],
            "stop_mesh_immediate_med": stop_segment_metrics["mesh_immediate_med"],
            "stop_mesh_dirty_tick_med": stop_segment_metrics["mesh_dirty_tick_med"],
            "stop_mesh_prep_med": stop_segment_metrics["mesh_prep_med"],
            "stop_mesh_prep_missing_med": stop_segment_metrics[
                "mesh_prep_missing_med"
            ],
            "stop_mesh_prep_unfinished_med": stop_segment_metrics[
                "mesh_prep_unfinished_med"
            ],
            "stop_mesh_prep_sticky_med": stop_segment_metrics[
                "mesh_prep_sticky_med"
            ],
            "stop_mesh_prep_drop_dirty_med": stop_segment_metrics[
                "mesh_prep_drop_dirty_med"
            ],
            "stop_mesh_prep_other_med": stop_segment_metrics[
                "mesh_prep_other_med"
            ],
            "physics_block_ms_p95": physics_block_ms_p95,
            "backend_store_mode": backend_store_mode,
            "backend_mesher_mode": backend_mesher_mode,
            "backend_cull_mode": backend_cull_mode,
            "backend_store_mdi": backend_store_mdi,
            "backend_mesher_gpu": backend_mesher_gpu,
            "backend_cull_gpu": backend_cull_gpu,
            "gpu_draw_cmds_med": gpu_draw_cmds_med,
            "gpu_cull_ms_med": gpu_cull_ms_med,
            "vertex_pool_fill_med": vertex_pool_fill_med,
            "gpu_cull_indirect_med": gpu_cull_indirect_med,
            "gpu_mesh_vbo_dispatch_med": gpu_mesh_vbo_dispatch_med,
            "gpu_light_seed_apply_med": gpu_light_seed_apply_med,
            "gpu_mask_readback_med": gpu_mask_readback_med,
            "gpu_blocklight_flood_med": gpu_blocklight_flood_med,
            "gpu_fluid_readback_med": gpu_fluid_readback_med,
            "gpu_light_readback_med": gpu_light_readback_med,
            "gpu_opaque_emit_gpu_med": gpu_opaque_emit_gpu_med,
            "gpu_opaque_emit_gpu_max": gpu_opaque_emit_gpu_max,
            "gpu_transparent_sort_gpu_med": gpu_transparent_sort_gpu_med,
            "gpu_transparent_sort_gpu_max": gpu_transparent_sort_gpu_max,
            "gpu_fallback_rate": gpu_fallback_rate,
            "gpu_fluid_scan_on_med": gpu_fluid_scan_on_med,
            "backend_fluid_mode": backend_fluid_mode,
            "backend_lighting_mode": backend_lighting_mode,
            "backend_lighting_flat": backend_lighting_flat,
            "backend_lighting_full": backend_lighting_full,
            "caps_has_compute": caps_has_compute,
            "caps_has_ssbo": caps_has_ssbo_med,
            "caps_probe_completed": caps_probe_completed,
            "android_gpu_user_pref": android_gpu_user_pref,
            "android_gpu_effective": android_gpu_effective,
            "android_gpu_deny_reason": android_gpu_deny_reason,
            "gl_version": gl_version,
            "gl_renderer": gl_renderer,
            "opaque_cmd_total_med": opaque_cmd_total_med,
            "opaque_cmd_on_med": opaque_cmd_on_med,
            "opaque_culled_frac_med": opaque_culled_frac_med,
            "cross_batch_count_med": cross_batch_count_med,
            "cpu_aabb_would_on_med": cpu_aabb_would_on_med,
            "edit_immediate_n_med": edit_immediate_n_med,
            "edit_dirty_n_med": edit_dirty_n_med,
            "edit_neighbor_pending_frames_med": edit_neighbor_pending_frames_med,
            "pool_unsync_uploads_med": pool_unsync_uploads_med,
            "pool_fence_wait_ms_med": pool_fence_wait_ms_med,
            "chunk_meshed_culled0_med": chunk_meshed_culled0_med,
            "chunk_meshed_unlit_med": chunk_meshed_unlit_med,
            "chunk_not_ready_med": chunk_not_ready_med,
            "opaque_on_min": opaque_on_min,
            "blue_screen_suspect": blue_screen_suspect,
        },
        "gates": gates,
        "gates_stop": gates_stop,
        "soft": soft,
        "pass": passed,
    }
    if parity_vs_manual is not None:
        out["parity_vs_manual"] = parity_vs_manual
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("perf_jsonl", type=Path)
    ap.add_argument("--warmup-sec", type=float, default=5.0)
    ap.add_argument("--stop-tail-periods", type=int, default=5)
    ap.add_argument(
        "--manual-idle",
        action="store_true",
        help="use longest focus plateau + stricter pending stop gates",
    )
    ap.add_argument("--report", type=Path, default=None)
    ap.add_argument(
        "--baseline-manual",
        type=Path,
        default=None,
        help="optional manual analyze JSON for parity ratios",
    )
    args = ap.parse_args()
    if not args.perf_jsonl.is_file():
        print(f"FAIL: missing {args.perf_jsonl}", file=sys.stderr)
        return 2
    result = analyze(
        args.perf_jsonl,
        args.warmup_sec,
        args.stop_tail_periods,
        manual_idle=args.manual_idle,
        baseline_manual=args.baseline_manual,
    )
    text = json.dumps(result, indent=2)
    print(text)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + "\n", encoding="utf-8")
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
