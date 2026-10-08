import json
from pathlib import Path

def bucket(row):
    cands={
        "fluid_map": float(row.get("fluid_map_cpu_ms") or 0),
        "world_extra": float(row.get("world_extra_ms") or 0),
        "stream": float(row.get("stream_ms") or 0),
        "relight": float(row.get("relight_drain_ms") or 0),
        "emerge": float(row.get("mesh_emerge_ms") or 0),
        "tick_env": float(row.get("tick_env_ms") or 0),
        "block_input": float(row.get("block_input_ms") or 0),
        "app_update": float(row.get("app_update_ms") or 0),
        "render": float(row.get("render_total_ms") or 0),
        "prepare": float(row.get("prepare_frame_ms") or 0),
    }
    best=max(cands, key=cands.get)
    wall=float(row.get("wall_ms") or 0)
    return best, cands[best], wall, (cands[best]/wall if wall else 0)

p=Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260821-220205_23988.jsonl")
rows=[json.loads(l) for l in p.read_text(encoding="utf-8").splitlines() if l.strip().startswith("{")]
spikes=[r for r in rows if r.get("kind")=="spike"]
periods=[r for r in rows if r.get("kind")=="period"]
spikes_sorted=sorted(spikes, key=lambda r: -float(r.get("wall_ms") or 0))
print("TOP 5 HEAVIEST SPIKES primary:")
for r in spikes_sorted[:5]:
    b,v,w,f=bucket(r)
    print("  wall=%.0f raw_best=%s:%.0f(%.0f%%) stream=%.0f emerge=%.0f fluid=%.0f apply=%.0f app_upd=%.0f wstream=%.0f render=%.0f dirty=%s PL=%s VB=%s" % (
        w,b,v,f*100,float(r.get("stream_ms") or 0),float(r.get("mesh_emerge_ms") or 0),float(r.get("fluid_map_cpu_ms") or 0),float(r.get("relight_apply_ms") or 0),float(r.get("app_update_ms") or 0),float(r.get("world_streaming_phase_ms") or 0),float(r.get("render_total_ms") or 0),r.get("dirty_n"),r.get("pending_light_focus"),r.get("visible_black_focus_n")))
print("SOFTEDEFER KEYS on first spike:")
s=spikes[0]
for k in sorted(s):
    if "softdefer" in k.lower():
        print(" ",k,"=",s[k])
uf=[int(r.get("underfeet_opaque_present") or 0) for r in periods]
print("period uf opaque:", uf)
