#!/usr/bin/env python3
"""P16 hole audit: miss sticky + moving unf/nm vs SoT 221516.

Usage:
  python bin/tmp_audit_221516_holes.py [perf.jsonl ...]
  python bin/tmp_audit_221516_holes.py <new> bin/logs/perf_20260825-221516_28600.jsonl

Gates (vs first SoT / 221516 baseline when present):
  miss_sticky_frac  ↓ ≥30% relative (e.g. ~1.0 → ≤0.70)
  moving_unf_p90    ↓
  moving_nm_p90     ↓
  SoftDefer standstill floor/spike ≤8, Site B retarget ≤5 (informational here;
    use tmp_audit_ne_frontier_stand.py for SoftDefer KEEP)
"""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "bin" / "logs"
SOT_221516 = LOGS / "perf_20260825-221516_28600.jsonl"


def load(p: Path):
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def percentile(xs, p):
    xs = sorted(float(x) for x in xs)
    if not xs:
        return None
    if len(xs) == 1:
        return xs[0]
    k = (len(xs) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(xs) - 1)
    if f == c:
        return xs[f]
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def moving_land_rows(u):
    out = []
    for i in range(1, len(u)):
        if (u[i].get("player_x"), u[i].get("player_z")) == (
            u[i - 1].get("player_x"),
            u[i - 1].get("player_z"),
        ):
            continue
        if int(u[i].get("keep_cols") or 0) < 160:
            continue
        out.append(u[i])
    return out


def rate(rows, key):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)
    return d / max(1, len(rows) - 1)


def analyze(label: str, path: Path) -> dict:
    u = load(path)
    mv = moving_land_rows(u)
    print(f"\n=== {label} spikes={len(u)} land_moves={len(mv)} ===")
    if len(mv) < 5:
        keep_med = (
            st.median([float(r.get("keep_cols") or 0) for r in u]) if u else None
        )
        print(f"  ABORT: too few land moves (keep_med={keep_med})")
        return {
            "label": label,
            "path": str(path),
            "pass": False,
            "abort": True,
            "keep_med": keep_med,
        }

    miss_sticky = sum(1 for r in mv if int(r.get("focus_missing_mesh") or 0) == 1) / len(
        mv
    )
    unf = [float(r.get("unfinished_visual") or 0) for r in mv]
    nm = [float(r.get("column_loaded_no_mesh_n") or 0) for r in mv]
    uh = [float(r.get("chunk_meshed_unlit_hidden") or 0) for r in mv]
    unlit_late = [
        float(r.get("chunk_meshed_unlit_hidden") or 0)
        for r in (u[-40:] if len(u) >= 40 else u)
    ]
    keep_med = st.median([float(r.get("keep_cols") or 0) for r in mv])

    unf_p90 = percentile(unf, 90)
    nm_p90 = percentile(nm, 90)
    uh_med = st.median(uh)
    unlit_late_med = st.median(unlit_late) if unlit_late else None

    # SoftDefer rest window (informational for cruise logs)
    rest = u[60:] if len(u) > 60 else u
    site_b = rate(rest, "softdefer_capture_retarget_n")
    floor_r = rate(rest, "softdefer_capture_floor_hits")

    print(f"  keep_med={keep_med}")
    print(f"  miss_sticky_frac={miss_sticky:.3f}")
    print(f"  moving_unf_p90={unf_p90:.1f} moving_nm_p90={nm_p90:.1f} uh_med={uh_med}")
    print(f"  unlit_late_med={unlit_late_med}")
    print(
        f"  (info) rest floor/spike={floor_r} SiteB retarget/spike={site_b}"
        if floor_r is not None
        else "  (info) SoftDefer rates N/A"
    )

    return {
        "label": label,
        "path": str(path),
        "abort": False,
        "keep_med": keep_med,
        "miss_sticky_frac": miss_sticky,
        "moving_unf_p90": unf_p90,
        "moving_nm_p90": nm_p90,
        "uh_med": uh_med,
        "unlit_late_med": unlit_late_med,
        "site_b_retarget": site_b,
        "floor_per_spike": floor_r,
        "n_land_moves": len(mv),
    }


def gate_vs_baseline(cur: dict, base: dict) -> bool:
    print("\n=== P16 hole gates vs baseline ===")
    if cur.get("abort") or base.get("abort"):
        print("  FAIL abort (keep med <160 or too few moves)")
        return False

    ok = True
    b_miss = base["miss_sticky_frac"]
    c_miss = cur["miss_sticky_frac"]
    # ↓ ≥30% relative → cur <= 0.70 * baseline
    miss_tgt = b_miss * 0.70
    miss_ok = c_miss <= miss_tgt + 1e-9
    print(
        f"  {'PASS' if miss_ok else 'FAIL':7s} miss_sticky_frac "
        f"got={c_miss:.3f} tgt<={miss_tgt:.3f} (base={b_miss:.3f}, -30% rel)"
    )
    ok = ok and miss_ok

    for name, ck, bk in [
        ("moving_unf_p90 down", "moving_unf_p90", "moving_unf_p90"),
        ("moving_nm_p90 down", "moving_nm_p90", "moving_nm_p90"),
    ]:
        cv, bv = cur[ck], base[bk]
        stt = "PASS" if cv is not None and bv is not None and cv < bv else "FAIL"
        if stt == "FAIL":
            ok = False
        print(f"  {stt:7s} {name:22s} got={cv} base={bv}")

    keep_ok = (cur.get("keep_med") or 0) >= 160
    print(f"  {'PASS' if keep_ok else 'FAIL':7s} keep_med >=160 got={cur.get('keep_med')}")
    ok = ok and keep_ok

    print(f"  OVERALL {'PASS' if ok else 'FAIL'}")
    return ok


def main():
    args = [Path(a) for a in sys.argv[1:]]
    if not args:
        args = [SOT_221516] if SOT_221516.is_file() else []
        newest = sorted(
            LOGS.glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True
        )
        if newest and newest[0] != SOT_221516:
            args = [newest[0], SOT_221516] if SOT_221516.is_file() else [newest[0]]
    if not args:
        print("no perf logs", file=sys.stderr)
        return 1

    outs = []
    for p in args:
        lab = p.stem
        if "221516" in p.name:
            lab = "SoT 221516"
        outs.append(analyze(lab, p))

    base = next((o for o in outs if "221516" in o["path"] or "SoT" in o["label"]), None)
    cur = outs[0]
    if base and cur is not base:
        return 0 if gate_vs_baseline(cur, base) else 1
    if len(outs) == 1 and "221516" in outs[0]["path"]:
        print("\n(baseline only — no compare)")
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
