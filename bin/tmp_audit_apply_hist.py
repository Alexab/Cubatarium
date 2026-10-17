#!/usr/bin/env python3
import json
import statistics
from collections import Counter
from pathlib import Path


def load(p):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return rows


for stamp, path in [
    ("133345", "bin/logs/perf_20260821-133345_9472.jsonl"),
    ("123613", "bin/logs/perf_20260821-123613_28584.jsonl"),
]:
    rows = load(path)
    spikes = [r for r in rows if r.get("kind") == "spike"]
    hi = [r for r in spikes if float(r.get("pending_light_focus") or 0) > 30]
    an = Counter(int(r.get("relight_apply_n") or 0) for r in hi)
    am = [float(r.get("relight_apply_ms") or 0) for r in hi]
    print(
        stamp,
        "spikes",
        len(spikes),
        "PL>30",
        len(hi),
        "apply_n hist",
        dict(sorted(an.items())),
    )
    print(
        "  apply_ms med/p90 among PL>30",
        statistics.median(am) if am else None,
        sorted(am)[int(0.9 * (len(am) - 1))] if am else None,
    )
    print(
        "  apply_n>=3 count",
        sum(1 for r in hi if int(r.get("relight_apply_n") or 0) >= 3),
    )
    print("  apply_n==0 fraction", an.get(0, 0) / max(1, len(hi)))
    print(
        "  capture_over sum",
        sum(float(r.get("capture_over_budget") or 0) for r in hi),
    )
    print(
        "  partial capture frames",
        sum(
            1
            for r in hi
            if float(r.get("relight_capture_ms") or 0) > 0.01
            and int(r.get("relight_capture_finalize") or 0) == 0
        ),
    )
    vbs = [float(r.get("visible_black_stalled_n") or 0) for r in spikes]
    print("  VB_stalled med/max", statistics.median(vbs), max(vbs) if vbs else None)
    print()

for stamp in ["100114", "121414", "131045", "131938"]:
    p = Path(f"bin/suite_reports/20260821-{stamp}_suite_summary.json")
    if not p.exists():
        continue
    d = json.load(open(p))
    print("SUITE", stamp)
    for s in d:
        m = s["metrics"]
        print(
            f"  {s['scenario']:14} PL={m.get('pending_light_focus_med')} "
            f"wall_fly={m.get('wall_ms_fly_med'):.0f} "
            f"BS_blink={m.get('black_sticky_blink_rate'):.3f} "
            f"gates={s.get('gates_pass')}"
        )
