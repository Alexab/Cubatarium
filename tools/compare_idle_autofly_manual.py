#!/usr/bin/env python3
"""Compare idle autofly report vs manual calm-stand metrics.

Exit non-zero when idle-warm wall/prep/stream are <50% of manual (harness too clean).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

KEYS = (
    "calm_stop_wall_med",
    "calm_stop_emerge_med",
    "calm_stop_stream_med",
    "stop_mesh_prep_med",
    "dirty_med",
    "post_stop_focus_dirty_med",
    "opaque_cmd_on_med",
    "post_stop_black_sticky_max",
    "wall_ms_fly_med",
)


def load_metrics(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if "median" in data and isinstance(data["median"], dict):
        # Aggregate from --repeat
        return dict(data["median"])
    return dict(data.get("metrics") or {})


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--autofly", type=Path, required=True)
    ap.add_argument("--manual", type=Path, required=True)
    ap.add_argument(
        "--mode",
        choices=["idle-warm", "idle-clean"],
        default="idle-warm",
        help="idle-warm: red-flag if wall/prep/stream <50% manual; idle-clean: report only",
    )
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    if not args.autofly.is_file():
        print(f"FAIL: missing autofly {args.autofly}", file=sys.stderr)
        return 2
    if not args.manual.is_file():
        print(f"FAIL: missing manual {args.manual}", file=sys.stderr)
        return 2

    af = load_metrics(args.autofly)
    man = load_metrics(args.manual)

    rows = []
    for k in KEYS:
        a = af.get(k)
        m = man.get(k)
        ratio = None
        if a is not None and m is not None and float(m) != 0.0:
            ratio = float(a) / float(m)
        rows.append({"key": k, "autofly": a, "manual": m, "ratio": ratio})

    print(f"{'metric':28} {'autofly':>12} {'manual':>12} {'ratio':>8}")
    for r in rows:
        a = r["autofly"]
        m = r["manual"]
        ratio = r["ratio"]
        print(
            f"{r['key']:28} "
            f"{('n/a' if a is None else f'{a:.3f}'):>12} "
            f"{('n/a' if m is None else f'{m:.3f}'):>12} "
            f"{('n/a' if ratio is None else f'{ratio:.2f}'):>8}"
        )

    red = []
    if args.mode == "idle-warm":
        for key in ("calm_stop_wall_med", "stop_mesh_prep_med", "calm_stop_stream_med"):
            a = af.get(key)
            m = man.get(key)
            if a is None or m is None or float(m) <= 0:
                continue
            if float(a) < 0.5 * float(m):
                red.append(key)

    out = {
        "autofly": str(args.autofly),
        "manual": str(args.manual),
        "mode": args.mode,
        "rows": rows,
        "red_flags": red,
        "pass": len(red) == 0,
    }
    if args.out:
        args.out.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")

    if red:
        print(f"RED: autofly too clean vs manual on {red}", file=sys.stderr)
        return 1
    print("OK: harness pressure within 50% of manual (or report-only mode)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
