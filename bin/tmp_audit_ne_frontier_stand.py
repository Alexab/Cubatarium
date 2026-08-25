#!/usr/bin/env python3
"""SoftDefer standstill gates for fz-ne-frontier-stand / manual 205739 vs 170807.

Usage:
  python bin/tmp_audit_ne_frontier_stand.py [perf.jsonl ...]
  python bin/tmp_audit_ne_frontier_stand.py   # defaults: latest + 205739 + 170807
"""
from __future__ import annotations

import json
import statistics as st
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "bin" / "logs"


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


def steady(u):
    return u[60:] if len(u) > 60 else u[len(u) // 2 :]


def g(rows, key):
    return [r.get(key) for r in rows]


def rate(rows, key):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)
    return d / max(1, len(rows) - 1)


def standstill_window(rows):
    """Prefer keep>=160 contiguous stand; else last no-move stretch; else late third."""
    if len(rows) < 4:
        return rows
    land = [r for r in rows if int(r.get("keep_cols") or 0) >= 160]
    if len(land) >= 15:
        # longest trailing standstill inside land
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
    best_i = len(rows) - 1
    for i in range(len(rows) - 2, -1, -1):
        if (rows[i].get("player_x"), rows[i].get("player_z")) != (
            rows[i + 1].get("player_x"),
            rows[i + 1].get("player_z"),
        ):
            best_i = i + 1
            break
        best_i = i
    stand = rows[best_i:]
    if len(stand) >= 20:
        return stand
    return rows[-(max(1, len(rows) // 3)) :]


def summ(label: str, path: Path) -> dict:
    u = load(path)
    s = steady(u)
    stand = standstill_window(s)
    late = stand[-60:] if len(stand) >= 60 else stand
    print(f"\n=== {label} spikes={len(u)} steady={len(s)} stand={len(stand)} ===")
    if not s:
        print("  EMPTY")
        return {"label": label, "path": str(path), "pass": False}

    px0, pz0 = s[0].get("player_x"), s[0].get("player_z")
    px1, pz1 = s[-1].get("player_x"), s[-1].get("player_z")
    print(f"  steady xz ({px0},{pz0}) -> ({px1},{pz1})")
    sx0, sz0 = stand[0].get("player_x"), stand[0].get("player_z")
    print(f"  stand  xz ({sx0},{sz0}) n={len(stand)}")

    floor_r = rate(stand, "softdefer_capture_floor_hits")
    wr_r = rate(stand, "softdefer_witness_retarget")
    stuck = med(g(stand, "softdefer_empty_stuck_n"))
    stale = med(g(late, "dark_face_stale_near_n"))
    capture = med(g(late, "capture_bg_cap_n"))
    keep = med(g(late, "keep_cols"))
    pl_drop = rate(stand, "pending_light_dropped") or 0.0
    unf = med(g(late, "unfinished_visual"))
    vbnt = med(g(late, "visible_black_no_ticket_n"))

    print(f"  floor/spike={floor_r:.2f}" if floor_r is not None else "  floor/spike=N/A")
    print(f"  witness/spike={wr_r:.2f}" if wr_r is not None else "  witness/spike=N/A")
    print(f"  stuck_med={stuck} stale_late={stale} capture={capture} keep={keep}")
    print(f"  pl_drop/spike={pl_drop:.3f} unfinished={unf} vb_no_ticket={vbnt}")

    gates = [
        ("floor/spike <=8", floor_r, 8.0, "<="),
        ("witness/spike <=5", wr_r, 5.0, "<="),
        ("stuck med <=5", stuck, 5.0, "<="),
        ("stale late <=20", stale, 20.0, "<="),
        ("capture >=3 KEEP", capture, 3.0, ">="),
        ("PL drop/spike ~0 KEEP", pl_drop, 0.5, "<="),
        ("keep >=160 KEEP", keep, 160.0, ">="),
    ]
    ok = True
    print("  SoftDefer standstill gates:")
    results = {}
    for name, val, tgt, op in gates:
        if val is None:
            stt = "MISSING"
            ok = False
        elif op == "<=":
            stt = "PASS" if val <= tgt else "FAIL"
            ok = ok and stt == "PASS"
        else:
            stt = "PASS" if val >= tgt else "FAIL"
            ok = ok and stt == "PASS"
        print(f"    {stt:7s} {name:28s} got={val} {op}{tgt}")
        results[name] = {"status": stt, "got": val, "tgt": tgt}
    print(f"  OVERALL {'PASS' if ok else 'FAIL'}")
    return {
        "label": label,
        "path": str(path),
        "pass": ok,
        "floor_per_spike": floor_r,
        "witness_per_spike": wr_r,
        "stuck_med": stuck,
        "stale_late": stale,
        "gates": results,
    }


def newest_perf() -> Path | None:
    files = sorted(LOGS.glob("perf_*.jsonl"), key=lambda p: p.stat().st_mtime, reverse=True)
    return files[0] if files else None


def main():
    paths: list[tuple[str, Path]] = []
    if len(sys.argv) > 1:
        for a in sys.argv[1:]:
            paths.append((Path(a).stem, Path(a)))
    else:
        newest = newest_perf()
        if newest:
            paths.append(("NEWEST", newest))
        for lab, name in [
            ("P14 FAIL 205739", "perf_20260825-205739_3064.jsonl"),
            ("P13 170807", "perf_20260825-170807_29680.jsonl"),
        ]:
            p = LOGS / name
            if p.is_file():
                paths.append((lab, p))
    if not paths:
        print("no perf logs", file=sys.stderr)
        return 1
    outs = [summ(lab, p) for lab, p in paths]
    # A/B: if first is post-revert and 205739 present, compare floor
    fail = next((o for o in outs if "205739" in o["label"] or "P14" in o["label"]), None)
    good = next((o for o in outs if "170807" in o["label"] or "P13" in o["label"]), None)
    cur = outs[0]
    if fail and cur is not fail and cur.get("floor_per_spike") is not None and fail.get("floor_per_spike"):
        print("\n=== A/B vs P14 FAIL 205739 ===")
        print(
            f"  floor/spike cur={cur['floor_per_spike']:.2f} "
            f"p14={fail['floor_per_spike']:.2f} "
            f"{'BETTER' if cur['floor_per_spike'] < fail['floor_per_spike'] * 0.75 else 'NOT_CLEAR'}"
        )
        if good and good.get("floor_per_spike") is not None:
            print(
                f"  floor/spike p13={good['floor_per_spike']:.2f} "
                f"(target band)"
            )
    return 0 if all(o.get("pass") for o in outs if o["label"] == "NEWEST" or "post" in o["label"].lower()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
