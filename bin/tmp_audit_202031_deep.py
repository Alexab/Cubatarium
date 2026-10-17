import json
from pathlib import Path
rows=[json.loads(l) for l in Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260920-202031_6988.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
def f(r,k): return float(r.get(k) or 0)
print("period emerge residual")
for i,r in enumerate(rows):
    em=f(r,"mesh_emerge_ms")
    if em<33: continue
    residual=em-f(r,"mesh_dirty_tick_ms")-f(r,"mesh_gpu_kick_ms")-f(r,"mesh_gpu_finish_ms")-f(r,"mesh_emerge_prep_ms")
    print("p%d: emerge=%.1f dirty=%.1f kick=%.1f fin=%.1f prep=%.1f resid=%.1f async_drain=%.1f phase=%.1f prep_column=%.1f prep_self=%.1f" % (
        i,em,f(r,"mesh_dirty_tick_ms"),f(r,"mesh_gpu_kick_ms"),f(r,"mesh_gpu_finish_ms"),f(r,"mesh_emerge_prep_ms"),residual,
        f(r,"mesh_async_drain_ms"),f(r,"world_streaming_phase_ms"),
        f(r,"mesh_emerge_prep_column_flow_drain_ms"),f(r,"mesh_emerge_prep_self_ms")))
print("--- accept/retain/stale ---")
for k in sorted(rows[0]):
    if any(s in k for s in ("accept","retain","stale","prefer","publication")):
        vals=[f(r,k) for r in rows]
        print("%s: med=%.2f max=%.2f sum=%.1f" % (k, sorted(vals)[len(vals)//2], max(vals), sum(vals)))
print("--- HOT mesh_*_ms ---")
for k in sorted(rows[0]):
    if k.startswith("mesh_") and k.endswith("_ms"):
        vals=[f(r,k) for r in rows]
        if max(vals)>5:
            s=sorted(vals)
            print("HOT %s: med=%.2f p95=%.2f max=%.2f" % (k, s[len(s)//2], s[int(0.95*(len(s)-1))], max(vals)))
print("--- render hot ---")
for k in sorted(rows[0]):
    if "render" in k or k in ("wall_ms","do_movement_ms","scene_ms","spikes"):
        vals=[f(r,k) for r in rows]
        if vals and max(vals)>1:
            s=sorted(vals)
            print("%s: med=%.2f p95=%.2f max=%.2f" % (k, s[len(s)//2], s[int(0.95*(len(s)-1))], max(vals)))
