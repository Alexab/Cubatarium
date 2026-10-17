#!/usr/bin/env python3
"""Audit inland cruise 215411 (P0-P4) vs 195810 SoT rollback."""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

LOGS = Path(r"E:/Work/Home/Cubatarium/bin/logs")
FILES = [
    ("215411 P0-P4", "perf_20260817-215411_29916.jsonl"),
    ("195810 SoT rollback", "perf_20260817-195810_30216.jsonl"),
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


def summarize(label, name):
    rows = load(name)
    cruise = [r for r in rows if (r.get("chunk_count") or 0) >= 80]
    print(f"\n===== {label} raw={len(rows)} cruise={len(cruise)} =====")
    if not cruise:
        return cruise
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
        "relight_capture_col_horiz",
        "relight_capture_finalize",
        "relight_capture_band_cy_span",
        "relight_witness_hold_n",
        "relight_apply_n",
        "visible_black_focus_n",
        "opaque_draw_n",
        "opaque_gpu_packed_n",
        "gpu_pool_used_mb",
        "softdefer_witness_retarget",
        "softdefer_capture_floor_hits",
        "softdefer_empty_placeholder_n",
        "pending_gpu_queued_n",
        "mesh_dirty_gpu_n",
        "gpu_kick_n",
        "gpu_finish_n",
        "relight_completed_discarded",
        "heal_deferred_for_miss",
    ]
    for k in keys:
        vals = num(cruise, k)
        if not vals:
            print(f"  {k}: MISSING")
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
        print("  miss_horiz", Counter(r.get("miss_horiz") for r in miss).most_common(8))
        print("  miss_cy", Counter(r.get("miss_cy") for r in miss).most_common(8))
        print(
            "  miss_cx/cz",
            Counter((r.get("miss_cx"), r.get("miss_cz")) for r in miss).most_common(10),
        )
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
        print(
            f"  late pool med={pct(num(late,'gpu_pool_used_mb'),0.5)} "
            f"packed med={pct(num(late,'opaque_gpu_packed_n'),0.5)}"
        )
    n = len(cruise)
    for tag, r in [("start", cruise[0]), ("mid", cruise[n // 2]), ("end", cruise[-1])]:
        print(
            f"  {tag}: cz={r.get('focus_cz')} opaque={r.get('opaque_draw_n')} "
            f"packed={r.get('opaque_gpu_packed_n')} pool={r.get('gpu_pool_used_mb')} "
            f"pl={r.get('pending_light_n')} fifo={r.get('relight_fifo_n')} "
            f"done={r.get('relight_completed_n')} apply={r.get('relight_apply_n')} "
            f"cap_h={r.get('relight_capture_col_horiz')} "
            f"fin={r.get('relight_capture_finalize')} "
            f"span={r.get('relight_capture_band_cy_span')} "
            f"hold={r.get('relight_witness_hold_n')} "
            f"drop={r.get('relight_fifo_dropped')} "
            f"fm={r.get('dirty_fm_n')} skip={r.get('mesh_dirty_schedule_skip_n')} "
            f"sch={r.get('mesh_dirty_schedule_ok_n')} miss={r.get('focus_missing_mesh')} "
            f"nh={r.get('miss_horiz')} gpuq={r.get('pending_gpu_queued_n')} "
            f"wit={r.get('softdefer_witness_retarget')} floor={r.get('softdefer_capture_floor_hits')}"
        )
    done = num(cruise, "relight_completed_n")
    if done:
        z = sum(1 for v in done if v == 0)
        print(f"  relight_completed_n==0 frames: {z}/{len(done)}")
    apply = num(cruise, "relight_apply_n")
    if apply:
        z = sum(1 for v in apply if v == 0)
        print(f"  relight_apply_n==0 frames: {z}/{len(apply)} sum={sum(apply)}")
    fin = num(cruise, "relight_capture_finalize")
    if fin:
        print(f"  capture_finalize=1 frames: {sum(1 for v in fin if v)}/{len(fin)}")
    cap_h = [r.get("relight_capture_col_horiz") for r in cruise]
    print("  capture_col_horiz", Counter(cap_h).most_common(8))
    drain_miss = num(miss, "relight_drain_ms") if miss else []
    if drain_miss:
        print(
            f"  relight_drain_ms on miss: med={pct(drain_miss,0.5)} "
            f"p90={pct(drain_miss,0.9)} min={min(drain_miss)} max={max(drain_miss)}"
        )
    cool = [r for r in cruise if (r.get("pending_light_n") or 0) <= 2]
    print(f"  frames pending_light<=2: {len(cool)}")
    if cool:
        cool_miss = [r for r in cool if r.get("focus_missing_mesh")]
        print(
            f"    miss among them: {len(cool_miss)}"
            f" dirty_fm med={pct(num(cool,'dirty_fm_n'),0.5)}"
            f" nh={Counter(r.get('miss_horiz') for r in cool_miss).most_common(6)}"
        )
    spikes = [r for r in cruise if (r.get("relight_drain_ms") or 0) > 200]
    print(f"  drain>200ms frames: {len(spikes)} drain>600: {sum(1 for r in cruise if (r.get('relight_drain_ms') or 0) > 600)}")
    return cruise


def cz_buckets(label, name):
    cruise = [r for r in load(name) if (r.get("chunk_count") or 0) >= 80]
    print(f"\n===== {label} per-cz =====")
    for cz in range(50, 56):
        b = [r for r in cruise if r.get("focus_cz") == cz]
        if not b:
            continue
        miss_b = [r for r in b if r.get("focus_missing_mesh")]
        o = num(b, "opaque_draw_n")
        pl = num(b, "pending_light_n")
        fm = num(b, "dirty_fm_n")
        pack = num(b, "opaque_gpu_packed_n")
        pool = num(b, "gpu_pool_used_mb")
        fifo = num(b, "relight_fifo_n")
        done = num(b, "relight_completed_n")
        apply = num(b, "relight_apply_n")
        skip = num(b, "mesh_dirty_schedule_skip_n")
        drain = num(b, "relight_drain_ms")
        hold = num(b, "relight_witness_hold_n")
        fin = num(b, "relight_capture_finalize")
        uf7 = sum(1 for r in b if r.get("underfeet_reason") == 7)
        print(
            f"cz={cz} n={len(b)} miss={len(miss_b)} uf7={uf7} "
            f"opaque med={pct(o,0.5)} packed med={pct(pack,0.5)} "
            f"pool med={round(pct(pool,0.5),2) if pool else None} "
            f"pl med={pct(pl,0.5)} fifo med={pct(fifo,0.5)} "
            f"done med={pct(done,0.5)} apply med={pct(apply,0.5)} "
            f"fm med={pct(fm,0.5)} skip med={pct(skip,0.5)} "
            f"drain med={pct(drain,0.5)} hold med={pct(hold,0.5)} "
            f"fin1={sum(1 for v in fin if v)}/{len(fin) if fin else 0} "
            f"horiz={Counter(r.get('miss_horiz') for r in miss_b).most_common(4)} "
            f"cap_h={Counter(r.get('relight_capture_col_horiz') for r in b).most_common(4)}"
        )


def main():
    for label, name in FILES:
        summarize(label, name)
    cz_buckets("215411", FILES[0][1])
    cz_buckets("195810", FILES[1][1])


if __name__ == "__main__":
    main()
