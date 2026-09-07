#!/usr/bin/env python3
"""Phase 5.7 scorecard: flicker/VB/latch + CPU accel vs 093857.

Usage:
  python tools/AnalyzePhase57Scorecard.py \\
    --report bin/suite_reports/phase57_v0_flyheavy.json \\
    [--perf bin/logs/perf_....jsonl] \\
    [--info bin/logs/...INFO...] \\
    [--baseline-manual bin/logs/perf_20260907-093857_26724.jsonl] \\
    [--teleport false] \\
    [--tag v0]
"""
from __future__ import annotations

import argparse
import json
import re
import statistics as st
import sys
from pathlib import Path

SETTLE_RE = re.compile(
    r"settle_reason=(\S+).*?visibility_debt=(\d+)", re.IGNORECASE
)
EMPTY_BATCH_RE = re.compile(r"empty_batch_event")

# Manual SoT 093857 cruise / stand class (baked fallback if baseline file missing).
MANUAL_093857 = {
    "focus_missing_frac": 1.0,
    "phase_abort_heavy_frac": 1.0,
    "visual_holes_frac": 0.55,
    "empty_backlog_max": 19.0,
    "wall_med": 98.0,
    "phase_med": 69.0,
    "pool_unsync_med": 76.0,
    "softdefer_stuck_n_med": 0.0,
    "softdefer_age_max_med": None,
    "focus_unfinished_ahead_tail_med": 10.0,
    "miss_stuck_run_frames_tail_max": 1166.0,
    "enter_settle_soft_force_with_debt_max": 1.0,
    "latch_clear_t_ms": None,  # never cleared on SoT
    "mesh_discarded_late_med": 0.0,  # cruise period-delta (093857 abs plateau ~12)
    "mesh_discarded_late_max": 12.0,  # cruise sum growth class
    "visible_black_focus_med": 43.0,
    "visible_black_focus_max": 108.0,
    "vb_no_ticket_max": 91.0,
    "vb_stalled_max": 26.0,
    "opaque_cull_med": 13.0,
}


