#!/usr/bin/env python3
"""Phase 5.4 scorecard: suite report + cruise periods + INFO settle.

Usage:
  python tools/AnalyzePhase54Scorecard.py \\
    --report bin/suite_reports/phase54_v1_fz.json \\
    [--perf bin/logs/perf_....jsonl] \\
    [--info bin/logs/...INFO...] \\
    [--tag v1]
"""
from __future__ import annotations

import argparse
import json
import re
import statistics as st
from pathlib import Path

SETTLE_RE = re.compile(
    r"settle_reason=(\S+).*?visibility_debt=(\d+)", re.IGNORECASE
)
EMPTY_BATCH_RE = re.compile(r"empty_batch_event")


def median(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


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

    def series(key):
        return [g(r, key) for r in use]

    focus_key = "focus_missing_mesh"
    abort_key = "phase_abort_heavy"
    return {
        "periods": len(periods),
        "cruise_n": len(cr),
        "use_n": len(use),
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
        )
        and not s["reason"].startswith("soft_force")
        and s["reason"] != "abort_underfeet"
    ]
    return {
        "settle_count": len(settles),
        "settles": settles[-8:],
        "bad_live_soft_vis_debt": bad,
        "empty_batch_event_n": len(EMPTY_BATCH_RE.findall(text)),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--report", required=True)
    ap.add_argument("--perf")
    ap.add_argument("--info")
    ap.add_argument("--tag", default="")
    args = ap.parse_args()

    report_path = Path(args.report)
    report = json.loads(report_path.read_text(encoding="utf-8"))
    metrics = report.get("metrics") or {}
    gates = report.get("gates") or {}

    print(f"=== Phase54 scorecard {args.tag} ===")
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

    perf_path = args.perf or report.get("perf_jsonl")
    if perf_path:
        p = Path(perf_path)
        if p.exists():
            a = analyze_perf(p)
            print(f"perf: {p.name} periods={a['periods']} cruise={a['cruise_n']}")
            for k, v in a.items():
                if k in ("periods", "cruise_n", "use_n"):
                    continue
                if isinstance(v, float):
                    print(f"  {k}={v:.4g}")
                else:
                    print(f"  {k}={v}")
        else:
            print(f"perf missing: {perf_path}")

    if args.info:
        info = analyze_info(Path(args.info))
        print(
            f"info: settle_n={info['settle_count']} "
            f"empty_batch_event={info['empty_batch_event_n']} "
            f"bad_live_soft_vis_debt={len(info['bad_live_soft_vis_debt'])}"
        )
        for s in info["settles"]:
            print(f"  settle {s}")
        for s in info["bad_live_soft_vis_debt"]:
            print(f"  BAD {s}")


if __name__ == "__main__":
    main()
