#!/usr/bin/env python3
"""Build n01 scorecard JSON from perf JSONL (manual or autofly)."""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from flight_sim_run import (  # noqa: E402
    compute_dual_lane_stop_line,
    compute_eye_proxy_stop_line,
    compute_product_174657_proxy_adequacy,
)


def load_periods(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("{"):
            continue
        row = json.loads(line)
        if row.get("kind") == "period":
            rows.append(row)
    return rows


def med(xs: list[float]) -> float | None:
    if not xs:
        return None
    xs = sorted(xs)
    mid = len(xs) // 2
    return xs[mid] if len(xs) % 2 else (xs[mid - 1] + xs[mid]) / 2.0


def mid_third(rows: list[dict]) -> list[dict]:
    n = len(rows)
    if n < 3:
        return rows
    return rows[n // 3 : (2 * n) // 3]


def mid_corridor(rows: list[dict]) -> list[dict]:
    out = []
    for r in rows:
        try:
            cx = float(r.get("focus_cx", 0))
        except (TypeError, ValueError):
            continue
        if 2.0 <= cx <= 5.0:
            out.append(r)
    return out if out else rows


def val(row: dict, key: str) -> float | None:
    try:
        v = row.get(key)
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def build_scorecard(perf_path: Path, *, label: str, operator_visual: str | None) -> dict:
    periods = load_periods(perf_path)
    mid = mid_third(periods)
    mid_focus = mid_corridor(mid)

    def collect(key: str, subset: list[dict]) -> list[float]:
        return [v for r in subset if (v := val(r, key)) is not None]

    vb = collect("visible_black_focus_n", mid)
    stale = collect("draw_oracle_stale_vertex_light_n", mid)
    if not stale:
        stale = collect("stale_vertex_light_n", mid)
    stalled = collect("visible_black_fully_dark_stalled_n", mid)
    stale_rev = collect("stale_vl_rev_n", mid)
    census = collect("fully_dark_census_n", mid)
    unlit = collect("chunk_meshed_unlit", periods)
    holes = collect("near_focus_holes", mid)
    stale_vis = collect("mesh_apply_stale_visual", mid)
    if not stale_vis:
        stale_vis = collect("mesh_apply_stale", mid)
    stale_deltas = [
        abs(stale_vis[i] - stale_vis[i - 1]) for i in range(1, len(stale_vis))
    ]

    adequacy = compute_product_174657_proxy_adequacy(perf_path)
    stop_cold = compute_dual_lane_stop_line(perf_path, warm=False)
    stop_warm = compute_dual_lane_stop_line(perf_path, warm=True)
    eye_proxy = compute_eye_proxy_stop_line(perf_path)

    mid_stalled_med = med(stalled)
    stalled_gate = mid_stalled_med is not None and mid_stalled_med <= 5.0

    return {
        "label": label,
        "perf_jsonl": str(perf_path.relative_to(ROOT))
        if perf_path.is_relative_to(ROOT)
        else str(perf_path),
        "periods": len(periods),
        "operator_visual": operator_visual,
        "mid_corridor": {
            "focus_cx_band": "[2,5]",
            "visible_black_focus_med": med(vb),
            "stale_vertex_light_med": med(stale),
            "stale_vl_rev_med": med(stale_rev),
            "fully_dark_census_med": med(census),
            "fully_dark_stalled_med": mid_stalled_med,
            "near_focus_holes_med": med(holes),
            "unlit_max": max(unlit) if unlit else None,
            "mesh_apply_stale_visual_med": med(stale_vis),
            "mesh_apply_stale_visual_delta_med": med(stale_deltas),
            "publication_incomplete_material_med": med(
                collect("publication_incomplete_material_n", mid_focus)
            ),
            "publication_oom_retain_med": med(
                collect("publication_oom_retain_n", mid_focus)
            ),
            "publication_overload_retain_med": med(
                collect("publication_overload_retain_n", mid_focus)
            ),
            "rows_mid": len(mid),
            "rows_mid_focus": len(mid_focus),
        },
        "proxy_adequacy": adequacy,
        "dual_lane_stop_line_cold": stop_cold,
        "dual_lane_stop_line_warm": stop_warm,
        "eye_proxy_stop_line": eye_proxy,
        "mid_stalled_gate_pass": stalled_gate,
        # Four signals: adequacy / dual-lane / eye_proxy / operator eye (manual).
        "merge_green": bool(
            adequacy.get("adequacy_pass")
            and stop_cold.get("dual_lane_stop_line_pass")
            and eye_proxy.get("eye_proxy_stop_line_pass")
            and stalled_gate
            and (operator_visual is None or operator_visual == "PASS")
        ),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("perf_jsonl", type=Path)
    ap.add_argument("-o", "--output", type=Path, required=True)
    ap.add_argument("--label", default="manual")
    ap.add_argument("--operator-visual", default=None)
    args = ap.parse_args()
    out = build_scorecard(args.perf_jsonl, label=args.label, operator_visual=args.operator_visual)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