def median(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def pct(xs, p):
    xs = [float(x) for x in xs if x is not None]
    if not xs:
        return None
    xs = sorted(xs)
    i = int(round((len(xs) - 1) * p / 100.0))
    return xs[max(0, min(i, len(xs) - 1))]


def load_periods(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        r = json.loads(line)
        if r.get("kind") == "period":
            rows.append(r)
    return rows


def cruise(rows):
    out = []
    for r in rows:
        try:
            spd = float(r.get("movement_speed") or 0)
        except (TypeError, ValueError):
            spd = 0.0
        if spd > 2.0:
            out.append(r)
    return out


def stand_tail(rows, last_s=45.0):
    """Last standing samples (movement_speed<=2) near end of session."""
    if not rows:
        return []
    t_end = float(rows[-1].get("t_ms") or 0)
    out = []
    for r in rows:
        try:
            spd = float(r.get("movement_speed") or 0)
        except (TypeError, ValueError):
            spd = 0.0
        t = float(r.get("t_ms") or 0)
        if spd <= 2.0 and (t_end - t) <= last_s * 1000.0:
            out.append(r)
    return out


def frac(rows, pred):
    if not rows:
        return None
    return sum(1 for r in rows if pred(r)) / len(rows)


def g(r, *keys, default=0):
    for k in keys:
        if k in r and r[k] is not None:
            return r[k]
    return default


def latch_timing(periods):
    """First arm (1) and first clear (1→0) on enter_settle_soft_force_with_debt."""
    armed_periods = 0
    arm_t = None
    clear_t = None
    prev = None
    for r in periods:
        v = g(r, "enter_settle_soft_force_with_debt", default=None)
        if v is None:
            continue
        try:
            iv = int(v)
        except (TypeError, ValueError):
            continue
        t = float(r.get("t_ms") or 0)
        if iv > 0:
            armed_periods += 1
            if arm_t is None:
                arm_t = t
        if prev is not None and prev > 0 and iv <= 0 and clear_t is None:
            clear_t = t
        prev = iv
    return {
        "latch_armed_periods": armed_periods,
        "latch_arm_t_ms": arm_t,
        "latch_clear_t_ms": clear_t,
    }


def analyze_perf(path: Path):
    periods = load_periods(path)
    cr = cruise(periods)
    use = cr if len(cr) >= 3 else periods
    tail = stand_tail(periods)
    lt = latch_timing(periods)

    def series(key, rows=None):
        rows = use if rows is None else rows
        return [g(r, key) for r in rows]

    def series_max(key, rows=None):
        xs = [float(x) for x in series(key, rows) if x is not None]
        return max(xs) if xs else None

    def series_delta_med(key, rows=None):
        """Median of non-negative period-to-period growth (cumulative counters)."""
        rows = use if rows is None else rows
        prev = None
        deltas = []
        for r in rows:
            v = g(r, key)
            if v is None:
                continue
            fv = float(v)
            if prev is not None:
                deltas.append(max(0.0, fv - prev))
            prev = fv
        return median(deltas) if deltas else None

    def series_delta_sum(key, rows=None):
        rows = use if rows is None else rows
        prev = None
        total = 0.0
        n = 0
        for r in rows:
            v = g(r, key)
            if v is None:
                continue
            fv = float(v)
            if prev is not None:
                total += max(0.0, fv - prev)
                n += 1
            prev = fv
        return total if n else None

    def cruise_delta_med(key):
        """Growth only across consecutive cruise periods (skip enter stand spike)."""
        prev = None
        prev_cruise = False
        deltas = []
        for r in periods:
            try:
                spd = float(r.get("movement_speed") or 0)
            except (TypeError, ValueError):
                spd = 0.0
            is_cruise = spd > 2.0
            v = g(r, key)
            if v is None:
                prev = None
                prev_cruise = False
                continue
            fv = float(v)
            if prev is not None and is_cruise and prev_cruise:
                deltas.append(max(0.0, fv - prev))
            prev = fv
            prev_cruise = is_cruise
        return median(deltas) if deltas else None

    def cruise_delta_sum(key):
        prev = None
        prev_cruise = False
        total = 0.0
        n = 0
        for r in periods:
            try:
                spd = float(r.get("movement_speed") or 0)
            except (TypeError, ValueError):
                spd = 0.0
            is_cruise = spd > 2.0
            v = g(r, key)
            if v is None:
                prev = None
                prev_cruise = False
                continue
            fv = float(v)
            if prev is not None and is_cruise and prev_cruise:
                total += max(0.0, fv - prev)
                n += 1
            prev = fv
            prev_cruise = is_cruise
        return total if n else None

    focus_key = "focus_missing_mesh"
    abort_key = "phase_abort_heavy"
    return {
        "periods": len(periods),
        "cruise_n": len(cr),
        "use_n": len(use),
        "stand_tail_n": len(tail),
        "focus_missing_frac": frac(
            use, lambda r: int(g(r, focus_key, "focus_missing") or 0) > 0
        ),
        "phase_abort_heavy_frac": frac(
            use, lambda r: int(g(r, abort_key) or 0) > 0
        ),
        "visual_holes_frac": frac(
            use, lambda r: int(g(r, "visual_holes") or 0) > 0
        ),
        "empty_backlog_max": series_max("empty_backlog_n"),
        "empty_backlog_med": median(series("empty_backlog_n")),
        "unfinished_visual_med": median(series("unfinished_visual")),
        "abort_schedule_final_med": median(series("abort_schedule_final")),
        "miss_horiz_med": median(
            series("nearest_miss_horiz")
            if any("nearest_miss_horiz" in r for r in use)
            else series("miss_horiz")
        ),
        "mesh_dirty_schedule_ok_med": median(series("mesh_dirty_schedule_ok_n")),
        "dirty_fm_med": median(series("dirty_fm_n")),
        "scene_opaque_cull_med": median(series("scene_opaque_cull_ms")),
        "pool_unsync_med": median(series("pool_unsync_uploads")),
        "ingress_debt_med": median(series("ingress_debt_level")),
        "mesh_emerge_med": median(series("mesh_emerge_ms")),
        "stream_med": median(series("stream_ms")),
        "relight_drain_med": median(series("relight_drain_ms")),
        "phase_med": median(series("world_streaming_phase_ms")),
        "wall_med": median(series("wall_ms")),
        "scene_med": median(series("scene_ms")),
        "softdefer_stuck_n_med": median(series("softdefer_empty_stuck_n")),
        "softdefer_age_max_med": median(series("softdefer_empty_age_max_frames")),
        "softdefer_stuck_n_tail_med": median(
            series("softdefer_empty_stuck_n", tail)
        ),
        "softdefer_age_max_tail_med": median(
            series("softdefer_empty_age_max_frames", tail)
        ),
        "softdefer_age_max_tail_p90": pct(
            series("softdefer_empty_age_max_frames", tail), 90
        ),
        "softdefer_owned_no_gpu_med": median(series("softdefer_owned_no_gpu_n")),
        "enter_settle_soft_force_with_debt_max": series_max(
            "enter_settle_soft_force_with_debt"
        ),
        "latch_armed_periods": lt["latch_armed_periods"],
        "latch_arm_t_ms": lt["latch_arm_t_ms"],
        "latch_clear_t_ms": lt["latch_clear_t_ms"],
        "focus_unfinished_ahead_tail_med": median(
            series("focus_unfinished_ahead", tail)
        ),
        "miss_stuck_run_frames_tail_max": series_max(
            "miss_stuck_run_frames", tail
        ),
        "visibility_debt_med": median(series("visibility_debt")),
        "visibility_debt_tail_med": median(series("visibility_debt", tail)),
        "visibility_debt_max": series_max("visibility_debt"),
        "gpu_kick_med": median(series("gpu_kick_n")),
        "gpu_kick_tail_med": median(series("gpu_kick_n", tail)),
        "relight_fifo_med": median(series("relight_fifo_n")),
        # Cumulative FPM: consecutive-cruise deltas only (enter stand spike excluded).
        "mesh_discarded_late_med": cruise_delta_med("mesh_discarded_late"),
        "mesh_discarded_late_max": cruise_delta_sum("mesh_discarded_late"),
        "mesh_discarded_late_epoch_max": cruise_delta_sum(
            "mesh_discarded_late_epoch"
        ),
        "mesh_discarded_late_job_mismatch_max": cruise_delta_sum(
            "mesh_discarded_late_job_mismatch"
        ),
        "mesh_discarded_late_abs_med": median(series("mesh_discarded_late")),
        "visible_black_focus_med": median(series("visible_black_focus_n")),
        "visible_black_focus_max": series_max("visible_black_focus_n"),
        "vb_no_ticket_max": series_max("visible_black_no_ticket_n"),
        "vb_stalled_max": series_max("visible_black_stalled_n"),
        "opaque_cull_med": median(series("scene_opaque_cull_ms")),
    }


def analyze_info(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    settles = []
    for m in SETTLE_RE.finditer(text):
        settles.append({"reason": m.group(1), "visibility_debt": int(m.group(2))})

    bad = [
        s
        for s in settles
        if s["visibility_debt"] > 0
        and (
            s["reason"].startswith("live_")
            or s["reason"].startswith("soft_")
            or s["reason"].startswith("soft_force")
        )
        and s["reason"] != "abort_underfeet"
    ]
    soft_force_debt = [
        s
        for s in settles
        if s["reason"].startswith("soft_force") and s["visibility_debt"] > 0
    ]
    return {
        "settle_count": len(settles),
        "settles": settles[-8:],
        "bad_live_soft_vis_debt": bad,
        "soft_force_with_debt": soft_force_debt,
        "empty_batch_event_n": len(EMPTY_BATCH_RE.findall(text)),
    }


def fmt(v, digits=4):
    if v is None:
        return "None"
    if isinstance(v, float):
        return f"{v:.{digits}g}"
    return str(v)


def print_delta(auto: dict, baseline: dict, label: str):
    keys = [
        "focus_missing_frac",
        "phase_abort_heavy_frac",
        "visual_holes_frac",
        "empty_backlog_max",
        "empty_backlog_med",
        "wall_med",
        "phase_med",
        "pool_unsync_med",
        "softdefer_stuck_n_med",
        "softdefer_age_max_med",
        "softdefer_stuck_n_tail_med",
        "softdefer_age_max_tail_med",
        "scene_med",
        "enter_settle_soft_force_with_debt_max",
        "latch_armed_periods",
        "latch_clear_t_ms",
        "focus_unfinished_ahead_tail_med",
        "miss_stuck_run_frames_tail_max",
        "abort_schedule_final_med",
        "gpu_kick_tail_med",
        "visibility_debt_max",
        "mesh_discarded_late_med",
        "mesh_discarded_late_max",
        "visible_black_focus_med",
        "visible_black_focus_max",
        "vb_no_ticket_max",
        "vb_stalled_max",
        "opaque_cull_med",
    ]
    print(f"=== auto<->manual delta ({label}) ===")
    print(f"{'metric':36} {'auto':>12} {'manual':>12} {'delta':>12}")
    for k in keys:
        a = auto.get(k)
        m = baseline.get(k)
        if a is None and m is None:
            continue
        d = None
        if isinstance(a, (int, float)) and isinstance(m, (int, float)):
            d = float(a) - float(m)
        print(f"{k:36} {fmt(a):>12} {fmt(m):>12} {fmt(d):>12}")


def print_control_checklist(perf: dict | None, info: dict | None, report: dict):
    print("=== Phase57 control checklist ===")
    print(f"  hang_killed={report.get('hang_killed')}  (want false)")
    soft_n = len((info or {}).get("soft_force_with_debt") or [])
    print(f"  soft_force+vis_debt n={soft_n}  (honest FAIL if >0)")
    if perf:
        latch_max = perf.get("enter_settle_soft_force_with_debt_max")
        clear_t = perf.get("latch_clear_t_ms")
        armed = perf.get("latch_armed_periods")
        print(
            f"  latch max={fmt(latch_max)} armed_periods={fmt(armed)} "
            f"clear_t_ms={fmt(clear_t)}  "
            f"(after 5.6.1: clear finite OR debt down)"
        )
        if latch_max is not None and float(latch_max) > 0 and clear_t is None:
            print("  NOTE: latch never cleared in periods (093857 class)")
        print(
            f"  visual_holes_frac={fmt(perf.get('visual_holes_frac'))}  "
            f"(093857~0.60; want down)"
        )
        print(
            f"  empty_backlog_max={fmt(perf.get('empty_backlog_max'))}  "
            f"(093857=19; want down)"
        )
        print(
            f"  focus_missing_frac={fmt(perf.get('focus_missing_frac'))}  "
            f"(want <=0.3 after 5.6.2)"
        )
        print(
            f"  phase_abort_heavy_frac={fmt(perf.get('phase_abort_heavy_frac'))}  "
            f"abort_schedule_final_med={fmt(perf.get('abort_schedule_final_med'))}"
        )
        print(
            f"  focus_unfinished_ahead_tail_med="
            f"{fmt(perf.get('focus_unfinished_ahead_tail_med'))}  "
            f"(093857=10; want ->0)"
        )
        print(
            f"  miss_stuck_run_frames_tail_max="
            f"{fmt(perf.get('miss_stuck_run_frames_tail_max'))}  "
            f"gpu_kick_tail_med={fmt(perf.get('gpu_kick_tail_med'))}  "
            f"(093857 miss_stuck=1469; not climb with kick=0)"
        )
        print(
            f"  softdefer_age_max_tail_med="
            f"{fmt(perf.get('softdefer_age_max_tail_med'))}  "
            f"(no 5.5 regress)"
        )
        print(
            f"  wall_med={fmt(perf.get('wall_med'))}  "
            f"phase_med={fmt(perf.get('phase_med'))}  "
            f"(093857~98/69)"
        )
        print(
            f"  mesh_discarded_late_med={fmt(perf.get('mesh_discarded_late_med'))}  "
            f"cruise_sum={fmt(perf.get('mesh_discarded_late_max'))}  "
            f"abs_med={fmt(perf.get('mesh_discarded_late_abs_med'))}  "
            f"(093857 cruise~12 abs; want delta med<=2)"
        )
        print(
            f"  visible_black_focus_med={fmt(perf.get('visible_black_focus_med'))}  "
            f"max={fmt(perf.get('visible_black_focus_max'))}  "
            f"no_ticket_max={fmt(perf.get('vb_no_ticket_max'))}  "
            f"(093857 med~43 max~108)"
        )
        print(
            f"  opaque_cull_med={fmt(perf.get('opaque_cull_med'))}  "
            f"(093857~13; want down)"
        )


def evaluate_fidelity(
    report: dict,
    perf: dict | None,
    info: dict | None,
    teleport: bool | None,
) -> list[str]:
    fails = []
    if report.get("hang_killed"):
        fails.append("hang_killed=true")
    if teleport is True:
        fails.append("teleport_cruise=true forbidden for Phase57 gate")
    periods = (perf or {}).get("periods") or report.get("periods") or 0
    if int(periods) <= 0:
        fails.append("periods<=0")
    if info is not None:
        if info["settle_count"] <= 0:
            fails.append("no settle_reason in INFO")
        for s in info.get("soft_force_with_debt") or []:
            if s not in info.get("bad_live_soft_vis_debt", []) and not any(
                b["reason"] == s["reason"]
                and b["visibility_debt"] == s["visibility_debt"]
                for b in info.get("bad_live_soft_vis_debt") or []
            ):
                fails.append("soft_force+debt not marked BAD (scorecard bug)")
    return fails


def evaluate_product(info: dict | None, perf: dict | None) -> list[str]:
    """Product UX fails (Phase57: holes/empty/latch/miss_stuck)."""
    fails = []
    latch = None
    if perf:
        latch = perf.get("enter_settle_soft_force_with_debt_max")
    catch_up_armed = latch is not None and float(latch) > 0
    if info:
        if info.get("soft_force_with_debt"):
            if catch_up_armed:
                pass
            else:
                fails.append(
                    f"soft_force+visibility_debt>0 n={len(info['soft_force_with_debt'])} "
                    "(no PresentableCatchUp latch)"
                )
        if info.get("bad_live_soft_vis_debt") and not info.get("soft_force_with_debt"):
            fails.append(
                f"live/soft settle with vis_debt>0 n={len(info['bad_live_soft_vis_debt'])}"
            )
        if info.get("empty_batch_event_n", 0) > 0:
            fails.append(f"empty_batch_event={info['empty_batch_event_n']}")
    if perf:
        fm = perf.get("focus_missing_frac")
        if fm is not None and fm > 0.45:
            fails.append(f"focus_missing_frac={fm:.3g}>0.45")
        ab = perf.get("phase_abort_heavy_frac")
        if ab is not None and ab > 0.5:
            fm_ok = fm is not None and fm <= 0.3
            carve = perf.get("abort_schedule_final_med")
            carve_ok = carve is not None and float(carve) >= 2
            if not (fm_ok or carve_ok):
                fails.append(f"phase_abort_heavy_frac={ab:.3g}>0.5")
        holes = perf.get("visual_holes_frac")
        if holes is not None and float(holes) >= 0.50:
            fails.append(f"visual_holes_frac={holes:.3g}>=0.50 (093857 class)")
        empty_max = perf.get("empty_backlog_max")
        if empty_max is not None and float(empty_max) >= 15:
            fails.append(f"empty_backlog_max={empty_max:.0f}>=15 (093857 class)")
        clear_t = perf.get("latch_clear_t_ms")
        debt_tail = perf.get("visibility_debt_tail_med")
        if debt_tail is None:
            debt_tail = perf.get("visibility_debt_med")
        if catch_up_armed and clear_t is None:
            if debt_tail is None or float(debt_tail) > 36:
                fails.append(
                    "latch_never_cleared (drainable catch-up pending)"
                )
        miss_stuck = perf.get("miss_stuck_run_frames_tail_max")
        gpu_kick = perf.get("gpu_kick_tail_med")
        if (
            miss_stuck is not None
            and float(miss_stuck) >= 500
            and (gpu_kick is None or float(gpu_kick) <= 0)
        ):
            fails.append(
                f"miss_stuck_run_frames_tail_max={miss_stuck:.0f} with gpu_kick~0"
            )
        age = perf.get("softdefer_age_max_tail_med")
        stuck = perf.get("softdefer_stuck_n_tail_med")
        if (
            age is not None
            and stuck is not None
            and float(stuck) > 0
            and float(age) >= 1000
        ):
            fails.append(
                f"softdefer_age_max_tail_med={age:.0f} with stuck_n={stuck}"
            )
        disc = perf.get("mesh_discarded_late_med")
        if disc is not None and float(disc) >= 10:
            fails.append(f"mesh_discarded_late_med={disc:.3g}>=10 (093857 class)")
        vbnt = perf.get("vb_no_ticket_max")
        if vbnt is not None and float(vbnt) >= 95:
            fails.append(f"vb_no_ticket_max={vbnt:.0f}>=95 (093857 class ~91)")
        vb = perf.get("visible_black_focus_med")
        if vb is not None and float(vb) >= 40:
            fails.append(f"visible_black_focus_med={vb:.3g}>=40 (093857 class)")
        cull = perf.get("opaque_cull_med")
        if cull is not None and float(cull) >= 14.0:
            fails.append(f"opaque_cull_med={cull:.3g}>=14 (093857 class ~13)")
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", required=True)
    ap.add_argument("--perf")
    ap.add_argument("--info")
    ap.add_argument(
        "--baseline-manual",
        default="bin/logs/perf_20260907-093857_26724.jsonl",
        help="manual perf JSONL for delta (default: 093857 SoT)",
    )
    ap.add_argument(
        "--teleport",
        choices=["auto", "true", "false"],
        default="auto",
        help="teleport_cruise for fidelity FAIL (auto=read report)",
    )
    ap.add_argument("--tag", default="")
    ap.add_argument(
        "--expect-product-red",
        action="store_true",
        help="exit 0 if fidelity OK even when product FAIL (harness / early sprint)",
    )
    args = ap.parse_args()

    report_path = Path(args.report)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    metrics = report.get("metrics") or {}
    gates = report.get("gates") or {}

    print(f"=== Phase57 scorecard {args.tag} ===")
    print(f"report: {report_path}")
    print(
        f"hang_killed={report.get('hang_killed')} "
        f"pass={report.get('pass')} "
        f"holes_rate={metrics.get('holes_rate') or metrics.get('holes_rate_raw')} "
        f"wall_med={metrics.get('wall_ms_med')} "
        f"fly_wall={metrics.get('wall_ms_fly_med') or metrics.get('fly_only_wall_ms_med')} "
        f"phase={metrics.get('world_streaming_phase_ms_med')} "
        f"scene={metrics.get('scene_ms_med')} "
        f"unsync={metrics.get('pool_unsync_uploads_med')}"
    )
    hard = {
        k: gates.get(k)
        for k in (
            "wall_ms_fly_le_16_6",
            "scene_ms_le_5",
            "stream_phase_ms_le_5",
            "visual_holes_rate_le_0_10",
        )
        if k in gates
    }
    if hard:
        print(f"hard_gates: {hard}")

    perf = None
    perf_path = args.perf or report.get("perf_jsonl")
    if perf_path:
        p = Path(perf_path)
        if p.exists():
            perf = analyze_perf(p)
            print(f"perf: {p.name} periods={perf['periods']} cruise={perf['cruise_n']}")
            for k, v in perf.items():
                if k in ("periods", "cruise_n", "use_n", "stand_tail_n"):
                    continue
                print(f"  {k}={fmt(v)}")
        else:
            print(f"perf missing: {perf_path}")

    info = None
    if args.info:
        info = analyze_info(Path(args.info))
        print(
            f"info: settle_n={info['settle_count']} "
            f"empty_batch_event={info['empty_batch_event_n']} "
            f"bad_live_soft_vis_debt={len(info['bad_live_soft_vis_debt'])} "
            f"soft_force_with_debt={len(info['soft_force_with_debt'])}"
        )
        for s in info["settles"]:
            print(f"  settle {s}")
        for s in info["soft_force_with_debt"]:
            print(f"  FAIL soft_force+debt {s}")
        for s in info["bad_live_soft_vis_debt"]:
            if s not in info["soft_force_with_debt"]:
                print(f"  BAD {s}")

    baseline = dict(MANUAL_093857)
    baseline_label = "baked_093857"
    if args.baseline_manual:
        bp = Path(args.baseline_manual)
        if bp.exists():
            baseline = analyze_perf(bp)
            baseline_label = bp.name
        else:
            print(f"baseline-manual missing: {bp} (using baked 093857)", file=sys.stderr)

    if perf:
        print_delta(perf, baseline, baseline_label)

    print_control_checklist(perf, info, report)

    teleport = None
    if args.teleport == "true":
        teleport = True
    elif args.teleport == "false":
        teleport = False
    else:
        teleport = bool(report.get("teleport_cruise")) if "teleport_cruise" in report else None

    fid_fails = evaluate_fidelity(report, perf, info, teleport)
    prod_fails = evaluate_product(info, perf)

    print("=== fidelity ===")
    if fid_fails:
        for f in fid_fails:
            print(f"  FIDELITY_FAIL {f}")
    else:
        print("  FIDELITY_OK (harness sees settle/soft_force; no hang/teleport)")

    print("=== product ===")
    if prod_fails:
        for f in prod_fails:
            print(f"  PRODUCT_FAIL {f}")
    else:
        print("  PRODUCT_OK")

    if fid_fails:
        return 2
    if prod_fails and not args.expect_product_red:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
