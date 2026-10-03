import json, statistics as st
from collections import Counter
from pathlib import Path

def load(p):
    rows=[]
    for line in Path(p).read_text(encoding="utf-8").splitlines():
        line=line.strip()
        if not line: continue
        rows.append(json.loads(line))
    return rows

def med(vals):
    vals=[v for v in vals if v is not None]
    if not vals: return None
    vals=sorted(vals)
    return vals[len(vals)//2]

def summarize(name, rows, min_chunks=100):
    rows=[r for r in rows if (r.get("chunk_count") or 0)>min_chunks]
    if not rows:
        print(name, "no rows"); return
    keys=["wall_ms","world_streaming_phase_ms","mesh_emerge_ms","mesh_emerge_prep_unfinished_ms","mesh_dirty_tick_ms","dirty","dirty_fm_n","dirty_remesh_n","mesh_dirty_schedule_ok_n","mesh_dirty_schedule_skip_n","dirty_revisit_same_n","focus_missing_mesh","visual_holes","visible_black_focus_n","dark_face_void_near_n","opaque_cmd_on","column_absent_in_rd_n","gen_backlog_total","relight_fifo_dropped","column_loaded_no_mesh_n","column_render_ready_n","column_meshing_n","chunk_count","frontier_pressure"]
    print(f"\n=== {name} n={len(rows)} ===")
    for k in keys:
        vals=[r.get(k) for r in rows if isinstance(r.get(k),(int,float))]
        if not vals: print(f"  {k}: MISSING"); continue
        vals=sorted(vals)
        print(f"  {k}: med={vals[len(vals)//2]} p90={vals[min(len(vals)-1,int(len(vals)*0.9))]} max={vals[-1]}")
    miss=sum(1 for r in rows if r.get("focus_missing_mesh"))/len(rows)*100
    holes=sum(1 for r in rows if r.get("visual_holes"))/len(rows)*100
    print(f"  miss%={miss:.1f} holes%={holes:.1f}")
    print("  uf_reason", Counter(r.get("underfeet_reason") for r in rows))
    print("  focus range", rows[0].get("focus_cx"), rows[0].get("focus_cz"), "->", rows[-1].get("focus_cx"), rows[-1].get("focus_cz"))

path=r"bin/logs/perf_20260816-102747_5292.jsonl"
rows=load(path)
summarize("102747 NEW", rows)

# timeline
print("\n--- timeline ---")
for i in [0,20,40,60,80,100,120,140,160,-1]:
    idx=i if i>=0 else len(rows)+i
    r=rows[idx]
    print(
        f"i={idx} focus=({r.get('focus_cx')},{r.get('focus_cz')}) "
        f"wall={r.get('wall_ms'):.0f} emerge={r.get('mesh_emerge_ms'):.0f} ufprep={r.get('mesh_emerge_prep_unfinished_ms'):.0f} "
        f"dirty={r.get('dirty')} skip={r.get('mesh_dirty_schedule_skip_n')} ok={r.get('mesh_dirty_schedule_ok_n')} "
        f"miss={r.get('focus_missing_mesh')} holes={r.get('visual_holes')} vb={r.get('visible_black_focus_n')} "
        f"opaque={r.get('opaque_cmd_on')} uf_r={r.get('underfeet_reason')} "
        f"no_mesh={r.get('column_loaded_no_mesh_n')} rr={r.get('column_render_ready_n')} meshing={r.get('column_meshing_n')} "
        f"fifo_d={r.get('relight_fifo_dropped')}"
    )

for f,label in [
 (r"bin/logs/perf_20260815-203518_21932.jsonl","203518 baseline"),
 (r"bin/logs/perf_20260815-222059_21608.jsonl","222059 post-SoT"),
 (r"bin/logs/perf_20260816-100951_23068.jsonl","100951 long"),
]:
    summarize(label, load(f))
