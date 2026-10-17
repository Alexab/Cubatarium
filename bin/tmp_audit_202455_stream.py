import json
import statistics

lines = [json.loads(l) for l in open(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260903-202455_21316.jsonl")]
fly = [d for d in lines if int(d.get("miss_horiz") or 0) >= 3]
diet = [d for d in fly if int(d.get("rim_perf_diet") or 0) != 0]
near = [d for d in lines if int(d.get("miss_horiz") or 0) <= 2]


def report(name, rows):
    print(f"=== {name} n={len(rows)} ===")
    keys = [
        "stream_ms",
        "prep_refresh_pressure_ms",
        "prep_refresh_gap_ms",
        "streamer_update_ms",
        "idle_prefetch_ms",
        "async_io_ms",
        "relight_drain_ms",
        "commit_apply_ms",
        "commit_seal_ms",
        "commit_physics_ms",
        "softdefer_empty_scan_ms",
        "world_streaming_phase_ms",
        "mesh_emerge_ms",
        "mesh_emerge_prep_ms",
        "mesh_emerge_prep_other_ms",
        "mesh_emerge_prep_missing_ms",
        "mesh_emerge_prep_unfinished_ms",
        "mark_relit_total_ms",
        "prep_admission_ms",
        "prep_column_flow_drain_ms",
    ]
    for k in keys:
        vals = [float(d.get(k) or 0) for d in rows]
        if not vals or (statistics.mean(vals) == 0 and max(vals) == 0):
            continue
        print(
            f"{k:32} med={statistics.median(vals):8.2f} "
            f"mean={statistics.mean(vals):8.2f} "
            f"p95={sorted(vals)[int(0.95 * (len(vals) - 1))]:8.2f}"
        )
    rem = [
        float(d.get("stream_ms") or 0) - float(d.get("prep_refresh_pressure_ms") or 0)
        for d in rows
    ]
    print(f"{'stream_minus_prep':32} med={statistics.median(rem):8.2f}")


report("FLY_RIM", fly)
report("DIET", diet)
report("NEAR", near)
