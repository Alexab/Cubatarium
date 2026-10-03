import json
import glob
import os

for p in sorted(glob.glob("bin/suite_reports/g1_a10_relight/wall_diet_a4_*.json")):
    d = json.load(open(p, encoding="utf-8"))
    pa = d.get("proxy_adequacy", {})
    m = d["metrics"]
    print("===", os.path.basename(p), "===")
    print(
        "adequacy",
        pa.get("adequacy_pass"),
        "VB",
        pa.get("visible_black_focus_fly_med"),
        "stale",
        m.get("dark_face_stale_near_n"),
        "unlit",
        m.get("chunk_meshed_unlit_med"),
    )
    print(
        "wall_fly",
        m.get("wall_ms_fly_med"),
        "emerge",
        m.get("mesh_emerge_ms"),
        "prep_share",
        m.get("wall_prep_share"),
    )
    print("perf", d.get("perf_jsonl"))

logs = sorted(glob.glob("bin/logs/perf_*.jsonl"), key=os.path.getmtime)[-3:]
keys = [
    "prep_schedule_policy_ms",
    "prep_spawn_ring_query_ms",
    "prep_drop_remesh_ms",
    "prep_cancel_async_ms",
    "prep_sched_other_ms",
    "prep_hole_force_ms",
    "prep_heavy_walk_n",
    "streamer_update_ms",
    "async_io_ms",
]
for lp in logs:
    print("---", lp)
    vals = {k: [] for k in keys}
    for line in open(lp, encoding="utf-8", errors="ignore"):
        if not line.startswith("{"):
            continue
        try:
            o = json.loads(line)
        except Exception:
            continue
        if "prep_schedule_policy_ms" not in o and "streamer_update_ms" not in o:
            continue
        for k in keys:
            if k in o and isinstance(o[k], (int, float)):
                vals[k].append(float(o[k]))
    for k, v in vals.items():
        if not v:
            print(k, "MISSING")
            continue
        v = sorted(v)
        mid = v[len(v) // 2]
        print(f"{k}: n={len(v)} med={mid:.4f} max={max(v):.4f}")
