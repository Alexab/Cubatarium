import json, statistics as st, re
from pathlib import Path
from collections import Counter

def load(p):
    rows=[]
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            try: rows.append(json.loads(line))
            except Exception: pass
    return rows

def periods(rows):
    return [r for r in rows if r.get("kind")=="period"]

def g(r,*ks,d=None):
    for k in ks:
        if k in r and r[k] is not None: return r[k]
    return d

def med(xs):
    xs=[float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None

def p95(xs):
    xs=sorted(float(x) for x in xs if x is not None)
    if not xs: return None
    return xs[min(len(xs)-1, int(round((len(xs)-1)*0.95)))]

def summarize(path):
    rows=load(path)
    per=periods(rows)
    cruise=[r for r in per if float(g(r,"movement_speed",d=0) or 0)>2]
    use=cruise if len(cruise)>=3 else per
    out={"file":Path(path).name,"n":len(per),"cruise_n":len(cruise)}
    if per:
        out["dur_s"]=(float(g(per[-1],"t_ms") or 0)-float(g(per[0],"t_ms") or 0))/1000.0
    keys=["wall_ms","scene_ms","world_streaming_phase_ms","stream_ms","mesh_emerge_ms","relight_drain_ms","pool_unsync_uploads","visual_holes","unfinished_visual","focus_missing_mesh","visible_black_focus_n","visible_black_stalled_n","empty_backlog_n","mesh_discarded_late","mesh_apply_stale","gpu_kick_n","pending_light_focus","column_loaded_no_mesh_n","column_meshing_n","column_render_ready_n","scene_opaque_cull_ms","scene_transparent_ms","gpu_cull_stats_readback","gpu_cull_submit_cpu_ms","softdefer_empty_age_max_frames","visibility_debt","dirty_dropped","buffer_expand_events","vertex_pool_fill"]
    for k in keys:
        xs=[g(r,k) for r in use]
        if any(x is not None for x in xs):
            out[k+"_med"]=med(xs)
            out[k+"_p95"]=p95(xs)
            out[k+"_max"]=max(float(x) for x in xs if x is not None)
            out[k+"_last"]=float(use[-1][k]) if k in use[-1] and use[-1][k] is not None else None
    out["holes_frac"]=sum(1 for r in use if int(g(r,"visual_holes",d=0) or 0)>0)/len(use) if use else None
    # enter first 30s by wall? periods may lack t_ms absolute; use index * approx
    # use first 15 periods as enter proxy if movement low initially
    enter=per[:max(1,min(20,len(per)//5))] if per else []
    # better: first periods until movement_speed>2 for 3 consecutive, else first 30 rows
    enter2=[]
    seen_cruise=0
    for r in per:
        spd=float(g(r,"movement_speed",d=0) or 0)
        if spd>2: seen_cruise+=1
        else: seen_cruise=0
        enter2.append(r)
        if seen_cruise>=3 and len(enter2)>5: break
        if len(enter2)>=40: break
    out["enter_n"]=len(enter2)
    out["enter_wall_med"]=med([g(r,"wall_ms") for r in enter2])
    out["enter_wall_p95"]=p95([g(r,"wall_ms") for r in enter2])
    out["enter_phase_med"]=med([g(r,"world_streaming_phase_ms") for r in enter2])
    out["enter_stream_med"]=med([g(r,"stream_ms") for r in enter2])
    out["enter_mesh_emerge_med"]=med([g(r,"mesh_emerge_ms") for r in enter2])
    out["enter_vb_med"]=med([g(r,"visible_black_focus_n") for r in enter2])
    out["enter_stale_max"]=max((float(g(r,"mesh_apply_stale",d=0) or 0) for r in enter2), default=None)
    out["enter_meshing_med"]=med([g(r,"column_meshing_n") for r in enter2])
    out["enter_rr_med"]=med([g(r,"column_render_ready_n") for r in enter2])
    out["enter_cull_readback_max"]=max((float(g(r,"gpu_cull_stats_readback",d=0) or 0) for r in enter2), default=None)
    return out

paths=[
"bin/logs/perf_20260910-174657_26996.jsonl",
"bin/logs/perf_20260910-141350_27632.jsonl",
"bin/logs/perf_20260910-093849_26976.jsonl",
]
for p in paths:
    s=summarize(p)
    print("===", s["file"], "===")
    for k,v in s.items():
        if k!="file":
            print(f"  {k}={v}")

# INFO signals
info=Path("bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260910-174654.26996").read_text(encoding="utf-8", errors="replace")
print("=== INFO counts ===")
for pat in ["settle_reason","empty_batch","soft_force","mesh_apply_stale","discarded_late","fence","Reserve","upload_full","Rejected","CaptureAndCommit","DependencyStamp","shadow mismatch"]:
    print(f"  {pat}={len(re.findall(pat, info, re.I))}")
settles=re.findall(r"settle_reason=\S+", info)
print("settles", Counter(settles).most_common(10))

el=Path("bin/logs/enter_lit_20260910-174709.jsonl")
rows=[json.loads(l) for l in el.read_text(encoding="utf-8", errors="replace").splitlines() if l.startswith("{")]
print("=== enter_lit === n", len(rows))
print("keys", sorted(rows[0].keys())[:50])
kinds=Counter(r.get("kind") or r.get("event") or "?" for r in rows)
print("kinds", kinds.most_common(12))
# duration
if "t_ms" in rows[0]:
    print("t span", rows[0]["t_ms"], "->", rows[-1]["t_ms"], "dur_s", (rows[-1]["t_ms"]-rows[0]["t_ms"])/1000)
elif "frame" in rows[0]:
    print("frames", rows[0].get("frame"), "->", rows[-1].get("frame"))
# print a few distinctive fields over time
for i in [0, len(rows)//4, len(rows)//2, -1]:
    r=rows[i]
    interesting={k:r[k] for k in r if k in ("kind","event","t_ms","frame","wall_ms","stream_ms","phase_ms","visual_holes","pending_light","render_ready","lit","settle","reason","vb","holes","ready")}
    print("row", i, interesting if interesting else list(r.items())[:12])
