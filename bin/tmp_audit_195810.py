#!/usr/bin/env python3
"""One-shot audit of inland cruise 195810 vs 183918 / 163559."""
from __future__ import annotations

import json
import statistics as st
from collections import Counter
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")
FILES = [
    ("195810 SoT rollback", "perf_20260817-195810_30216.jsonl"),
    ("183918 hole-fill", "perf_20260817-183918_13392.jsonl"),
    ("163559 pre hole-fill", "perf_20260817-163559_2464.jsonl"),
]


def load(name):
    p = LOGS / name
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return rows


def pct(vals, p):
    if not vals:
        return None
    vals = sorted(vals)
    i = min(len(vals) - 1, int(round((len(vals) - 1) * p)))
    return vals[i]


def num(rows, key):
    return [r[key] for r in rows if isinstance(r.get(key), (int, float))]


def sample_keys(rows):
    keys = set()
    for r in rows[:5] + rows[-3:]:
        keys.update(r.keys())
    return sorted(keys)


def summarize(label, name):
    rows = load(name)
    cruise = [r for r in rows if (r.get("chunk_count") or 0) >= 80]
    print(f"\n===== {label} raw={len(rows)} cruise={len(cruise)} =====")
    if not cruise:
        return
    r0, r1 = cruise[0], cruise[-1]
    print(
        f"  focus ({r0.get('focus_cx')},{r0.get('focus_cz')}) -> "
        f"({r1.get('focus_cx')},{r1.get('focus_cz')}) "
        f"chunks {r0.get('chunk_count')}->{r1.get('chunk_count')}"
    )
    keys = [
        "wall_ms",
        "dirty_fm_n",
        "dirty_remesh_n",
        "mesh_dirty_schedule_ok_n",
        "mesh_dirty_schedule_skip_n",
        "pending_light_n",
        "relight_fifo_n",
        "relight_completed_n",
        "relight_fifo_dropped",
        "relight_drain_ms",
        "visible_black_focus_n",
        "opaque_draw_n",
        "opaque_gpu_packed_n",
        "gpu_pool_used_mb",
        "softdefer_witness_retarget",
        "softdefer_capture_floor_hits",
        "softdefer_empty_placeholder_n",
        "mesh_async_n",
        "pending_gpu_queued_n",
        "mesh_dirty_gpu_n",
        "gpu_kick_n",
        "gpu_finish_n",
        "focus_missing_horiz",
        "focus_missing_cy",
        "stage_skip_remesh_pending_light",
    ]
    for k in keys:
        vals = num(cruise, k)
        if not vals:
            continue
        print(
            f"  {k}: med={pct(vals,0.5)} p90={pct(vals,0.9)} "
            f"min={min(vals)} max={max(vals)}"
        )
    miss_n = sum(1 for r in cruise if r.get("focus_missing_mesh"))
    print(f"  miss_frame%={100.0 * miss_n / len(cruise):.1f} n={miss_n}/{len(cruise)}")
    miss = [r for r in cruise if r.get("focus_missing_mesh")]
    if miss:
        pl_miss = sum(1 for r in miss if (r.get("pending_light_n") or 0) > 0)
        print(f"  miss with pending_light>0: {pl_miss}/{len(miss)}")
        print("  miss_horiz", Counter(r.get("focus_missing_horiz") for r in miss).most_common(8))
        print("  miss_cy", Counter(r.get("focus_missing_cy") for r in miss).most_common(8))
    print("  underfeet_reason", Counter(r.get("underfeet_reason") for r in cruise).most_common(8))
    uf0 = sum(1 for r in cruise if not r.get("underfeet_has_mesh"))
    print(f"  underfeet_missing%={100.0 * uf0 / len(cruise):.1f}")
    late = [r for r in cruise if (r.get("focus_cz") or 0) >= 55]
    if late:
        o = num(late, "opaque_draw_n") or num(late, "opaque_cmd_on")
        print(
            f"  late cz>=55 n={len(late)} opaque med/min={pct(o,0.5)}/{min(o) if o else None}"
        )
        print("  late uf", Counter(r.get("underfeet_reason") for r in late).most_common(6))
    # time series: start / mid / end
    n = len(cruise)
    for tag, r in [("start", cruise[0]), ("mid", cruise[n // 2]), ("end", cruise[-1])]:
        print(
            f"  {tag}: cz={r.get('focus_cz')} opaque={r.get('opaque_draw_n')} "
            f"packed={r.get('opaque_gpu_packed_n')} pool={r.get('gpu_pool_used_mb')} "
            f"pl={r.get('pending_light_n')} fifo={r.get('relight_fifo_n')} "
            f"done={r.get('relight_completed_n')} drop={r.get('relight_fifo_dropped')} "
            f"fm={r.get('dirty_fm_n')} skip={r.get('mesh_dirty_schedule_skip_n')} "
            f"sch={r.get('mesh_dirty_schedule_ok_n')} miss={r.get('focus_missing_mesh')} "
            f"nh={r.get('focus_missing_horiz')} gpuq={r.get('pending_gpu_queued_n')} "
            f"wit={r.get('softdefer_witness_retarget')} floor={r.get('softdefer_capture_floor_hits')}"
        )
    if label.startswith("195810"):
        print("  --- extra keys present ---")
        interesting = [
            k
            for k in sample_keys(cruise)
            if any(
                s in k
                for s in (
                    "relight",
                    "pending_light",
                    "softdefer",
                    "miss",
                    "gpu",
                    "dirty",
                    "skip",
                    "underfeet",
                    "opaque",
                    "witness",
                )
            )
        ]
        print(" ", ", ".join(interesting[:80]))
        # completed_n zeros
        done = num(cruise, "relight_completed_n")
        if done:
            z = sum(1 for v in done if v == 0)
            print(f"  relight_completed_n==0 frames: {z}/{len(done)}")
        drain = num(cruise, "relight_drain_ms")
        if drain:
            print(
                f"  relight_drain_ms on miss: "
                f"med={pct(num(miss,'relight_drain_ms'),0.5)} "
                f"min={min(num(miss,'relight_drain_ms') or [0])}"
            )
        # pending cool-down windows
        cool = [r for r in cruise if (r.get("pending_light_n") or 0) <= 2]
        print(f"  frames pending_light<=2: {len(cool)}")
        if cool:
            print(
                f"    miss among them: {sum(1 for r in cool if r.get('focus_missing_mesh'))}"
                f" dirty_fm med={pct(num(cool,'dirty_fm_n'),0.5)}"
                f" nh={Counter(r.get('focus_missing_horiz') for r in cool if r.get('focus_missing_mesh')).most_common(6)}"
            )


def main():
    for label, name in FILES:
        summarize(label, name)


if __name__ == "__main__":
    main()
