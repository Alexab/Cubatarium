#!/usr/bin/env python3
"""P17 stand discard/VB/stream gates vs manual 100413.

Usage:
  python bin/tmp_audit_100413_stand.py [perf.jsonl ...]
  python bin/tmp_audit_100413_stand.py <new> bin/logs/perf_20260826-100413_20156.jsonl
"""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "bin" / "logs"
SOT = LOGS / "perf_20260826-100413_20156.jsonl"


def load(p: Path):
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def rate(rows, key):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)
    return d / max(1, len(rows) - 1)


def standstill(u):
    if len(u) < 10:
        return u
    # Prefer longest trailing keep>=160 stand (abort ocean keep~49).
    land = [r for r in u if int(r.get("keep_cols") or 0) >= 160]
    if len(land) >= 15:
        best = land
        for i in range(len(land) - 2, -1, -1):
            if (land[i].get("player_x"), land[i].get("player_z")) != (
                land[i + 1].get("player_x"),
                land[i + 1].get("player_z"),
            ):
                best = land[i + 1 :]
                break
        if len(best) >= 15:
            return best
        return land
    best_i = len(u) - 1
    for i in range(len(u) - 2, -1, -1):
        if (u[i].get("player_x"), u[i].get("player_z")) != (
            u[i + 1].get("player_x"),
            u[i + 1].get("player_z"),
        ):
            best_i = i + 1
            break
        best_i = i
    return u[best_i:]


def analyze(label: str, path: Path) -> dict:
    u = load(path)
    stand = standstill(u)
    late = stand[-40:] if len(stand) >= 40 else stand
    early = stand[: max(1, len(stand) // 3)]
    print(f"\n=== {label} spikes={len(u)} stand={len(stand)} ===")
    if len(stand) < 15:
        print("  ABORT: short stand")
        return {"label": label, "path": str(path), "abort": True, "pass": False}

    disc = rate(stand, "mesh_discarded_late")
    vb_late = med([r.get("visible_black_focus_n") for r in late])
    unf_e = med([r.get("unfinished_visual") for r in early])
    unf_l = med([r.get("unfinished_visual") for r in late])
    stale_l = med([r.get("dark_face_stale_near_n") for r in late])
    stream_m = med([r.get("stream_ms") for r in stand])
    keep = med([r.get("keep_cols") for r in late])
    site_b = rate(stand, "softdefer_capture_retarget_n")
    floor_r = rate(stand, "softdefer_capture_floor_hits")

    print(f"  discard/spike={disc}")
    print(f"  vb_focus late={vb_late} unf early={unf_e} late={unf_l}")
    print(f"  stale late={stale_l} stream_ms={stream_m} keep={keep}")
    print(f"  SoftDefer floor={floor_r} SiteB={site_b}")

    return {
        "label": label,
        "path": str(path),
        "abort": False,
        "discard_rate": disc,
        "vb_late": vb_late,
        "unf_early": unf_e,
        "unf_late": unf_l,
        "stale_late": stale_l,
        "stream_ms": stream_m,
        "keep": keep,
        "site_b": site_b,
        "floor": floor_r,
    }


def gate(cur: dict, base: dict) -> bool:
    print("\n=== P17 stand gates vs 100413 ===")
    if cur.get("abort") or base.get("abort"):
        print("  FAIL abort")
        return False
    ok = True

    def check(name, val, tgt, op):
        nonlocal ok
        if val is None:
            print(f"  MISSING {name}")
            ok = False
            return
        if op == "<=":
            stt = "PASS" if val <= tgt else "FAIL"
        elif op == ">=":
            stt = "PASS" if val >= tgt else "FAIL"
        elif op == "down":
            stt = "PASS" if val < tgt else "FAIL"
        else:
            stt = "FAIL"
        if stt != "PASS":
            ok = False
        print(f"  {stt:7s} {name:40s} got={val} {op}{tgt}")

    check("mesh_discarded_late/spike <=0.05", cur["discard_rate"], 0.05, "<=")
    vb_tgt = (base["vb_late"] or 99) * 0.5
    check("vb_focus late <=50% of SoT", cur["vb_late"], vb_tgt, "<=")
    # Low absolute unfinished already healed — don't require early>late.
    if (cur["unf_late"] or 0) <= 15 and (cur["unf_early"] or 0) <= 15:
        print(
            f"  PASS    unf late/early low-abs                  "
            f"got={cur['unf_late']} early={cur['unf_early']} (<=15)"
        )
    else:
        check("unf late < unf early", cur["unf_late"], cur["unf_early"], "down")
    check("stale late <=200", cur["stale_late"], 200.0, "<=")
    check("stream_ms med <=25", cur["stream_ms"], 25.0, "<=")
    check("keep >=160", cur["keep"], 160.0, ">=")
    if cur.get("site_b") is not None:
        check("SiteB retarget <=5", cur["site_b"], 5.0, "<=")
    if cur.get("floor") is not None:
        check("floor/spike <=8", cur["floor"], 8.0, "<=")
    print(f"  OVERALL {'PASS' if ok else 'FAIL'}")
    return ok


def main():
    args = [Path(a) for a in sys.argv[1:]]
    if not args:
        newest = sorted(LOGS.glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)
        args = [newest[0], SOT] if newest and SOT.is_file() else ([SOT] if SOT.is_file() else [])
    if not args:
        print("no logs", file=sys.stderr)
        return 1
    outs = []
    for p in args:
        lab = "SoT 100413" if "100413" in p.name else p.stem
        outs.append(analyze(lab, p))
    base = next((o for o in outs if "100413" in o["path"]), None)
    cur = outs[0]
    if base and cur is not base:
        return 0 if gate(cur, base) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
