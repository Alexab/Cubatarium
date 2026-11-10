#!/usr/bin/env python3
"""Phase 5.5 scorecard: honest soft_force+debt FAIL + auto↔manual delta.

Usage:
  python tools/AnalyzePhase55Scorecard.py \\
    --report bin/suite_reports/phase55_v0_flyheavy.json \\
    [--perf bin/logs/perf_....jsonl] \\
    [--info bin/logs/...INFO...] \\
    [--baseline-manual bin/logs/perf_20260906-170813_25272.jsonl] \\
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

# Manual SoT 170813 cruise class (for delta thresholds / display).
MANUAL_170813 = {
    "focus_missing_frac": 1.0,
    "phase_abort_heavy_frac": 1.0,
    "wall_med": 116.3,
    "phase_med": 70.5,
    "pool_unsync_med": 26.0,
    "softdefer_stuck_n_med": 0.0,  # cruise; exit stand was 8
    "softdefer_age_max_med": None,
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


def analyze_perf(path: Path):
    periods = load_periods(path)
    cr = cruise(periods)
    use = cr if len(cr) >= 3 else periods
    tail = stand_tail(periods)

    def series(key, rows=None):
        rows = use if rows is None else rows
        return [g(r, key) for r in rows]

    def series_max(key, rows=None):
        xs = [float(x) for x in series(key, rows) if x is not None]
        return max(xs) if xs else None

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
        "gpu_kick_med": median(series("gpu_kick_n")),
        "relight_fifo_med": median(series("relight_fifo_n")),
    }


def analyze_info(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    settles = []
    for m in SETTLE_RE.finditer(text):
        settles.append({"reason": m.group(1), "visibility_debt": int(m.group(2))})

    # Phase 5.5 honesty: soft_force + vis_debt>0 is FAIL (no longer excluded).
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
        "wall_med",
        "phase_med",
        "pool_unsync_med",
        "softdefer_stuck_n_med",
        "softdefer_age_max_med",
        "softdefer_stuck_n_tail_med",
        "softdefer_age_max_tail_med",
        "scene_med",
        "empty_backlog_med",
    ]
    print(f"=== auto<->manual delta ({label}) ===")
    print(f"{'metric':32} {'auto':>12} {'manual':>12} {'delta':>12}")
    for k in keys:
        a = auto.get(k)
        m = baseline.get(k)
        if a is None and m is None:
            continue
        d = None
        if isinstance(a, (int, float)) and isinstance(m, (int, float)):
            d = float(a) - float(m)
        print(f"{k:32} {fmt(a):>12} {fmt(m):>12} {fmt(d):>12}")


def evaluate_fidelity(
    report: dict,
    perf: dict | None,
    info: dict | None,
    teleport: bool | None,
) -> list[str]:
    """Return list of FAIL reasons (empty => fidelity harness OK for 5.5.0).

    Product may still be red (soft_force debt) — that is honest FAIL for product,
    but fidelity pass requires the harness *sees* it (soft_force_with_debt logged).
    """
    fails = []
    if report.get("hang_killed"):
        fails.append("hang_killed=true")
    if teleport is True:
        fails.append("teleport_cruise=true forbidden for Phase55 gate")
    periods = (perf or {}).get("periods") or report.get("periods") or 0
    if int(periods) <= 0:
        fails.append("periods<=0")
    if info is not None:
        if info["settle_count"] <= 0:
            fails.append("no settle_reason in INFO")
        # Harness honesty: if soft_force+debt present, must be in soft_force_with_debt
        # (always true by construction) AND counted as bad.
        for s in info.get("soft_force_with_debt") or []:
            if s not in info.get("bad_live_soft_vis_debt", []) and not any(
                b["reason"] == s["reason"]
                and b["visibility_debt"] == s["visibility_debt"]
                for b in info.get("bad_live_soft_vis_debt") or []
            ):
                fails.append("soft_force+debt not marked BAD (scorecard bug)")
    return fails


def evaluate_product(info: dict | None, perf: dict | None) -> list[str]:
    """Product UX fails (expected red until PresentableCatchUp drains debt)."""
    fails = []
    latch = None
    if perf:
        latch = perf.get("enter_settle_soft_force_with_debt_max")
    catch_up_armed = latch is not None and float(latch) > 0
    if info:
        if info.get("soft_force_with_debt"):
            if catch_up_armed:
                # Phase 5.5.1: soft_force+debt allowed only with PresentableCatchUp latch.
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
        if fm is not None and fm > 0.3:
            fails.append(f"focus_missing_frac={fm:.3g}>0.3")
        ab = perf.get("phase_abort_heavy_frac")
        if ab is not None and ab > 0.5:
            fm_ok = fm is not None and fm <= 0.3
            carve = perf.get("abort_schedule_final_med")
            carve_ok = carve is not None and float(carve) >= 2
            if not (fm_ok or carve_ok):
                fails.append(f"phase_abort_heavy_frac={ab:.3g}>0.5")
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
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", required=True)
    ap.add_argument("--perf")
    ap.add_argument("--info")
    ap.add_argument(
        "--baseline-manual",
        default="",
        help="manual perf JSONL for delta (default: baked 170813 numbers if omitted)",
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
        help="exit 0 if fidelity OK even when product FAIL (5.5.0 harness sprint)",
    )
    args = ap.parse_args()

    report_path = Path(args.report)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    metrics = report.get("metrics") or {}
    gates = report.get("gates") or {}

    print(f"=== Phase55 scorecard {args.tag} ===")
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

    baseline = dict(MANUAL_170813)
    baseline_label = "baked_170813"
    if args.baseline_manual:
        bp = Path(args.baseline_manual)
        if bp.exists():
            baseline = analyze_perf(bp)
            baseline_label = bp.name
        else:
            print(f"baseline-manual missing: {bp}", file=sys.stderr)

    if perf:
        print_delta(perf, baseline, baseline_label)

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
