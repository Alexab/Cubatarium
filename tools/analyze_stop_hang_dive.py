#!/usr/bin/env python3
"""Post-analyze dive-stop hang (SoT 210431): unload/keep under FrameDeadline.

Segments: fly_west, dive, stop_uw.
Scores variants for U/K bake-off ranking.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any


SEA_LEVEL_DEFAULT = 62.0
SPD_STOP = 2.0


def _f(row: dict, *keys: str, default: float = 0.0) -> float:
    for k in keys:
        if k in row and row[k] is not None:
            try:
                return float(row[k])
            except (TypeError, ValueError):
                pass
    return default


def _i(row: dict, *keys: str, default: int = 0) -> int:
    return int(_f(row, *keys, default=float(default)))


def load_periods(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open(encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if obj.get("kind") != "period":
                continue
            rows.append(obj)
    return rows


def classify(rows: list[dict], sea: float) -> dict[str, list[dict]]:
    fly: list[dict] = []
    dive: list[dict] = []
    stop_uw: list[dict] = []
    prev_cx: float | None = None
    for r in rows:
        spd = _f(r, "movement_speed", "spd", "speed")
        y = _f(r, "player_y", "eye_y", "y")
        cx = _f(r, "focus_cx", "cx", "chunk_x")
        underwater = y < sea - 2.0
        moving = spd > SPD_STOP
        cx_decreasing = prev_cx is not None and cx < prev_cx - 0.05
        if moving and (cx_decreasing or (prev_cx is None and not underwater)):
            fly.append(r)
        elif underwater and moving:
            dive.append(r)
        elif underwater and not moving:
            stop_uw.append(r)
        elif not underwater and moving:
            fly.append(r)
        prev_cx = cx
    return {"fly_west": fly, "dive": dive, "stop_uw": stop_uw}


def seg_metrics(seg: list[dict], label: str) -> dict[str, Any]:
    if not seg:
        return {
            "label": label,
            "n": 0,
            "max_wall_ms": 0.0,
            "streamer_unload_ms_max": 0.0,
            "streamer_keep_shell_ms_max": 0.0,
            "streamer_core_ms_max": 0.0,
            "update_streaming_ms_max": 0.0,
            "stream_loads_sum": 0,
            "stream_load_candidates_sum": 0,
            "chunk_count_end": 0,
            "player_y_med": None,
            "cx_end": None,
        }

    def mx(*keys: str) -> float:
        return max(_f(r, *keys) for r in seg)

    walls = [_f(r, "max_wall_ms", "wall_ms") for r in seg]
    ys = [_f(r, "player_y", "eye_y", "y") for r in seg]
    ys_sorted = sorted(ys)
    med_y = ys_sorted[len(ys_sorted) // 2]
    loads = sum(_i(r, "stream_loads", "loads") for r in seg)
    cands = sum(
        _i(r, "stream_load_candidates", "load_candidates") for r in seg
    )
    last = seg[-1]
    return {
        "label": label,
        "n": len(seg),
        "max_wall_ms": max(walls) if walls else 0.0,
        "streamer_unload_ms_max": mx("streamer_unload_ms"),
        "streamer_keep_shell_ms_max": mx("streamer_keep_shell_ms"),
        "streamer_core_ms_max": mx("streamer_core_ms", "streamer_update_ms"),
        "update_streaming_ms_max": mx("update_streaming_ms"),
        "stream_loads_sum": loads,
        "stream_load_candidates_sum": cands,
        "chunk_count_end": _i(last, "chunk_count", "chunks"),
        "player_y_med": med_y,
        "cx_end": _f(last, "focus_cx", "cx", "chunk_x"),
    }


def score_stop(
    m: dict[str, Any],
    *,
    eye_pass: bool,
    chunk_growth: float,
    dual_flip: int,
) -> float:
    """Lower is better. Penalize eye fail / unbounded chunk growth / dual-flip."""
    w1, w2, w3 = 1.0, 2.0, 2.0
    s = (
        w1 * float(m["max_wall_ms"])
        + w2 * float(m["streamer_unload_ms_max"])
        + w3 * float(m["streamer_keep_shell_ms_max"])
    )
    if not eye_pass:
        s += 1.0e6
    if chunk_growth > 80:
        s += 500.0 + chunk_growth
    if dual_flip > 0:
        s += 1.0e5 * dual_flip
    return s


def analyze_one(
    perf: Path,
    *,
    sea: float,
    report: dict | None,
    label: str,
) -> dict[str, Any]:
    rows = load_periods(perf)
    segs = classify(rows, sea)
    metrics = {k: seg_metrics(v, k) for k, v in segs.items()}
    stop = metrics["stop_uw"]
    fly = metrics["fly_west"]
    dive = metrics["dive"]

    chunk_start = 0
    if rows:
        chunk_start = _i(rows[0], "chunk_count", "chunks")
    chunk_end = stop["chunk_count_end"] or (
        _i(rows[-1], "chunk_count", "chunks") if rows else 0
    )
    chunk_growth = float(chunk_end - chunk_start)

    eye_pass = True
    dual_flip = 0
    west_covered = False
    if report:
        eye_pass = bool(report.get("eye_proxy_pass", report.get("eye_pass", True)))
        dual_flip = int(
            report.get("dual_lane_flips", report.get("dual_flip", 0)) or 0
        )
        west_covered = bool(
            report.get("west_route_covered", report.get("west_covered", False))
        )
        if "metrics" in report and isinstance(report["metrics"], dict):
            m = report["metrics"]
            if "eye_proxy_pass" in m:
                eye_pass = bool(m["eye_proxy_pass"])
            if "dual_lane_flips" in m:
                dual_flip = int(m["dual_lane_flips"] or 0)

    # Coverage from perf if report silent.
    if fly["n"] >= 3 and fly.get("cx_end") is not None:
        if float(fly["cx_end"]) <= -3.0:
            west_covered = True
    dive_ok = dive["n"] >= 1 or (
        stop["n"] >= 3 and stop.get("player_y_med") is not None
        and float(stop["player_y_med"]) < sea - 2.0
    )
    stop_ok = stop["n"] >= 3
    coverage = {
        "west_covered": west_covered,
        "dive_reached": dive_ok,
        "stop_uw_periods": stop["n"],
        "stop_uw_ok": stop_ok,
        "untested": not (west_covered and dive_ok and stop_ok),
    }

    sc = score_stop(
        stop, eye_pass=eye_pass and not coverage["untested"], chunk_growth=chunk_growth, dual_flip=dual_flip
    )
    return {
        "label": label,
        "perf": str(perf),
        "sea_level": sea,
        "coverage": coverage,
        "segments": metrics,
        "chunk_count_start": chunk_start,
        "chunk_count_end": chunk_end,
        "chunk_growth": chunk_growth,
        "eye_pass": eye_pass,
        "dual_flip": dual_flip,
        "score": sc,
        "goals": {
            "max_wall_ms_lt": 200.0,
            "unload_max_lt": 50.0,
            "keep_max_lt": 30.0,
            "vs_sot_wall": 1500.0,
        },
        "pass_af_targets": (
            not coverage["untested"]
            and float(stop["max_wall_ms"]) < 200.0
            and float(stop["streamer_unload_ms_max"]) < 50.0
            and float(stop["streamer_keep_shell_ms_max"]) < 30.0
            and eye_pass
            and dual_flip == 0
        ),
    }


def rank_variants(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    tested = [r for r in results if not r["coverage"]["untested"]]
    tested.sort(key=lambda r: r["score"])
    return tested


def print_table(results: list[dict[str, Any]]) -> None:
    print(
        f"{'label':<28} {'score':>10} {'wall':>8} {'unload':>8} {'keep':>8} "
        f"{'core':>8} {'stop_n':>6} {'y_med':>7} {'chunks_d':>8} cov"
    )
    for r in results:
        s = r["segments"]["stop_uw"]
        cov = r["coverage"]
        flag = "UNTESTED" if cov["untested"] else ("PASS" if r["pass_af_targets"] else "FAIL")
        y = s["player_y_med"]
        y_s = f"{y:.1f}" if y is not None else "-"
        print(
            f"{r['label']:<28} {r['score']:10.1f} {s['max_wall_ms']:8.1f} "
            f"{s['streamer_unload_ms_max']:8.1f} {s['streamer_keep_shell_ms_max']:8.1f} "
            f"{s['streamer_core_ms_max']:8.1f} {s['n']:6d} {y_s:>7} "
            f"{r['chunk_growth']:8.0f} {flag}"
        )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("perf", nargs="*", type=Path, help="perf JSONL path(s)")
    ap.add_argument("--report", type=Path, action="append", default=[],
                    help="optional AF report JSON (aligned with perf order)")
    ap.add_argument("--label", action="append", default=[],
                    help="variant labels (aligned with perf)")
    ap.add_argument("--sea-level", type=float, default=SEA_LEVEL_DEFAULT)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--baseline", type=Path, default=None,
                    help="write/compare baseline JSON")
    args = ap.parse_args()
    if not args.perf:
        print("need at least one perf JSONL", file=sys.stderr)
        return 2

    results: list[dict[str, Any]] = []
    for i, perf in enumerate(args.perf):
        report = None
        if i < len(args.report) and args.report[i].is_file():
            report = json.loads(args.report[i].read_text(encoding="utf-8"))
        label = args.label[i] if i < len(args.label) else perf.stem
        results.append(
            analyze_one(perf, sea=args.sea_level, report=report, label=label)
        )

    ranked = rank_variants(results)
    out_obj: dict[str, Any] = {
        "results": results,
        "ranked": [r["label"] for r in ranked],
        "winner": ranked[0]["label"] if ranked else None,
    }
    if args.baseline and args.baseline.is_file():
        base = json.loads(args.baseline.read_text(encoding="utf-8"))
        out_obj["baseline_path"] = str(args.baseline)
        out_obj["baseline_score"] = base.get("score") or (
            (base.get("results") or [{}])[0].get("score")
        )

    print_table(results)
    if ranked:
        print(f"winner: {ranked[0]['label']} score={ranked[0]['score']:.1f}")
    else:
        print("winner: none (all UNTESTED)")

    out_path = args.out
    if out_path is None and args.baseline and not args.baseline.is_file():
        out_path = args.baseline
    if out_path:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        # If writing baseline, store first result as canonical.
        payload = results[0] if args.baseline and out_path == args.baseline else out_obj
        out_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {out_path}")

    # Exit 0 always for analysis; harness interprets coverage.untested.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
