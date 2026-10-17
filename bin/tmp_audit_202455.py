import json
import statistics
from collections import Counter

n = json.load(open("bin/suite_reports/manual_20260903-202455_analyze.json"))
b = json.load(open("bin/suite_reports/manual_20260903-180351_analyze.json"))
o = json.load(open("bin/suite_reports/manual_20260903-085143_analyze.json"))
nm, bm, om = n["metrics"], b["metrics"], o["metrics"]

keys = [
    "wall_ms_fly_med",
    "effective_fps_fly",
    "stream_ms",
    "mesh_emerge_ms",
    "prep_refresh_pressure_ms",
    "wall_stream_share",
    "wall_render_share",
    "holes_rate",
    "witness_latch_diet_share",
    "visual_holes_telemetry_mismatch_rate",
    "fm_dirty_to_gpu_finish_med",
    "miss_stuck_max_run_sec",
    "cruise_schedule_ok_med",
    "cruise_schedule_ok_when_positive_med",
    "schedule_ok_zero_rate",
    "dirty_med",
    "chunk_not_ready_med",
    "chunks_traveled",
    "cruise_dirty_fm_med",
    "cruise_fm_enqueue_med",
    "dominant_schedule_blocker",
    "mesh_waterfall_finish_med",
    "input_ms_fly_med",
]
print("=== COMPARE ===")
print(f"{'metric':42} {'085143':>14} {'180351':>14} {'202455':>14}")
for k in keys:
    print(f"{k:42} {str(om.get(k, 'N/A')):>14} {str(bm.get(k, 'N/A')):>14} {str(nm.get(k, 'N/A')):>14}")

soft = n.get("soft", {})
print("\n=== SOFT ===")
for k in [
    "stream_prep_share",
    "stream_emerge_share",
    "emerge_prep_other_share",
    "heal_on_hot_sec",
    "chain_stall_sec",
    "dominant_spike_class",
]:
    print(f"{k}: {soft.get(k)}")

lines = [json.loads(l) for l in open("bin/logs/perf_20260903-202455_21316.jsonl")]
total = len(lines)
fly = [d for d in lines if d.get("miss_horiz", 0) >= 3]
print(f"\n=== RAW TELEMETRY frames={total} fly_mh3={len(fly)} ===")


def nz_rate(rows, key):
    if not rows:
        return 0.0
    return 100.0 * sum(1 for r in rows if (r.get(key) or 0) > 0) / len(rows)


for key in [
    "rim_hole_pressure",
    "rim_perf_diet",
    "ingress_debt_level",
    "force_full_unfinished",
    "stream_phase_over_budget",
    "phase_budget_over",
]:
    print(f"{key:30} total_nz={nz_rate(lines, key):5.1f}%  fly_nz={nz_rate(fly, key):5.1f}%")

debt = Counter(int(d.get("ingress_debt_level") or 0) for d in fly)
print("ingress_debt_level fly dist:", dict(sorted(debt.items())))

# wall/stream on fly
if fly:
    for label, key in [("wall", "wall_ms"), ("stream", "stream_ms"), ("emerge", "mesh_emerge_ms")]:
        vals = [float(d.get(key) or 0) for d in fly]
        print(
            f"{label:8} med={statistics.median(vals):7.1f} mean={statistics.mean(vals):7.1f} p95={sorted(vals)[int(0.95*(len(vals)-1))]:7.1f}"
        )

# late flight: last third of fly frames — holes / miss
third = max(1, len(fly) // 3)
early, mid, late = fly[:third], fly[third : 2 * third], fly[2 * third :]


def holeish(rows):
    if not rows:
        return {}
    return {
        "n": len(rows),
        "focus_missing>0": sum(1 for r in rows if (r.get("focus_missing_mesh") or 0) > 0) / len(rows),
        "unfinished_vis>0": sum(1 for r in rows if (r.get("unfinished_visual") or 0) > 0) / len(rows),
        "miss_end_med": statistics.median([float(r.get("miss_end") or 0) for r in rows]),
        "focus_missing_med": statistics.median([float(r.get("focus_missing_mesh") or 0) for r in rows]),
        "dirty_fm_med": statistics.median([float(r.get("dirty_fm_n") or 0) for r in rows]),
        "sched_ok_med": statistics.median([float(r.get("mesh_dirty_schedule_ok_n") or 0) for r in rows]),
        "wall_med": statistics.median([float(r.get("wall_ms") or 0) for r in rows]),
        "stream_med": statistics.median([float(r.get("stream_ms") or 0) for r in rows]),
        "fm_finish_nz": sum(1 for r in rows if (r.get("fm_dirty_to_gpu_finish_n") or 0) > 0) / len(rows),
        "gpu_kick_nz": sum(1 for r in rows if (r.get("pending_gpu_kicked_n") or 0) > 0) / len(rows),
        "skip_softdefer_med": statistics.median(
            [float(r.get("mesh_dirty_schedule_skip_softdefer_n") or 0) for r in rows]
        ),
        "skip_remesh_med": statistics.median(
            [float(r.get("mesh_dirty_schedule_skip_remesh_starve_n") or 0) for r in rows]
        ),
        "blocker_sample": Counter(),
    }


print("\n=== EARLY/MID/LATE FLY ===")
for name, rows in [("early", early), ("mid", mid), ("late", late)]:
    h = holeish(rows)
    print(
        f"{name}: n={h['n']} wall={h['wall_med']:.0f} stream={h['stream_med']:.0f} "
        f"focus_miss_rate={h['focus_missing>0']:.0%} focus_miss_med={h['focus_missing_med']} "
        f"uv_rate={h['unfinished_vis>0']:.0%} dirty_fm={h['dirty_fm_med']} sched_ok={h['sched_ok_med']} "
        f"fm_finish_nz={h['fm_finish_nz']:.0%} gpu_kick_nz={h['gpu_kick_nz']:.0%} "
        f"skip_softdefer={h['skip_softdefer_med']} skip_remesh={h['skip_remesh_med']}"
    )

# schedule blocker on schedule_ok==0 fly frames
blockers = Counter()
for r in fly:
    if float(r.get("mesh_dirty_schedule_ok_n") or 0) != 0:
        continue
    dirty = float(r.get("dirty_fm_n") or 0)
    cap = float(r.get("first_mesh_schedule_cap") or 0)
    if dirty <= 0:
        blockers["empty_fm"] += 1
    elif cap <= 0:
        blockers["zero_cap"] += 1
    else:
        skips = {
            "softdefer": float(r.get("mesh_dirty_schedule_skip_softdefer_n") or 0),
            "remesh": float(r.get("mesh_dirty_schedule_skip_remesh_starve_n") or 0),
            "orphan": float(r.get("mesh_dirty_schedule_skip_orphan_n") or 0),
            "pipeline": float(r.get("mesh_dirty_schedule_skip_pipeline_n") or 0),
            "locked": float(r.get("mesh_dirty_schedule_skip_locked_n") or 0),
            "snapshot": float(r.get("mesh_dirty_schedule_skip_snapshot_n") or 0),
            "outside": float(r.get("mesh_dirty_schedule_skip_outside_focus_n") or 0),
            "other": float(r.get("mesh_dirty_schedule_skip_other_n") or 0),
        }
        top = max(skips, key=skips.get)
        if skips[top] > 0:
            blockers[f"skip_{top}"] += 1
        else:
            blockers["unknown_no_skip"] += 1
print("\nok=0 fly blockers:", dict(blockers))
