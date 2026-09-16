#!/usr/bin/env python3
"""Build (optional), run Cubatarium --flight-sim, analyze perf gates."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin"
EXE = BIN / "Cubatarium.exe"
DEBUG_EXE = ROOT / "build" / "desktop-msvc" / "Debug" / "Cubatarium.exe"
ANALYZE = Path(__file__).with_name("flight_sim_analyze.py")
DIAG = Path(__file__).with_name("flight_sim_diag.py")
PHASE_HISTORY = BIN / "flight_sim_phase_history.jsonl"

MANUAL_100645_PROXY_CLASS = {
    "focus_missing_mesh_med_min": 1.0,
    "miss_stuck_run_frames_tail_max_min": 100.0,
    "fog_pull_in_rd_fly_med_min": 3.0,
    "visible_black_focus_fly_med_min": 40.0,
    # Manual 122212/100645 eye stays ~50–58; hold-space proxy climbed to ~300.
    "player_y_late_med_min": 45.0,
    "player_y_late_med_max": 70.0,
    "player_y_delta_abs_max": 25.0,
    # Manual west (7,3)→(−3,3); stuck spawn fails product class.
    "focus_west_delta_cx_min": 3.0,
}

# Dual-lane upper stop-line (wall-diet / n01-rework). Adequacy alone is not merge-green.
DUAL_LANE_STOP_LINE = {
    "vb_fly_med_max": 84.5,
    "stale_vl_fly_med_max": 84.5,
    "unlit_max_cold": 15.0,
    "unlit_max_warm": 19.0,
    "mid_fully_dark_stalled_med_max": 5.0,
}

# Eye-proxy thrash stop-line (N08). Mid-corridor SoT (same band as dual-lane stalled).
# Fly-only med greenwashed autofly 095545 (fly stale=2, mid=17). Four merge signals:
# adequacy / dual-lane / eye_proxy / operator eye. Adequacy alone is not merge-green.
EYE_PROXY_STOP_LINE = {
    # Post-N01: align with thrash ≤102527 class (mid stale≤8), not pre-fix 6.
    "mesh_apply_stale_visual_mid_med_max": 8.0,
    "mesh_apply_stale_visual_delta_med_max": 1.5,
    "effective_holes_blink_rate_max": 0.05,
    "transparent_cmd_reorder_mid_med_max": 1.0,
    # N01 retain-storm: incomplete material mid med ≪ 134038 baseline (~40).
    "publication_incomplete_material_mid_med_max": 5.0,
    # Audit 2026-09-16 R10: absolute hole count is a gate (blink alone is not enough).
    "near_focus_holes_mid_med_max": 0.0,
    "visual_holes_mid_med_max": 0.0,
}

# Manual west (7,3)→(−3,3). Autofly that stops at cx≈2 is UNTESTED, not PASS coverage.
WEST_COVERAGE_FOCUS_CX_MAX = -3.0


def newest_perf(after_ts: float) -> Path | None:
    logs = BIN / "logs"
    if not logs.is_dir():
        return None
    cands = [
        p
        for p in logs.glob("perf_*.jsonl")
        if p.stat().st_mtime >= after_ts - 1.0
    ]
    if not cands:
        return None
    return max(cands, key=lambda p: p.stat().st_mtime)


def resolve_exe() -> Path:
    """Prefer the freshest local desktop build when available."""
    if DEBUG_EXE.is_file():
        if not EXE.is_file():
            return DEBUG_EXE
        try:
            if DEBUG_EXE.stat().st_mtime >= EXE.stat().st_mtime:
                return DEBUG_EXE
        except OSError:
            return DEBUG_EXE
    return EXE


def load_best(path: Path) -> dict | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return None


def compute_product_174657_proxy_adequacy(perf_path: Path) -> dict:
    """Return whether product-174657 reproduces the west miss/stuck class."""
    try:
        rows = []
        for line in perf_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("{"):
                continue
            row = json.loads(line)
            if row.get("kind") == "period":
                rows.append(row)
        fly = []
        for row in rows:
            try:
                if float(row.get("movement_speed") or 0) > 2.0:
                    fly.append(row)
            except (TypeError, ValueError):
                continue
        use = fly if fly else rows
        tail = use[-max(1, len(use) // 3) :] if use else []

        def values(key: str, subset: list[dict]) -> list[float]:
            out: list[float] = []
            for row in subset:
                try:
                    val = row.get(key)
                    if val is not None:
                        out.append(float(val))
                except (TypeError, ValueError):
                    continue
            return out

        def median(key: str, subset: list[dict]) -> float | None:
            xs = values(key, subset)
            if not xs:
                return None
            xs = sorted(xs)
            mid = len(xs) // 2
            if len(xs) % 2:
                return xs[mid]
            return (xs[mid - 1] + xs[mid]) / 2.0

        def max_value(key: str, subset: list[dict]) -> float | None:
            xs = values(key, subset)
            return max(xs) if xs else None
    except Exception as exc:  # pragma: no cover - best-effort reporting
        return {
            "adequacy_pass": False,
            "adequacy_fails": [f"adequacy_analyze_failed:{exc}"],
        }

    ys = values("player_y", use)
    y_early = None
    y_late = None
    y_delta = None
    if ys:
        n = len(ys)
        early_n = max(1, n // 10)

        def _med_list(xs: list[float]) -> float:
            xs = sorted(xs)
            mid = len(xs) // 2
            if len(xs) % 2:
                return xs[mid]
            return (xs[mid - 1] + xs[mid]) / 2.0

        y_early = _med_list(ys[:early_n])
        y_late = _med_list(ys[-early_n:])
        y_delta = y_late - y_early

    # Prefer all periods for path (movement_speed often 0 even while traveling).
    fcx_all = values("focus_cx", rows)
    focus_west_delta = None
    if len(fcx_all) >= 2:
        focus_west_delta = float(fcx_all[0]) - float(fcx_all[-1])

    metrics = {
        "early_vb_med": median("visible_black_focus_n", use[: min(5, len(use))]),
        "focus_missing_mesh_med": median("focus_missing_mesh", use),
        "miss_stuck_run_frames_tail_max": max_value("miss_stuck_run_frames", tail),
        "fog_pull_in_rd_fly_med": median("fog_pull_in_rd", use),
        "visible_black_focus_fly_med": median("visible_black_focus_n", use),
        "gpu_kick_fly_med": median("gpu_kick_n", tail if tail else use),
        "ok_remesh_fly_med": median("mesh_dirty_schedule_ok_remesh_n", use),
        "player_y_early_med": y_early,
        "player_y_late_med": y_late,
        "player_y_delta": y_delta,
        "focus_west_delta_cx": focus_west_delta,
        "gpu_kick_post_drain_fly_max": max_value("gpu_kick_post_drain_n", use),
    }
    fails: list[str] = []
    if (
        metrics["focus_missing_mesh_med"] is None
        or float(metrics["focus_missing_mesh_med"])
        < MANUAL_100645_PROXY_CLASS["focus_missing_mesh_med_min"]
    ):
        fails.append("focus_missing_too_low")
    if (
        metrics["miss_stuck_run_frames_tail_max"] is None
        or float(metrics["miss_stuck_run_frames_tail_max"])
        < MANUAL_100645_PROXY_CLASS["miss_stuck_run_frames_tail_max_min"]
    ):
        fails.append("miss_stuck_too_low")
    if (
        metrics["fog_pull_in_rd_fly_med"] is None
        or float(metrics["fog_pull_in_rd_fly_med"])
        < MANUAL_100645_PROXY_CLASS["fog_pull_in_rd_fly_med_min"]
    ):
        fails.append("fog_rd_collapsed")
    if (
        metrics["visible_black_focus_fly_med"] is None
        or float(metrics["visible_black_focus_fly_med"])
        < MANUAL_100645_PROXY_CLASS["visible_black_focus_fly_med_min"]
    ):
        fails.append("vb_too_low_for_product_class")
    if (
        y_late is None
        or float(y_late) < MANUAL_100645_PROXY_CLASS["player_y_late_med_min"]
        or float(y_late) > MANUAL_100645_PROXY_CLASS["player_y_late_med_max"]
    ):
        fails.append("altitude_out_of_corridor")
    if (
        y_delta is not None
        and abs(float(y_delta))
        > MANUAL_100645_PROXY_CLASS["player_y_delta_abs_max"]
    ):
        fails.append("altitude_climb")
    if (
        focus_west_delta is None
        or float(focus_west_delta)
        < MANUAL_100645_PROXY_CLASS["focus_west_delta_cx_min"]
    ):
        fails.append("focus_not_west")
    metrics["adequacy_pass"] = len(fails) == 0
    metrics["adequacy_fails"] = fails
    return metrics


def compute_dual_lane_stop_line(
    perf_path: Path, *, warm: bool = False
) -> dict:
    """Upper-bound regress vs dual-lane S1/S3 class (not adequacy)."""
    try:
        rows = []
        for line in perf_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("{"):
                continue
            row = json.loads(line)
            if row.get("kind") == "period":
                rows.append(row)
        fly = []
        for row in rows:
            try:
                if float(row.get("movement_speed") or 0) > 2.0:
                    fly.append(row)
            except (TypeError, ValueError):
                continue
        use = fly if fly else rows

        def values(key: str) -> list[float]:
            out: list[float] = []
            for row in use:
                try:
                    val = row.get(key)
                    if val is not None:
                        out.append(float(val))
                except (TypeError, ValueError):
                    continue
            return out

        def median(xs: list[float]) -> float | None:
            if not xs:
                return None
            xs = sorted(xs)
            mid = len(xs) // 2
            if len(xs) % 2:
                return xs[mid]
            return (xs[mid - 1] + xs[mid]) / 2.0

        vb_xs = values("visible_black_focus_n")
        # Prefer draw-oracle stale VL; fall back to dark_face_stale_near proxy.
        stale_xs = values("draw_oracle_stale_vertex_light_n")
        if not stale_xs:
            stale_xs = values("dark_face_stale_near_n")
        unlit_xs = values("chunk_meshed_unlit")
        mid_third = use[len(use) // 3 : (2 * len(use) // 3)] if len(use) >= 3 else use
        mid_focus = [
            r
            for r in mid_third
            if r.get("focus_cx") is not None and 2.0 <= float(r["focus_cx"]) <= 5.0
        ]
        mid_subset = mid_focus if mid_focus else mid_third

        def mid_values(key: str) -> list[float]:
            out: list[float] = []
            for row in mid_subset:
                try:
                    val = row.get(key)
                    if val is not None:
                        out.append(float(val))
                except (TypeError, ValueError):
                    continue
            return out

        stalled_xs = mid_values("visible_black_fully_dark_stalled_n")
        metrics = {
            "vb_fly_med": median(vb_xs),
            "stale_vl_fly_med": median(stale_xs),
            "unlit_max": max(unlit_xs) if unlit_xs else None,
            "mid_fully_dark_stalled_med": median(stalled_xs),
            "warm": bool(warm),
        }
        fails: list[str] = []
        vb_med = metrics["vb_fly_med"]
        if vb_med is None or float(vb_med) > DUAL_LANE_STOP_LINE["vb_fly_med_max"]:
            fails.append("vb_fly_med_above_dual_lane")
        stale_med = metrics["stale_vl_fly_med"]
        if stale_med is None or float(stale_med) > DUAL_LANE_STOP_LINE[
            "stale_vl_fly_med_max"
        ]:
            fails.append("stale_vl_fly_med_above_dual_lane")
        unlit_cap = (
            DUAL_LANE_STOP_LINE["unlit_max_warm"]
            if warm
            else DUAL_LANE_STOP_LINE["unlit_max_cold"]
        )
        unlit_max = metrics["unlit_max"]
        if unlit_max is None or float(unlit_max) > unlit_cap:
            fails.append("unlit_max_above_dual_lane")
        stalled_med = metrics["mid_fully_dark_stalled_med"]
        if stalled_med is None or float(stalled_med) > DUAL_LANE_STOP_LINE[
            "mid_fully_dark_stalled_med_max"
        ]:
            fails.append("mid_fully_dark_stalled_above_stop_line")
        metrics["dual_lane_stop_line_pass"] = len(fails) == 0
        metrics["dual_lane_stop_line_fails"] = fails
        return metrics
    except Exception as exc:  # pragma: no cover
        return {
            "dual_lane_stop_line_pass": False,
            "dual_lane_stop_line_fails": [f"stop_line_analyze_failed:{exc}"],
        }


def compute_west_route_coverage(perf_path: Path) -> dict:
    """Full-west product coverage: min focus_cx must reach ≤−3. Else UNTESTED."""
    try:
        focus_cx: list[float] = []
        for line in perf_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("{"):
                continue
            row = json.loads(line)
            if row.get("kind") != "period":
                continue
            try:
                if row.get("focus_cx") is not None:
                    focus_cx.append(float(row["focus_cx"]))
            except (TypeError, ValueError):
                continue
        if not focus_cx:
            return {
                "west_route_coverage": "UNTESTED",
                "focus_cx_min": None,
                "focus_cx_max": None,
                "west_route_coverage_reason": "no_period_focus_cx",
            }
        cx_min = min(focus_cx)
        cx_max = max(focus_cx)
        covered = cx_min <= WEST_COVERAGE_FOCUS_CX_MAX
        return {
            "west_route_coverage": "COVERED" if covered else "UNTESTED",
            "focus_cx_min": cx_min,
            "focus_cx_max": cx_max,
            "west_route_coverage_reason": (
                "reached_cx_le_minus3" if covered else "did_not_reach_cx_minus3"
            ),
        }
    except Exception as exc:  # pragma: no cover
        return {
            "west_route_coverage": "UNTESTED",
            "focus_cx_min": None,
            "focus_cx_max": None,
            "west_route_coverage_reason": f"west_coverage_analyze_failed:{exc}",
        }


def compute_eye_proxy_stop_line(perf_path: Path) -> dict:
    """B4 thrash proxies on mid-corridor (not fly-only). Not adequacy / not pixels."""
    try:
        rows: list[dict] = []
        for line in perf_path.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line.startswith("{"):
                continue
            row = json.loads(line)
            if row.get("kind") == "period":
                rows.append(row)

        mid_third = (
            rows[len(rows) // 3 : (2 * len(rows)) // 3] if len(rows) >= 3 else rows
        )
        # Product mid-corridor is spatial focus_cx∈[2,5], not temporal mid_third.
        # Full-west fly (7→≤−3) shifts temporal mid past that band — search all periods.
        mid_focus: list[dict] = []
        for row in rows:
            try:
                cx = float(row.get("focus_cx") or 0)
            except (TypeError, ValueError):
                continue
            if 2.0 <= cx <= 5.0:
                mid_focus.append(row)
        fly: list[dict] = []
        for row in rows:
            try:
                if float(row.get("movement_speed") or 0) > 2.0:
                    fly.append(row)
            except (TypeError, ValueError):
                continue

        # Audit R10: do not silently PASS on mid_third/stop fallback as product coverage.
        coverage_status = "COVERED"
        if mid_focus:
            use = mid_focus
            segment = "mid_corridor"
            moving = [
                r
                for r in use
                if float(r.get("movement_speed") or 0) > 2.0
            ]
            if len(use) > 0 and len(moving) * 2 < len(use):
                coverage_status = "UNTESTED"
        elif mid_third:
            use = mid_third
            segment = "mid_third"
            coverage_status = "UNTESTED"
        else:
            use = fly if fly else rows
            segment = "fly_fallback"
            coverage_status = "UNTESTED"

        def values(key: str) -> list[float]:
            out: list[float] = []
            for row in use:
                try:
                    val = row.get(key)
                    if val is not None:
                        out.append(float(val))
                except (TypeError, ValueError):
                    continue
            return out

        def field_present(key: str) -> bool:
            return any(key in row and row.get(key) is not None for row in use)

        def median(xs: list[float]) -> float | None:
            if not xs:
                return None
            xs = sorted(xs)
            mid = len(xs) // 2
            if len(xs) % 2:
                return xs[mid]
            return (xs[mid - 1] + xs[mid]) / 2.0

        stale_xs = values("mesh_apply_stale_visual")
        if not stale_xs:
            stale_xs = values("mesh_apply_stale")
        deltas: list[float] = [
            abs(stale_xs[i] - stale_xs[i - 1]) for i in range(1, len(stale_xs))
        ]

        hole_flags: list[float] = []
        for row in use:
            # N08: blink = missing-mesh holes only. unfinished_visual is SoftDefer/
            # render-ready census and oscillates mid without near_focus_holes.
            holes = float(row.get("near_focus_holes") or 0) > 0 or float(
                row.get("visual_holes") or 0
            ) > 0
            hole_flags.append(1.0 if holes else 0.0)
        blink_transitions = 0
        for i in range(1, len(hole_flags)):
            if hole_flags[i] != hole_flags[i - 1]:
                blink_transitions += 1
        blink_rate = (
            blink_transitions / max(1, len(hole_flags) - 1)
            if len(hole_flags) >= 2
            else 0.0
        )

        holes_xs = values("near_focus_holes")
        visual_holes_xs = values("visual_holes")
        reorder_xs = values("transparent_cmd_reorder_n")
        incomplete_xs = values("publication_incomplete_material_n")
        oom_xs = values("publication_oom_retain_n")
        stale_med = median(stale_xs)
        holes_med = median(holes_xs)
        visual_holes_med = median(visual_holes_xs)
        delta_med = median(deltas)
        reorder_med = median(reorder_xs)
        incomplete_med = median(incomplete_xs)
        oom_med = median(oom_xs)
        west = compute_west_route_coverage(perf_path)

        # Alias *_fly_* = mid values for one release (readers / old reports).
        metrics = {
            "source": "period_jsonl",
            "perf_path": str(perf_path),
            "eye_proxy_segment": segment,
            "eye_proxy_coverage": coverage_status,
            "mesh_apply_stale_visual_mid_med": stale_med,
            "mesh_apply_stale_visual_fly_med": stale_med,
            "mesh_apply_stale_visual_delta_med": delta_med,
            "effective_holes_blink_rate": blink_rate,
            "transparent_cmd_reorder_mid_med": reorder_med,
            "transparent_cmd_reorder_fly_med": reorder_med,
            "near_focus_holes_mid_med": holes_med,
            "visual_holes_mid_med": visual_holes_med,
            "publication_incomplete_material_mid_med": incomplete_med,
            "publication_oom_retain_mid_med": oom_med,
            "rows_used": len(use),
            **west,
        }
        fails: list[str] = []
        # Fail-closed: missing required fields are UNTESTED, never PASS.
        if not field_present("near_focus_holes") and not field_present("visual_holes"):
            fails.append("missing_holes_fields_untested")
        if not field_present("publication_incomplete_material_n"):
            fails.append("missing_publication_incomplete_field_untested")
        if not stale_xs:
            fails.append("missing_stale_visual_field_untested")
        if stale_med is None or float(stale_med) > EYE_PROXY_STOP_LINE[
            "mesh_apply_stale_visual_mid_med_max"
        ]:
            fails.append("mesh_apply_stale_visual_mid_med_above_eye_proxy")
        if delta_med is None or float(delta_med) > EYE_PROXY_STOP_LINE[
            "mesh_apply_stale_visual_delta_med_max"
        ]:
            fails.append("mesh_apply_stale_visual_delta_med_above_eye_proxy")
        if float(blink_rate) > EYE_PROXY_STOP_LINE["effective_holes_blink_rate_max"]:
            fails.append("effective_holes_blink_rate_above_eye_proxy")
        if reorder_med is not None and float(reorder_med) > EYE_PROXY_STOP_LINE[
            "transparent_cmd_reorder_mid_med_max"
        ]:
            fails.append("transparent_cmd_reorder_mid_med_above_eye_proxy")
        if incomplete_med is not None and float(incomplete_med) > EYE_PROXY_STOP_LINE[
            "publication_incomplete_material_mid_med_max"
        ]:
            fails.append("incomplete_material_mid_med_above_eye_proxy")
        # Absolute hole counts: persistent holes must FAIL even when blink_rate=0.
        if holes_med is not None and float(holes_med) > EYE_PROXY_STOP_LINE[
            "near_focus_holes_mid_med_max"
        ]:
            fails.append("near_focus_holes_mid_med_above_eye_proxy")
        if visual_holes_med is not None and float(visual_holes_med) > EYE_PROXY_STOP_LINE[
            "visual_holes_mid_med_max"
        ]:
            fails.append("visual_holes_mid_med_above_eye_proxy")
        # Holes telem = missing mesh only; thrash with holes==0 is still a defect.
        if (
            (holes_med is None or float(holes_med) == 0.0)
            and (visual_holes_med is None or float(visual_holes_med) == 0.0)
            and stale_med is not None
            and float(stale_med)
            > EYE_PROXY_STOP_LINE["mesh_apply_stale_visual_mid_med_max"]
        ):
            fails.append("stale_visual_without_hole_counters")
        # Silent mid_third/fly_fallback must not look like a covered mid-corridor PASS.
        if segment in ("mid_third", "fly_fallback"):
            fails.append("eye_proxy_segment_coverage_untested")
        metrics["eye_proxy_stop_line_pass"] = len(fails) == 0
        metrics["eye_proxy_stop_line_fails"] = fails
        return metrics
    except Exception as exc:  # pragma: no cover
        return {
            "eye_proxy_stop_line_pass": False,
            "eye_proxy_stop_line_fails": [f"eye_proxy_analyze_failed:{exc}"],
            "eye_proxy_coverage": "UNTESTED",
            "west_route_coverage": "UNTESTED",
        }


def kill_cubatarium_orphans() -> int:
    """Force-kill any Cubatarium.exe trees. Returns number of taskkill attempts."""
    if sys.platform != "win32":
        return 0
    r = subprocess.run(
        ["taskkill", "/F", "/T", "/IM", "Cubatarium.exe"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return 0 if r.returncode == 128 else 1


def exe_writable(timeout_sec: float = 5.0) -> bool:
    """True if bin/Cubatarium.exe can be replaced (not locked)."""
    exe = resolve_exe()
    if not exe.is_file():
        return True
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            with open(exe, "ab"):
                return True
        except OSError:
            time.sleep(0.25)
    return False


def preflight_cleanup() -> None:
    kill_cubatarium_orphans()
    time.sleep(0.3)
    kill_cubatarium_orphans()
    if not exe_writable(5.0):
        raise SystemExit(
            "FAIL: Cubatarium.exe still locked after kill — abort before build/sim"
        )


def kill_process_tree(pid: int) -> None:
    if sys.platform == "win32":
        subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        try:
            import os
            import signal

            os.killpg(os.getpgid(pid), signal.SIGKILL)
        except (ProcessLookupError, PermissionError, OSError):
            pass


def run_with_timeout(cmd: list[str], cwd: Path, timeout_sec: float) -> int:
    proc = subprocess.Popen(cmd, cwd=str(cwd))
    try:
        return proc.wait(timeout=timeout_sec)
    except subprocess.TimeoutExpired:
        print(
            f"WARN: flight-sim hung after {timeout_sec:.0f}s, killing pid={proc.pid}",
            flush=True,
        )
        kill_process_tree(proc.pid)
        kill_cubatarium_orphans()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        return 124


def append_phase_history(entry: dict) -> None:
    BIN.mkdir(parents=True, exist_ok=True)
    with PHASE_HISTORY.open("a", encoding="utf-8") as f:
        f.write(json.dumps(entry, ensure_ascii=False) + "\n")


def gates_pass_count(result: dict) -> int:
    g = result.get("gates") or {}
    return sum(1 for v in g.values() if v)


def gates_stop_pass_count(result: dict) -> int:
    g = result.get("gates_stop") or {}
    return sum(1 for v in g.values() if v)


def is_better(result: dict, best: dict | None) -> bool:
    """True only when cruise gates improve vs baseline (never on first 4/8 run)."""
    rc = gates_pass_count(result)
    rsc = gates_stop_pass_count(result)
    if rc < 6 or rsc < 4:
        return False
    if best is None:
        return result.get("pass", False)
    bc = gates_pass_count(best)
    if rc > bc:
        return True
    if rc < bc:
        return False
    rsc = gates_stop_pass_count(result)
    bsc = gates_stop_pass_count(best)
    if rsc > bsc:
        return True
    if rsc < bsc:
        return False
    rm = result.get("metrics") or {}
    bm = best.get("metrics") or {}
    rp = rm.get("pending_light_focus_med")
    bp = bm.get("pending_light_focus_med")
    if rp is not None and bp is not None and rp < bp - 4.0:
        return True
    rw = rm.get("wall_ms_med")
    bw = bm.get("wall_ms_med")
    if rw is not None and bw is not None and rw < bw - 2.0:
        return True
    return False


def annotate_report_hang(report: Path, hang_killed: bool, process_rc: int) -> None:
    """Backward-compatible wrapper; prefer annotate_report_run."""
    annotate_report_run(report, hang_killed, process_rc, perf_jsonl=None)


def annotate_report_run(
    report: Path,
    hang_killed: bool,
    process_rc: int,
    perf_jsonl: Path | None,
    info_log: Path | None = None,
) -> None:
    if not report.is_file():
        return
    if DIAG.is_file():
        import importlib.util

        spec = importlib.util.spec_from_file_location("flight_sim_diag", DIAG)
        if spec and spec.loader:
            mod = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(mod)
            mod.annotate_report_run(
                report, process_rc, hang_killed, perf_jsonl, info_log
            )
            return
    annotate_report_hang_legacy(report, hang_killed, process_rc)


def annotate_report_hang_legacy(report: Path, hang_killed: bool, process_rc: int) -> None:
    if not report.is_file():
        return
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return
    data["hang_killed"] = hang_killed
    data["process_rc"] = process_rc
    if hang_killed:
        data["pass"] = False
    report.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--world", default="World_164")
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--no-fly", action="store_true")
    ap.add_argument(
        "--fly-stop",
        action="store_true",
        help="fly phase then release W for stop-recovery (AppRunner --fly-stop)",
    )
    ap.add_argument("--fly-phase-sec", type=float, default=50.0)
    ap.add_argument("--stop-phase-sec", type=float, default=50.0)
    ap.add_argument("--idle-sec", type=float, default=8.0)
    ap.add_argument(
        "--warmup-sec",
        type=float,
        default=5.0,
        help="analyze skip of early periods (land-cruise raises to ≥20)",
    )
    ap.add_argument(
        "--sprint",
        action="store_true",
        help="hold sprint during fly (covers more chunks like manual)",
    )
    ap.add_argument(
        "--visible",
        action="store_true",
        help="show GLFW window (default hidden; GL context still exists hidden)",
    )
    ap.add_argument(
        "--resume",
        action="store_true",
        default=True,
        help="resume from save position (default; manual flight)",
    )
    ap.add_argument(
        "--teleport-cruise",
        action="store_true",
        help="teleport to fixed cruise start chunk (legacy auto west)",
    )
    ap.add_argument("--report", type=Path, default=BIN / "flight_sim_gate_report.json")
    ap.add_argument(
        "--build-dir",
        type=Path,
        default=ROOT / "build" / ("desktop-msvc" if sys.platform == "win32" else "desktop-linux"),
    )
    ap.add_argument(
        "--update-best",
        action="store_true",
        help="copy report to west_best / stop_best when improved",
    )
    ap.add_argument(
        "--replay-manual",
        action="store_true",
        help="replay World_164 manual profile: resume save pos (no teleport), "
        "level pitch, hold-space altitude, fly-stop",
    )
    ap.add_argument(
        "--replay-manual-fly-heavy",
        action="store_true",
        help="replay-manual with fly-heavy timing: idle 20s, fly 120s, stop 30s "
        "(less stop share for mesh fly gates)",
    )
    ap.add_argument(
        "--segment-fly-only-analyze",
        action="store_true",
        help="pass --segment-fly-only to analyzer (ignore stop tail for fly medians)",
    )
    ap.add_argument(
        "--replay-edge",
        action="store_true",
        help="replay World_164 edge autofly route (-47,5): teleport-cruise + fly-stop",
    )
    ap.add_argument(
        "--land-cruise",
        action="store_true",
        help="inland land cruise (manual corridor ~-485,50): teleport + hold-space "
        "+ cruise-eye-y + fly-stop; analyze --manual-idle",
    )
    ap.add_argument(
        "--land-stand",
        action="store_true",
        help="inland land stand (manual 170154 forever-hole): short east fly then "
        "stop≥60s on one chunk; ARCH_D3_LAND miss_end/stale",
    )
    ap.add_argument(
        "--land-south",
        action="store_true",
        help="inland −Z stand (manual 190350): from (-483,54) yaw 270 short fly "
        "then stop≥60s; residual stale/void blacks east/north autofly miss",
    )
    ap.add_argument(
        "--land-south-short",
        action="store_true",
        help="manual 190350 mid-heal repro: same −Z corridor as land-south but "
        "stop≈10s (catches stale/void before long heal)",
    )
    ap.add_argument(
        "--cruise-cx",
        type=float,
        default=None,
        help="teleport cruise start chunk X (default: ocean -47 or land -485)",
    )
    ap.add_argument(
        "--cruise-cz",
        type=float,
        default=None,
        help="teleport cruise start chunk Z (default: ocean 5 or land 50)",
    )
    ap.add_argument(
        "--cruise-eye-y",
        type=float,
        default=None,
        help="absolute eye Y for land cruise (overrides sea+alt when set)",
    )
    ap.add_argument(
        "--yaw",
        type=float,
        default=None,
        help="autopilot yaw degrees (default: exe 180 west)",
    )
    ap.add_argument(
        "--hold-space",
        action="store_true",
        help="hold Space while flying (climb / maintain altitude)",
    )
    ap.add_argument(
        "--min-alt-above-sea",
        type=float,
        default=None,
        help="AppRunner MinAltitudeAboveSea (ocean void telem needs ≤~12 so "
        "DarkFaceVoidNearN sphere 24m sees sea faces; default exe 28)",
    )
    ap.add_argument(
        "--pitch",
        type=float,
        default=None,
        help="autopilot pitch degrees (default: exe -2, replay-manual/land 0)",
    )
    ap.add_argument(
        "--process-timeout",
        type=float,
        default=0.0,
        help="max wall seconds for Cubatarium (0 = seconds + 120 grace)",
    )
    ap.add_argument(
        "--phase-id",
        default="",
        help="optional label written to flight_sim_phase_history.jsonl",
    )
    ap.add_argument(
        "--skip-preflight",
        action="store_true",
        help="do not kill orphan Cubatarium before run (debug only)",
    )
    ap.add_argument(
        "--baseline-manual",
        default="",
        help="analyze --baseline-manual path for Era38 2x parity soft gate",
    )
    ap.add_argument(
        "--scenario",
        default="",
        choices=[
            "",
            "break-stand",
            "visual-blue",
            "visual-dig",
            "visual-flicker",
            "visual-edge",
            "land-cruise",
            "land-cruise-resume",
            "land-stand",
            "land-south",
            "land-south-short",
            "idle-clean",
            "idle-warm",
            "idle-edit-smoke",
            "fly-clean",
            "ocean-cruise",
            "ocean-cruise-enter",
            "ocean-cruise-stress",
            "ocean-cruise-short",
            "fz-validate",
            "fz-manual-parity",
            "fz-manual-plateau",
            "fz-manual-long",
            "fz-cold-enter",
            "fz-ne-frontier-stand",
            "fz-frontier-stand-resume",
            "fz-inring-cruise",
            "product-174657",
        ],
        help="named scenario (... / product-174657 west G1 proxy / fz-inring-cruise)",
    )
    ap.add_argument("--break-phase-sec", type=float, default=20.0)
    ap.add_argument("--break-interval-sec", type=float, default=1.0)
    ap.add_argument("--yaw-sweep-sec", type=float, default=3.0)
    ap.add_argument(
        "--repeat",
        type=int,
        default=1,
        help="run scenario N times; write report, report_2..N, and report_agg.json",
    )
    args = ap.parse_args()

    if args.scenario == "break-stand":
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 55.0
        args.idle_sec = max(args.idle_sec, 8.0)
        min_break = args.idle_sec + args.break_phase_sec + 5.0
        if args.seconds < min_break:
            args.seconds = min_break

    if args.scenario == "visual-blue":
        # Resume near-sea World_164 focus; yaw sweep 0/90/180/270.
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 0.0
        args.idle_sec = max(args.idle_sec, 5.0)
        min_blue = args.idle_sec + args.yaw_sweep_sec * 4.0 + 5.0
        if args.seconds < min_blue:
            args.seconds = min_blue

    if args.scenario == "visual-dig":
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 55.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.break_phase_sec = max(args.break_phase_sec, 20.0)
        min_dig = args.idle_sec + args.break_phase_sec + 5.0
        if args.seconds < min_dig:
            args.seconds = min_dig

    if args.scenario == "visual-flicker":
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.teleport_cruise = True
        args.resume = False
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 30.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 20.0)

    if args.scenario == "visual-edge":
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.teleport_cruise = True
        args.resume = False
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 30.0)

    if args.scenario == "land-cruise":
        args.land_cruise = True
    if args.scenario == "land-cruise-resume":
        args.land_cruise_resume = True
    if args.scenario == "land-stand":
        args.land_stand = True
    if args.scenario == "land-south":
        args.land_south = True
    if args.scenario == "land-south-short":
        args.land_south_short = True

    if args.scenario == "idle-clean":
        # Clean idle perf: land −Z corridor, short fly, long stand (≥60s), no edit.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 20.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 60.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "idle-warm":
        # Debtful stand near manual focus (-482,72): longer fly accumulates remesh.
        args.world = args.world or "World_164"
        args.fly_stop = True
        if "--resume" in sys.argv:
            args.resume = True
            args.teleport_cruise = False
        else:
            args.resume = False
            args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            # South over land (same as land-cruise): yaw 270 gave opaque~4/blue.
            args.yaw = 90.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 40.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 40.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 60.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "idle-edit-smoke":
        # Stand + forced dig pulse for control-lag / physics_block regression.
        args.world = args.world or "World_164"
        args.no_fly = True
        args.fly_stop = False
        args.resume = True
        args.teleport_cruise = False
        args.hold_space = False
        args.sprint = False
        if args.pitch is None:
            args.pitch = 55.0
        args.idle_sec = max(args.idle_sec, 15.0)
        args.break_phase_sec = max(args.break_phase_sec, 8.0)
        args.break_interval_sec = min(args.break_interval_sec, 1.0)
        # Idle → edit → post-edit stand (~30s) inside break window + tail.
        min_edit = args.idle_sec + args.break_phase_sec + 30.0 + 5.0
        if args.seconds < min_edit:
            args.seconds = min_edit
        args.warmup_sec = max(args.warmup_sec, 8.0)

    if args.scenario == "fly-clean":
        # Moving cruise stress: fly ≥40s; judge move-segment sync/wall, not stop.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 40.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 40.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 20.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 15.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    def _apply_ocean_cruise_base():
        # Era31 void-debt parity: DarkFaceVoidNearN is a 24m sphere — sea+28 +
        # HoldSpace climb made autofly blind (void_max=0) while manual saw 774+.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.sprint = False
        args.hold_space = False
        if args.min_alt_above_sea is None:
            args.min_alt_above_sea = 10.0
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 180.0
        # Manual SoT corridor 122032/153653 (−550…−555, ~110), not (−525,100).
        if args.cruise_cx is None:
            args.cruise_cx = -550.0
        if args.cruise_cz is None:
            args.cruise_cz = 110.0

    if args.scenario == "ocean-cruise":
        # Ocean west cruise FillWater horizon heal stress (void/VB/fluid).
        # No cruise_eye_y — AppRunner sea+min_alt clamp.
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = True
        args.idle_sec = max(args.idle_sec, 8.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 65.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 60.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "ocean-cruise-enter":
        # Full enter path (no teleport) — reproduces manual residency buildup.
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = False
        args.idle_sec = max(args.idle_sec, 45.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 65.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 60.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "ocean-cruise-stress":
        # Cold teleport + short idle + sprint — void/holes parity with manual.
        # (Warm resume + idle≥12 + sea+28 hid void; OCEAN_CRUISE_STRESS DoD.)
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = True
        args.sprint = True
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 3.0
        else:
            args.idle_sec = max(args.idle_sec, 3.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 90.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 75.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        # Keep early void peak in fly segment (warmup 16 ate period 0–1).
        if "--warmup-sec" not in sys.argv:
            args.warmup_sec = 8.0
        else:
            args.warmup_sec = min(args.warmup_sec, 8.0)

    if args.scenario == "ocean-cruise-short":
        # Stop-debt snapshot (land_south_short lesson): shorter idle keeps void.
        _apply_ocean_cruise_base()
        args.resume = False
        args.teleport_cruise = True
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 3.0
        else:
            args.idle_sec = max(args.idle_sec, 3.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 65.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 60.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "product-174657":
        # G1 product gate proxy: west 174657-class (yaw 180), not north replay-manual.
        # See bin/suite_reports/g1_a10_relight/autofly_vs_manual_diff.md
        # Pin resume locus to spawn-near (7,3) — drifted saves start mid-west and
        # under-stress (fog_rd collapse → false VB PASS). Match manual 080455
        # distance (~10 chunks west), not fly-heavy 120s to ocean.
        if not args.visible:
            import os

            if os.environ.get("CUBA_FLIGHT_REQUIRE_VISIBLE", "").strip() in (
                "1",
                "true",
                "TRUE",
                "yes",
                "YES",
            ):
                print(
                    "FAIL: product-174657 requires --visible "
                    "(CUBA_FLIGHT_REQUIRE_VISIBLE=1)",
                    file=sys.stderr,
                    flush=True,
                )
                return 2
            print(
                "WARN: product-174657 without --visible uses hidden GLFW; "
                "operator cannot eye the flight. Pass --visible for honest gates.",
                flush=True,
            )
        args.replay_manual = True
        args.replay_manual_fly_heavy = False
        if args.yaw is None:
            args.yaw = 180.0
        if not (args.phase_id or "").strip():
            args.phase_id = "product_174657_proxy_v3"
        if args.teleport_cruise:
            print(
                "WARN: product-174657 forces --no-teleport-cruise "
                "(west 174657-class resume proxy)",
                flush=True,
            )
            args.teleport_cruise = False
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 15.0
        if "--fly-phase-sec" not in sys.argv:
            # Sticky land-eye used to pin Y and stick at cx≈2. With continuous
            # terrain+12 follow, ~5–6 blk/s covers (7,3)→(−3,3) in ~35s.
            # 55s leaves margin without overshooting to cx≈−20 (dilutes VB class).
            args.fly_phase_sec = 55.0
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 20.0
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        # Focus (7,3) ≈ world (120, y, 56); pin eye Y to manual 122212/100645 (~56).
        users = BIN / "worlds" / "World_164" / "users.json"
        if users.is_file():
            try:
                data = json.loads(users.read_text(encoding="utf-8"))
                user = data.get("Username") or data
                y = 56.0
                user["position"] = [120.0, y, 56.0]
                user["yaw"] = 180.0
                user["pitch"] = 0.0
                users.write_text(
                    json.dumps(data, indent=4) + "\n", encoding="utf-8"
                )
                print(
                    f"INFO: product-174657 pinned World_164 locus to "
                    f"[120, {y}, 56] yaw180 (focus~7,3)",
                    flush=True,
                )
            except (OSError, json.JSONDecodeError, TypeError, ValueError) as exc:
                print(f"WARN: product-174657 locus pin failed: {exc}", flush=True)
        # Fog pull-in collapses RD and masks west VB/missing (manual keeps fog~3–4).
        # Temporarily disable for this scenario; restore after the run.
        cfg_path = BIN / "config.json"
        args._product174657_cfg_restore = None  # type: ignore[attr-defined]
        if cfg_path.is_file():
            try:
                cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
                render = cfg.setdefault("render", {})
                prev_fog = render.get("fog_pull_in_enabled", True)
                args._product174657_cfg_restore = (cfg_path, prev_fog)  # type: ignore[attr-defined]
                if prev_fog is not False:
                    render["fog_pull_in_enabled"] = False
                    cfg_path.write_text(
                        json.dumps(cfg, indent=4) + "\n", encoding="utf-8"
                    )
                    print(
                        "INFO: product-174657 set render.fog_pull_in_enabled=false "
                        f"(was {prev_fog})",
                        flush=True,
                    )
            except (OSError, json.JSONDecodeError, TypeError, ValueError) as exc:
                print(f"WARN: product-174657 fog pin failed: {exc}", flush=True)

    if args.replay_manual_fly_heavy:
        args.replay_manual = True

    if args.replay_manual:
        args.world = "World_164"
        args.fly_stop = True
        args.resume = True
        # Resume save focus (manual 190126 / 192816 ~-484) — do NOT teleport to (-47,5).
        # --cruise-cx is ignored without teleport (AppRunner); pin requires land save.
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        # Default north (+Z) smoke; product-174657 sets yaw 180 (west) before this.
        if args.yaw is None:
            args.yaw = 90.0
        if args.scenario == "product-174657":
            # Eye-level west parity with manual 122212/100645.
            # HoldSpace climb made autofly Y ~76→300 and collapsed fog_rd/miss
            # class (same lesson as ocean-cruise HoldSpace blindness).
            # Without Space, free-move at y≈50 sticks in terrain (cold 124719:
            # focus stayed (7,3)). CruiseEyeY unlocks land-eye floor
            # (terrain+12, continuous along route) without Space climb.
            args.hold_space = False
            if args.min_alt_above_sea is None:
                args.min_alt_above_sea = 0.0
            if args.cruise_eye_y is None:
                args.cruise_eye_y = 56.0
            if args.pitch is None:
                args.pitch = 0.0
            # Timings already set above (idle15/fly38/stop20); do not bump to
            # north smoke 45/90/90 or fly-heavy 20/120/30.
            pass
        elif args.replay_manual_fly_heavy:
            args.idle_sec = max(args.idle_sec, 20.0)
            args.fly_phase_sec = max(args.fly_phase_sec, 120.0)
            args.stop_phase_sec = max(args.stop_phase_sec, 30.0)
        else:
            args.idle_sec = max(args.idle_sec, 45.0)
            # Argparse default fly-phase=50 is too short at ~3 FPS (travel<3 → exit 1).
            if "--fly-phase-sec" not in sys.argv:
                args.fly_phase_sec = 90.0
            else:
                args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)

    if args.land_cruise:
        # Inland corridor matching manual 084551…142306 (not ocean -47,5).
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            # South over land (L2): opaque_med~700. West (180) at eye-y 96
            # often sparse/blue_screen (L1/L3/L4 opaque_med~2–4).
            args.yaw = 90.0
        if args.cruise_cx is None:
            args.cruise_cx = -485.0
        if args.cruise_cz is None:
            args.cruise_cz = 50.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 45.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        # Skip cold-spawn miss in analyze (land_fix_P1e: miss=1 for ~12s at
        # teleport). Do not raise idle — longer idle raised wall/dirty (P1f).
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if getattr(args, "land_cruise_resume", False):
        # Era38 B1: gate of record — World_174 resume, no teleport, stand-before-fly.
        # Do NOT default cruise_eye_y (keep save height unless CLI set --cruise-eye-y).
        args.world = args.world or "World_174"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 90.0
        # Optional corridor hints only (no teleport); unused when teleport=False.
        if args.cruise_cx is None:
            args.cruise_cx = 2.0
        if args.cruise_cz is None:
            args.cruise_cz = -10.0
        args.idle_sec = max(args.idle_sec, 15.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 45.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.land_stand:
        # Forever-hole repro (manual 170154): short east fly then stand ≥60s.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            # East (−491→−484 in manual 170154).
            args.yaw = 0.0
        if args.cruise_cx is None:
            args.cruise_cx = -485.0
        if args.cruise_cz is None:
            args.cruise_cz = 50.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.land_south or args.land_south_short:
        # Manual 190350: (−483,54)→(−485,47) (−Z). Autofly yaw 90 = +Z (L2
        # "south"); yaw 270 = −Z to match that corridor / residual blacks.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 8.0)
        # Argparse default fly-phase=50 stretches past the manual stand chunk;
        # keep short unless user overrode --fly-phase-sec.
        if "--fly-phase-sec" not in sys.argv:
            # short: ≥25s so chunks_traveled≥3 (fly20 sometimes only 2).
            args.fly_phase_sec = 25.0 if args.land_south_short else 20.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        if args.land_south_short:
            # Mid-heal snapshot (~manual 190350 ~8s stop). Shorter idle so
            # emerge/void debt still visible at stop (teleport+idle8 was too clean).
            if "--idle-sec" not in sys.argv:
                args.idle_sec = 3.0
            if "--stop-phase-sec" not in sys.argv:
                args.stop_phase_sec = 10.0
            else:
                args.stop_phase_sec = max(args.stop_phase_sec, 10.0)
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 16.0)

    if args.scenario == "fz-validate":
        # FZ2.2 manual parity: land-south corridor, extended fly+stop (~195s).
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if args.cruise_cx is None:
            args.cruise_cx = -483.0
        if args.cruise_cz is None:
            args.cruise_cz = 54.0
        if args.cruise_eye_y is None:
            args.cruise_eye_y = 96.0
        args.idle_sec = max(args.idle_sec, 15.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 90.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 90.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 90.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-manual-parity":
        # FZ2.3 DoD: resume save, NO teleport — mimics manual land-south FZ flights.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        args.idle_sec = max(args.idle_sec, 45.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 90.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 90.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 90.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-manual-plateau":
        # FZ2.4 DoD: resume no-teleport; stop before steady drain masks PL plateau.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        args.idle_sec = max(args.idle_sec, 45.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 45.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 15.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 15.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-manual-long":
        # FZ2.4 C8 proxy: resume no-teleport; long fly+stop for drain + steady gates.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        args.idle_sec = max(args.idle_sec, 45.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 480.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 480.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 120.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 120.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-cold-enter":
        # FZ2.3 PL enter stress: cold load, NO teleport (ocean-cruise-enter model).
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        args.idle_sec = max(args.idle_sec, 45.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 90.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 90.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 90.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-ne-frontier-stand":
        # P14 SoftDefer thrash repro: World_164 cold spawn ~(118,86) (manual
        # 205739 stand locus, keep=169). fly_phase=0 + long stop — any NE fly
        # walks into ocean (keep~49) and invalidates SoftDefer standstill gates.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 270.0
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 30.0
        else:
            args.idle_sec = max(args.idle_sec, 15.0)
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 0.0
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 120.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-frontier-stand-resume":
        # Isolate SoftDefer standstill: resume save should already be near
        # dense frontier (~118,86); short/no fly + long stop. Operator places
        # save or chains after fz-ne-frontier-stand.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = True
        args.teleport_cruise = False
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 10.0
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 15.0
        if "--fly-phase-sec" not in sys.argv:
            args.fly_phase_sec = 10.0
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 90.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.scenario == "fz-inring-cruise":
        # P16 in-ring holes vs manual 221516 corridor (118,86)->(-31,58).
        # Cold World_164 save drifts after manuals; pin SoT locus via teleport.
        # Engine yaw: 180=west (-X), 270=south (-Z) — south from (-31,58) hits
        # ocean keep~49 (abort). Keep med must stay >=160 for hole audit.
        args.world = args.world or "World_164"
        args.fly_stop = True
        args.resume = False
        args.teleport_cruise = True
        if args.cruise_cx is None:
            args.cruise_cx = 118.0
        if args.cruise_cz is None:
            args.cruise_cz = 86.0
        args.sprint = False
        args.hold_space = True
        if args.pitch is None:
            args.pitch = 0.0
        if args.yaw is None:
            args.yaw = 180.0
        if "--idle-sec" not in sys.argv:
            args.idle_sec = 15.0
        else:
            args.idle_sec = max(args.idle_sec, 10.0)
        if "--fly-phase-sec" not in sys.argv:
            # Keep land corridor: 60s west from (118,86) often exits keep→49.
            args.fly_phase_sec = 35.0
        else:
            args.fly_phase_sec = max(args.fly_phase_sec, 20.0)
        if "--stop-phase-sec" not in sys.argv:
            args.stop_phase_sec = 120.0
        else:
            args.stop_phase_sec = max(args.stop_phase_sec, 90.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )
        args.warmup_sec = max(args.warmup_sec, 20.0)

    if args.replay_edge:
        args.world = "World_164"
        args.fly_stop = True
        args.teleport_cruise = True
        args.resume = False
        args.sprint = False
        args.hold_space = False
        if args.pitch is None:
            args.pitch = -2.0
        args.idle_sec = max(args.idle_sec, 8.0)
        args.fly_phase_sec = max(args.fly_phase_sec, 45.0)
        args.stop_phase_sec = max(args.stop_phase_sec, 60.0)
        args.seconds = max(
            args.seconds,
            args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0,
        )

    phase_id = (args.phase_id or "").strip()
    if phase_id.startswith("mesh-") and args.teleport_cruise:
        raise SystemExit(
            "FAIL: teleport_cruise=true forbidden for mesh-* phase-id "
            f"({phase_id!r}); use no-teleport replay-manual harness"
        )
    if args.scenario == "product-174657" and args.teleport_cruise:
        raise SystemExit(
            "FAIL: teleport_cruise=true forbidden for product-174657 "
            "(west 174657-class resume proxy; use --no-teleport-cruise)"
        )

    # Phase 5.5 / 5.6 / 5.7: no-teleport gate; land-stand is teleport smoke only.
    report_name = str(args.report).replace("\\", "/").lower()
    phase55_gate = phase_id.startswith("phase55") or "/phase55_" in report_name
    phase56_gate = phase_id.startswith("phase56") or "/phase56_" in report_name
    phase57_gate = phase_id.startswith("phase57") or "/phase57_" in report_name
    phase_no_teleport_gate = phase55_gate or phase56_gate or phase57_gate
    if args.land_stand:
        print(
            "WARN: --land-stand forces teleport_cruise=True; "
            "not a Phase55/56/57 / mesh no-teleport gate",
            flush=True,
        )
    if phase_no_teleport_gate and args.teleport_cruise:
        gate_name = (
            "Phase57" if phase57_gate else ("Phase56" if phase56_gate else "Phase55")
        )
        raise SystemExit(
            f"FAIL: teleport_cruise=true forbidden for {gate_name} gate "
            f"(phase_id={phase_id!r} report={args.report}); "
            "use --replay-manual[-fly-heavy] or --scenario fz-cold-enter"
        )
    if phase_no_teleport_gate and args.land_stand:
        gate_name = (
            "Phase57" if phase57_gate else ("Phase56" if phase56_gate else "Phase55")
        )
        raise SystemExit(
            f"FAIL: --land-stand forbidden for {gate_name} gate "
            "(teleport smoke only; use no-teleport replay-manual)"
        )

    # Phase55/56 / soft_force@150s: bump default timeout when operator left 0.
    phase55_need_timeout = phase_no_teleport_gate or args.replay_manual or (
        args.scenario in ("fz-cold-enter", "fz-manual-parity", "fz-manual-long")
    )
    if phase55_need_timeout and args.process_timeout <= 0.0:
        args.process_timeout = 600.0
        print(
            "INFO: process-timeout defaulted to 600s "
            "(soft_force@150s + fly/stop; Phase55/56/57 / replay-manual / fz-cold-enter)",
            flush=True,
        )

    if not args.skip_preflight:
        print("preflight: killing orphan Cubatarium.exe (if any)", flush=True)
        preflight_cleanup()

    if args.build:
        # MSVC multi-config: Debug lands under build/*/Debug; flight-sim runs
        # bin/Cubatarium.exe which is Release/RelWithDebInfo RUNTIME_OUTPUT.
        cmd = [
            "cmake",
            "--build",
            str(args.build_dir),
            "--config",
            "Release",
            "--parallel",
            "8",
            "-j7",
            "--target",
            "Cubatarium",
        ]
        print("building:", " ".join(cmd), flush=True)
        if not args.skip_preflight:
            preflight_cleanup()
        rc = subprocess.call(cmd)
        if rc != 0:
            return rc

    if not EXE.is_file():
        print(f"FAIL: missing {EXE}", file=sys.stderr)
        return 2

    if args.fly_stop:
        min_sec = args.idle_sec + args.fly_phase_sec + args.stop_phase_sec + 5.0
        if args.seconds < min_sec:
            args.seconds = min_sec

    repeats = max(1, int(args.repeat or 1))
    base_report = args.report
    last_rc = 0
    run_reports: list[Path] = []

    for rep in range(1, repeats + 1):
        if repeats > 1:
            if rep == 1:
                report_path = base_report
            else:
                stem = base_report.stem
                report_path = base_report.with_name(f"{stem}_{rep}{base_report.suffix}")
            args.report = report_path
            print(f"=== repeat {rep}/{repeats} → {report_path} ===", flush=True)
        else:
            report_path = base_report

        t0 = time.time()
        if not args.skip_preflight:
            kill_cubatarium_orphans()

        sim_cmd = [
            str(resolve_exe()),
            "--flight-sim",
            "--world",
            args.world,
            "--seconds",
            str(args.seconds),
            "--report",
            str(BIN / "flight_sim_report.json"),
        ]
        break_scenarios = ("break-stand", "visual-dig", "idle-edit-smoke")
        if args.scenario in break_scenarios:
            sim_cmd.append("--break-stand")
            sim_cmd.extend(["--break-phase", str(args.break_phase_sec)])
            sim_cmd.extend(["--break-interval", str(args.break_interval_sec)])
            sim_cmd.append("--no-fly")
            sim_cmd.append("--no-hold-forward")
        elif args.scenario == "visual-blue":
            sim_cmd.append("--yaw-sweep")
            sim_cmd.extend(["--yaw-sweep-sec", str(args.yaw_sweep_sec)])
            sim_cmd.append("--no-fly")
            sim_cmd.append("--no-hold-forward")
        elif args.no_fly:
            sim_cmd.append("--no-fly")
            sim_cmd.append("--no-hold-forward")
        else:
            sim_cmd.extend(["--fly", "--hold-forward"])
        if args.fly_stop and args.scenario not in (
            "break-stand",
            "visual-dig",
            "visual-blue",
            "idle-edit-smoke",
        ):
            sim_cmd.append("--fly-stop")
            sim_cmd.extend(["--fly-phase", str(args.fly_phase_sec)])
            sim_cmd.extend(["--stop-phase", str(args.stop_phase_sec)])
        sim_cmd.extend(["--idle", str(args.idle_sec)])
        if args.sprint:
            sim_cmd.append("--sprint")
        if args.hold_space:
            sim_cmd.append("--hold-space")
        if args.pitch is not None:
            sim_cmd.extend(["--pitch", str(args.pitch)])
        if args.yaw is not None:
            sim_cmd.extend(["--yaw", str(args.yaw)])
        if args.visible:
            sim_cmd.append("--visible")
        if args.teleport_cruise:
            sim_cmd.append("--teleport-cruise")
        else:
            sim_cmd.append("--no-teleport-cruise")
        if args.cruise_cx is not None:
            sim_cmd.extend(["--cruise-cx", str(args.cruise_cx)])
        if args.cruise_cz is not None:
            sim_cmd.extend(["--cruise-cz", str(args.cruise_cz)])
        if args.cruise_eye_y is not None:
            sim_cmd.extend(["--cruise-eye-y", str(args.cruise_eye_y)])
        if args.min_alt_above_sea is not None:
            sim_cmd.extend(["--min-alt-above-sea", str(args.min_alt_above_sea)])

        process_timeout = args.process_timeout
        if process_timeout <= 0.0:
            process_timeout = args.seconds + 120.0
        if args.fly_stop and args.scenario not in ("fz-manual-plateau",):
            process_timeout = max(process_timeout, 420.0)
        if args.scenario in (
            "break-stand",
            "visual-dig",
            "visual-blue",
            "idle-edit-smoke",
            "ocean-cruise",
            "ocean-cruise-enter",
            "ocean-cruise-stress",
            "ocean-cruise-short",
        ):
            process_timeout = max(process_timeout, args.seconds + 180.0)

        print("running:", " ".join(sim_cmd), flush=True)
        rc = run_with_timeout(sim_cmd, BIN, process_timeout)
        hang_killed = rc == 124
        kill_cubatarium_orphans()

        perf = newest_perf(t0)
        if perf is None:
            flight_report = BIN / "flight_sim_report.json"
            if flight_report.is_file():
                data = json.loads(flight_report.read_text(encoding="utf-8"))
                p = data.get("perf_jsonl") or ""
                if p and Path(p).is_file():
                    perf = Path(p)

        ana = 1
        if perf is None:
            print("FAIL: no perf jsonl produced", file=sys.stderr)
            append_phase_history(
                {
                    "phase": args.phase_id or "unspecified",
                    "rc": rc,
                    "hang_killed": hang_killed,
                    "perf": None,
                    "report": str(report_path),
                    "repeat": rep,
                }
            )
            last_rc = 3 if hang_killed else 1
            if repeats == 1:
                return last_rc
            continue

        print(f"analyzing {perf}", flush=True)
        analyze_cmd = [
            sys.executable,
            str(ANALYZE),
            str(perf),
            "--report",
            str(report_path),
        ]
        if getattr(args, "segment_fly_only_analyze", False):
            analyze_cmd.append("--segment-fly-only")
        if (
            args.replay_manual
            or args.fly_stop
            or args.land_cruise
            or args.land_stand
            or args.land_south
            or args.land_south_short
            or args.scenario
            in (
                "idle-clean",
                "idle-warm",
                "idle-edit-smoke",
                "fly-clean",
                "ocean-cruise",
                "fz-validate",
                "fz-manual-parity",
                "fz-manual-plateau",
                "fz-manual-long",
                "fz-cold-enter",
                "fz-ne-frontier-stand",
                "fz-frontier-stand-resume",
                "fz-inring-cruise",
            )
            or (args.scenario or "").startswith("ocean-cruise")
        ):
            analyze_cmd.append("--manual-idle")
        if getattr(args, "warmup_sec", None) is not None:
            analyze_cmd.extend(["--warmup-sec", str(args.warmup_sec)])
        if getattr(args, "baseline_manual", None):
            analyze_cmd.extend(["--baseline-manual", str(args.baseline_manual)])
        ana = subprocess.call(analyze_cmd)
        info_log = None
        if DIAG.is_file():
            import importlib.util

            spec = importlib.util.spec_from_file_location("flight_sim_diag", DIAG)
            if spec and spec.loader:
                mod = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(mod)
                info_log = mod.newest_info_log(t0)
        annotate_report_run(report_path, hang_killed, rc, perf, info_log)
        # Phase55 fidelity: expose teleport flag for AnalyzePhase55Scorecard.
        if report_path.is_file():
            try:
                _ann = json.loads(report_path.read_text(encoding="utf-8"))
                _ann["teleport_cruise"] = bool(args.teleport_cruise)
                _ann["process_timeout_s"] = float(process_timeout)
                report_path.write_text(
                    json.dumps(_ann, indent=2) + "\n", encoding="utf-8"
                )
            except (json.JSONDecodeError, OSError):
                pass

        metrics_summary: dict = {}
        if report_path.is_file():
            run_reports.append(report_path)
            try:
                result = json.loads(report_path.read_text(encoding="utf-8"))
                if args.scenario == "product-174657" and perf and Path(perf).is_file():
                    adequacy = compute_product_174657_proxy_adequacy(Path(perf))
                    result["proxy_adequacy"] = adequacy
                    warm = "warm" in report_path.stem.lower()
                    stop_line = compute_dual_lane_stop_line(Path(perf), warm=warm)
                    result["dual_lane_stop_line"] = stop_line
                    result["dual_lane_stop_line_pass"] = stop_line.get(
                        "dual_lane_stop_line_pass"
                    )
                    result["dual_lane_stop_line_fails"] = stop_line.get(
                        "dual_lane_stop_line_fails"
                    )
                    eye_proxy = compute_eye_proxy_stop_line(Path(perf))
                    result["eye_proxy_stop_line"] = eye_proxy
                    result["eye_proxy_stop_line_pass"] = eye_proxy.get(
                        "eye_proxy_stop_line_pass"
                    )
                    result["eye_proxy_stop_line_fails"] = eye_proxy.get(
                        "eye_proxy_stop_line_fails"
                    )
                    west = compute_west_route_coverage(Path(perf))
                    result["west_route_coverage"] = west
                    report_path.write_text(
                        json.dumps(result, indent=2) + "\n", encoding="utf-8"
                    )
                    print(
                        "product-174657 adequacy: "
                        + ("PASS" if adequacy.get("adequacy_pass") else "FAIL")
                        + f" {adequacy}",
                        flush=True,
                    )
                    print(
                        "product-174657 dual-lane stop-line: "
                        + (
                            "PASS"
                            if stop_line.get("dual_lane_stop_line_pass")
                            else "FAIL"
                        )
                        + f" {stop_line}",
                        flush=True,
                    )
                    print(
                        "product-174657 eye-proxy stop-line: "
                        + (
                            "PASS"
                            if eye_proxy.get("eye_proxy_stop_line_pass")
                            else "FAIL"
                        )
                        + f" {eye_proxy}",
                        flush=True,
                    )
                    print(
                        "product-174657 west-route coverage: "
                        + str(west.get("west_route_coverage"))
                        + f" {west}",
                        flush=True,
                    )
                metrics_summary = {
                    "pass": result.get("pass"),
                    "hang_killed": result.get("hang_killed"),
                    "gates_pass_count": gates_pass_count(result),
                    "gates_stop_pass_count": gates_stop_pass_count(result),
                    "metrics": {
                        k: (result.get("metrics") or {}).get(k)
                        for k in (
                            "pending_light_focus_med",
                            "post_stop_pending_med",
                            "post_stop_not_ready_end",
                            "stop_not_ready_delta",
                            "post_stop_black_sticky_max",
                            "stop_wall_med",
                            "calm_stop_wall_med",
                            "calm_stop_emerge_med",
                            "calm_stop_stream_med",
                            "stop_mesh_prep_med",
                            "chunks_traveled",
                            "dominant_spike_class",
                            "dominant_heavy_spike_class",
                            "spike_max_world_extra",
                            "spike_world_extra_dominant_rate",
                            "break_complete_sum",
                            "break_inflight_race_sum",
                            "break_dark_face_sum",
                            "wall_ms_med",
                            "wall_ms_fly_med",
                            "tick_env_fly_max",
                            "world_extra_fly_max",
                        )
                    },
                    "soft": {
                        k: (result.get("soft") or {}).get(k)
                        for k in (
                            "dominant_spike_class",
                            "dominant_heavy_spike_class",
                            "soft_world_extra_ok",
                            "spike_bucket_counts",
                        )
                    },
                }
                if result.get("proxy_adequacy") is not None:
                    metrics_summary["proxy_adequacy"] = result["proxy_adequacy"]
                if result.get("dual_lane_stop_line") is not None:
                    metrics_summary["dual_lane_stop_line"] = result[
                        "dual_lane_stop_line"
                    ]
                    metrics_summary["dual_lane_stop_line_pass"] = result.get(
                        "dual_lane_stop_line_pass"
                    )
                if result.get("eye_proxy_stop_line") is not None:
                    metrics_summary["eye_proxy_stop_line"] = result[
                        "eye_proxy_stop_line"
                    ]
                    metrics_summary["eye_proxy_stop_line_pass"] = result.get(
                        "eye_proxy_stop_line_pass"
                    )
                if args.update_best and not hang_killed:
                    if args.fly_stop:
                        best_path = BIN / "flight_sim_gate_report_stop_best.json"
                    else:
                        best_path = BIN / "flight_sim_gate_report_west_best.json"
                    best = load_best(best_path)
                    if is_better(result, best):
                        best_path.write_text(
                            report_path.read_text(encoding="utf-8"), encoding="utf-8"
                        )
                        print(f"updated best: {best_path}", flush=True)
            except (json.JSONDecodeError, OSError):
                pass

        append_phase_history(
            {
                "phase": args.phase_id or "unspecified",
                "rc": rc,
                "ana": ana,
                "hang_killed": hang_killed,
                "perf": str(perf),
                "report": str(report_path),
                "repeat": rep,
                "summary": metrics_summary,
            }
        )

        if hang_killed:
            print("flight-sim process HANG-KILLED exit=124", file=sys.stderr)
            last_rc = 3
        elif rc != 0:
            print(f"flight-sim process exit={rc}", file=sys.stderr)
            last_rc = rc
        else:
            last_rc = ana
            if args.scenario == "product-174657" and metrics_summary.get(
                "proxy_adequacy"
            ):
                if not metrics_summary["proxy_adequacy"].get("adequacy_pass", False):
                    print(
                        "flight-sim adequacy FAIL for product-174657 proxy",
                        file=sys.stderr,
                    )
                    last_rc = 2
                elif not metrics_summary.get("dual_lane_stop_line_pass", True):
                    print(
                        "flight-sim dual-lane stop-line FAIL for product-174657 "
                        "(adequacy alone is not merge-green)",
                        file=sys.stderr,
                    )
                    last_rc = 2
                elif not metrics_summary.get("eye_proxy_stop_line_pass", True):
                    print(
                        "flight-sim eye-proxy stop-line FAIL for product-174657 "
                        "(stale-visual thrash / holes blink; adequacy alone is not merge-green)",
                        file=sys.stderr,
                    )
                    last_rc = 2

    if repeats > 1 and run_reports:
        agg_path = base_report.with_name(f"{base_report.stem}_agg{base_report.suffix}")
        write_repeat_aggregate(run_reports, agg_path)
        print(f"wrote aggregate: {agg_path}", flush=True)

    restore = getattr(args, "_product174657_cfg_restore", None)
    if restore:
        cfg_path, prev_fog = restore
        try:
            cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
            cfg.setdefault("render", {})["fog_pull_in_enabled"] = prev_fog
            cfg_path.write_text(json.dumps(cfg, indent=4) + "\n", encoding="utf-8")
            print(
                f"INFO: product-174657 restored render.fog_pull_in_enabled={prev_fog}",
                flush=True,
            )
        except (OSError, json.JSONDecodeError, TypeError, ValueError) as exc:
            print(f"WARN: product-174657 fog restore failed: {exc}", flush=True)

    return last_rc


AGG_METRIC_KEYS = (
    "calm_stop_wall_med",
    "calm_stop_emerge_med",
    "calm_stop_stream_med",
    "calm_stop_phys_med",
    "stop_mesh_prep_med",
    "stop_wall_med",
    "wall_ms_med",
    "wall_ms_fly_med",
    "dirty_med",
    "post_stop_focus_dirty_med",
    "post_stop_black_sticky_max",
    "post_stop_missing_max",
    "physics_block_ms_p95",
    "chunks_traveled",
    "opaque_cmd_on_med",
)


def write_repeat_aggregate(reports: list[Path], out: Path) -> None:
    import statistics

    rows: list[dict] = []
    for p in reports:
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            continue
        m = data.get("metrics") or {}
        rows.append(
            {
                "report": str(p),
                "pass": data.get("pass"),
                **{k: m.get(k) for k in AGG_METRIC_KEYS},
            }
        )
    agg: dict = {"n": len(rows), "runs": rows, "median": {}, "min": {}, "max": {}}
    for k in AGG_METRIC_KEYS:
        vals = [r[k] for r in rows if r.get(k) is not None]
        if not vals:
            continue
        agg["median"][k] = statistics.median(vals)
        agg["min"][k] = min(vals)
        agg["max"][k] = max(vals)
    out.write_text(json.dumps(agg, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
