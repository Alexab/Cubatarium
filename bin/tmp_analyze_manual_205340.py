#!/usr/bin/env python3
import json
import statistics as st
from collections import Counter
from pathlib import Path

p = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260818-205340_27764.jsonl")
rows = []
for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
    line = line.strip()
    if not line:
        continue
    try:
        rows.append(json.loads(line))
    except json.JSONDecodeError:
        pass

periods = [r for r in rows if r.get("kind") == "period"]
spikes = [r for r in rows if r.get("kind") == "spike"]
print("file", p.name, "lines", len(rows), "periods", len(periods), "spikes", len(spikes))
if not periods:
    raise SystemExit("NO PERIODS")

r0, r1 = periods[0], periods[-1]
print(
    "focus start",
    (r0.get("focus_cx"), r0.get("focus_cz")),
    "end",
    (r1.get("focus_cx"), r1.get("focus_cz")),
)
print("player start", (r0.get("player_x"), r0.get("player_y"), r0.get("player_z")))
print("player end", (r1.get("player_x"), r1.get("player_y"), r1.get("player_z")))
print("chunks", r0.get("chunk_count"), "->", r1.get("chunk_count"))


def col(key):
    return [float(r.get(key) or 0) for r in periods]


on = col("opaque_cmd_on")
tot = col("opaque_cmd_total")
aabb = col("cpu_aabb_would_on")
c0 = col("chunk_meshed_culled0")
holes = col("unfinished_visual")
miss = [r.get("focus_missing_mesh") or 0 for r in periods]
print("opaque_on min/med/max", min(on), st.median(on), max(on))
print("opaque_total med", st.median(tot))
print("cpu_aabb med", st.median(aabb))
print(
    "culled0 max",
    max(c0),
    "frames culled0==total",
    sum(1 for i in range(len(on)) if tot[i] > 0 and c0[i] >= tot[i]),
)
print("blue suspect frames", sum(1 for v in on if v == 0))
print("holes med/max", st.median(holes), max(holes))
print("miss_end", miss[-1], "miss periods", sum(1 for m in miss if m))
print("underfeet_reason top", Counter(r.get("underfeet_reason") for r in periods).most_common(6))
walls = col("wall_ms")
print("wall p50/p90", st.median(walls), sorted(walls)[max(0, int(0.9 * len(walls)) - 1)])
print("void_near max", max(col("dark_face_void_near_n")))
print("visible_black max", max(col("visible_black_focus_n")))
print("gpu_pool_used max MB", max(col("gpu_pool_used_mb")))
print("mesh_discarded_late last", periods[-1].get("mesh_discarded_late"))
print("pending_gpu last", periods[-1].get("pending_gpu_applies_n"))
print("stream_ring_blocked max", max(r.get("stream_ring_blocked") or 0 for r in periods))
print("column_absent max", max(r.get("column_absent_in_rd_n") or 0 for r in periods))
print("column_loaded_no_mesh max", max(r.get("column_loaded_no_mesh_n") or 0 for r in periods))
print("not_ready max", max(r.get("chunk_not_ready") or 0 for r in periods))
print("post_load_ring max", max(r.get("post_load_ring_not_ready") or 0 for r in periods))
print("frontier_pressure max", max(r.get("frontier_pressure") or 0 for r in periods))
print("fog_hole_debt max", max(r.get("fog_hole_debt") or 0 for r in periods))

print("\n--- trajectory segments ---")
step = max(1, len(periods) // 10)
for i in range(0, len(periods), step):
    r = periods[i]
    print(
        f"p{i:02d} cx={r.get('focus_cx')} cz={r.get('focus_cz')} "
        f"y={r.get('player_y'):.0f} on={r.get('opaque_cmd_on')} "
        f"cul0={r.get('chunk_meshed_culled0')} holes={r.get('unfinished_visual')} "
        f"miss={r.get('focus_missing_mesh')} void={r.get('dark_face_void_near_n')} "
        f"blk={r.get('visible_black_focus_n')} chunks={r.get('chunk_count')} "
        f"pool={r.get('gpu_pool_used_mb'):.1f}"
    )

print("\n--- anomalies (opaque==0 or cul0>=100 or on/tot<0.3) ---")
for i, r in enumerate(periods):
    o = r.get("opaque_cmd_on") or 0
    t = r.get("opaque_cmd_total") or 0
    c = r.get("chunk_meshed_culled0") or 0
    if o == 0 or c >= 100 or (t > 0 and o / t < 0.3):
        print(
            f"p{i:02d} cx={r.get('focus_cx')} cz={r.get('focus_cz')} "
            f"on={o}/{t} cul0={c} aabb={r.get('cpu_aabb_would_on')} "
            f"holes={r.get('unfinished_visual')} miss={r.get('focus_missing_mesh')} "
            f"void={r.get('dark_face_void_near_n')} y={r.get('player_y')}"
        )

# last 15 periods detail
print("\n--- last 15 periods ---")
for i, r in enumerate(periods[-15:], start=len(periods) - 15):
    print(
        f"p{i:02d} cx={r.get('focus_cx')} cz={r.get('focus_cz')} "
        f"on={r.get('opaque_cmd_on')}/{r.get('opaque_cmd_total')} "
        f"holes={r.get('unfinished_visual')} miss={r.get('focus_missing_mesh')} "
        f"void={r.get('dark_face_void_near_n')} nr={r.get('chunk_not_ready')} "
        f"absent={r.get('column_absent_in_rd_n')} no_mesh={r.get('column_loaded_no_mesh_n')}"
    )
