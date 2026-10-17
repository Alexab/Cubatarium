#!/usr/bin/env python3
"""One-shot mid-third metric slice for N01 regress analysis."""
from __future__ import annotations

import json
import statistics
from glob import glob
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KEYS = [
    "visible_black_focus_n",
    "stale_vertex_light_n",
    "stale_vl_rev_n",
    "fully_dark_census_n",
    "visible_black_fully_dark_stalled",
    "visible_black_fully_dark_stalled_n",
    "unlit_max",
    "near_focus_holes",
    "prep_spawn_ring_query_ms",
    "streamer_update_ms",
    "mesh_emerge_prep_ms",
    "prep_schedule_policy_ms",
    "wall_ms",
]


def load_periods(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except json.JSONDecodeError:
                continue
            if not isinstance(o, dict):
                continue
            if o.get("type") not in (None, "period", "Period"):
                # keep rows that look like period samples
                if "visible_black_focus_n" not in o and "wall_ms" not in o:
                    continue
            rows.append(o)
    return rows


def mid_third(rows: list[dict]) -> list[dict]:
    n = len(rows)
    if n < 3:
        return rows
    return rows[n // 3 : (2 * n) // 3]


def summarize(rows: list[dict]) -> dict:
    out = {}
    for k in KEYS:
        vals = []
        for r in rows:
            v = r.get(k)
            if v is None:
                continue
            try:
                vals.append(float(v))
            except (TypeError, ValueError):
                pass
        if not vals:
            continue
        vals_s = sorted(vals)
        out[k] = {
            "med": statistics.median(vals),
            "p90": vals_s[int(0.9 * (len(vals_s) - 1))],
            "max": max(vals),
            "n": len(vals),
        }
    return out


def print_flight(label: str, path: Path) -> None:
    rows = load_periods(path)
    mid = mid_third(rows)
    st = summarize(mid)
    print(f"=== {label}  {path.name}  periods={len(rows)} mid={len(mid)}")
    for k, v in st.items():
        print(f"  {k}: med={v['med']:.3f} p90={v['p90']:.3f} max={v['max']:.3f} n={v['n']}")


def main() -> None:
    flights = {
        "manual_085208": ROOT / "bin/logs/perf_20260915-085208_27820.jsonl",
        "manual_200927": glob(str(ROOT / "bin/logs/perf_20260914-200927_*.jsonl")),
        "manual_205048": glob(str(ROOT / "bin/logs/perf_20260914-205048_*.jsonl")),
        "manual_205626": glob(str(ROOT / "bin/logs/perf_20260914-205626_*.jsonl")),
        "manual_154921": glob(str(ROOT / "bin/logs/perf_20260914-154921_*.jsonl")),
    }
    for label, p in flights.items():
        path = Path(p[0]) if isinstance(p, list) and p else (p if isinstance(p, Path) else None)
        if path is None or not path.exists():
            print(f"=== {label} MISSING")
            continue
        print_flight(label, path)

    print("\n=== autofly wall_diet reports (pre-N01)")
    for jf in sorted((ROOT / "bin/suite_reports/g1_a10_relight").glob("wall_diet_*.json")):
        o = json.loads(jf.read_text(encoding="utf-8"))
        adeq = o.get("adequacy_pass")
        dl = o.get("dual_lane_stop_line_pass")
        # nested analyzer
        metrics = o.get("metrics") or o.get("summary") or {}
        print(f"{jf.name}: adequacy={adeq} dual_lane={dl} keys={list(o)[:12]}")
        for k in (
            "visible_black_focus_n_median",
            "stale_vertex_light_n_median",
            "unlit_max",
            "visible_black_median",
        ):
            if k in o:
                print(f"  {k}={o[k]}")
        # walk shallow for medians of interest
        for section in ("period_medians", "gates", "oracle", "cruise", "segments"):
            if section in o and isinstance(o[section], dict):
                interesting = {
                    kk: vv
                    for kk, vv in o[section].items()
                    if any(
                        x in kk.lower()
                        for x in ("black", "stale", "unlit", "stalled", "vb", "hole")
                    )
                }
                if interesting:
                    print(f"  {section}: {interesting}")


if __name__ == "__main__":
    main()
