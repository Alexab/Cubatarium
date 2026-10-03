import json
from pathlib import Path


def periods(path: str):
    rows = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        o = json.loads(line)
        if o.get("kind") == "period":
            rows.append(o)
    return rows


def med(xs):
    xs = sorted(xs)
    if not xs:
        return None
    mid = len(xs) // 2
    return xs[mid] if len(xs) % 2 else (xs[mid - 1] + xs[mid]) / 2


rows = periods("bin/logs/perf_20260914-154921_33492.jsonl")
cruise = [
    r
    for r in rows
    if r.get("focus_cx") is not None
    and float(r["focus_cx"]) <= 6
    and float(r["focus_cx"]) >= -2
]
print("cruise n", len(cruise), "all", len(rows))

keys = [
    "wall_ms",
    "stream_ms",
    "mesh_emerge_ms",
    "scene_ms",
    "phys_ms",
    "sim_ms",
    "swap_wait_ms",
    "mesh_waterfall_snapshot_med",
    "mesh_waterfall_schedule_med",
    "mesh_waterfall_drain_med",
    "mesh_waterfall_gpu_med",
    "mesh_waterfall_kick_med",
    "mesh_waterfall_finish_med",
    "mesh_snapshot_ms",
    "mesh_gpu_finish_ms",
    "pending_gpu",
    "pending_light_focus",
    "dirty_fm_n",
    "dirty_remesh_n",
    "chunk_meshed_unlit",
    "unfinished_visual",
    "visible_black_focus_n",
    "draw_oracle_stale_vertex_light_n",
    "mesh_dirty_schedule_ok_fm_n",
    "mesh_dirty_schedule_ok_remesh_n",
    "mesh_dirty_schedule_skip_snapshot_n",
    "first_mesh_schedule_cap",
    "remesh_schedule_cap",
    "schedule_lane_starve_reason",
]
for k in keys:
    vals = [float(r[k]) for r in cruise if r.get(k) is not None]
    if vals:
        print(f"{k:40s} med={med(vals):10.3f} max={max(vals):10.3f}")

print("\nTOP wall periods:")
ranked = sorted(rows, key=lambda r: float(r.get("wall_ms") or 0), reverse=True)[:5]
for r in ranked:
    print(
        {
            k: r.get(k)
            for k in [
                "wall_ms",
                "stream_ms",
                "mesh_emerge_ms",
                "scene_ms",
                "focus_cx",
                "player_y",
                "unfinished_visual",
                "visible_black_focus_n",
                "dirty_fm_n",
                "dirty_remesh_n",
                "chunk_meshed_unlit",
                "mesh_dirty_schedule_ok_fm_n",
                "mesh_dirty_schedule_ok_remesh_n",
                "mesh_snapshot_ms",
                "mesh_waterfall_schedule_med",
            ]
        }
    )

# Compare wall med across three flights on same mid-corridor
for label, path in [
    ("154921", "bin/logs/perf_20260914-154921_33492.jsonl"),
    ("122212", "bin/logs/perf_20260914-122212_41064.jsonl"),
    ("134914", "bin/logs/perf_20260914-134914_34224.jsonl"),
]:
    rs = periods(path)
    cr = [
        r
        for r in rs
        if r.get("focus_cx") is not None
        and float(r["focus_cx"]) <= 6
        and float(r["focus_cx"]) >= -2
    ]
    use = cr or rs
    w = [float(r["wall_ms"]) for r in use if r.get("wall_ms") is not None]
    em = [float(r["mesh_emerge_ms"]) for r in use if r.get("mesh_emerge_ms") is not None]
    st = [float(r["stream_ms"]) for r in use if r.get("stream_ms") is not None]
    un = [float(r["chunk_meshed_unlit"]) for r in use if r.get("chunk_meshed_unlit") is not None]
    uf = [float(r.get("unfinished_visual") or r.get("unfinished") or 0) for r in use]
    print(
        label,
        "wall_med",
        med(w),
        "emerge_med",
        med(em),
        "stream_med",
        med(st),
        "unlit_max",
        max(un) if un else None,
        "unf_med",
        med(uf),
    )

ep = Path("bin/logs/enter_lit_20260914-154948.jsonl")
elines = [json.loads(l) for l in ep.read_text(encoding="utf-8").splitlines() if l.strip()]
print("\nenter_lit n", len(elines))
if elines:
    keys = set()
    for e in elines:
        keys |= set(e.keys())
    interesting = [
        k
        for k in sorted(keys)
        if any(
            x in k.lower()
            for x in ["settle", "blocker", "void", "debt", "ready", "pass", "fail", "ms", "vb"]
        )
    ]
    last = elines[-1]
    print("last enter subset:")
    for k in interesting:
        if last.get(k) is not None:
            print(f"  {k}: {last.get(k)}")
