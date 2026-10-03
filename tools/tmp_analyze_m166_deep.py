import json
import re
from collections import Counter, defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[1]
perf = root / "bin/logs/perf_20260929-233110_35024.jsonl"
log = root / "bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260929-233106.35024"
rows = [json.loads(line) for line in perf.read_text(encoding="utf-8").splitlines() if line.startswith("{")]
pixels = [r for r in rows if r.get("kind") == "renderer_pixel_probe"]
def packed(r):
    return (round(r.get("renderer_pixel_opaque_vertex_block_light", 0) * 15) << 4) | round(r.get("renderer_pixel_opaque_vertex_sky_light", 0) * 15)
stale = [r for r in pixels if r.get("renderer_pixel_opaque_surface_valid") and r.get("renderer_pixel_opaque_vertex_light_valid") and r.get("renderer_pixel_opaque_live_face_light_valid") and packed(r) == 0 and r.get("renderer_pixel_opaque_live_face_light_packed", 0) != 0 and r.get("renderer_pixel_opaque_field_light_rev", 0) > r.get("renderer_pixel_opaque_published_light_rev", 0)]
hits = {(r.get("renderer_pixel_opaque_chunk_x"), r.get("renderer_pixel_opaque_chunk_y"), r.get("renderer_pixel_opaque_chunk_z")) for r in stale}
coord = lambda r: (r.get("cx"), r.get("cy"), r.get("cz"))
jobs = [r for r in rows if r.get("kind") == "job_trace"]
hit_jobs = [r for r in jobs if coord(r) in hits]
print("pixel_stale", len(stale), "hit_chunks", len(hits), sorted(hits))
print("job_trace_total", len(jobs), "hit_rows", len(hit_jobs), "terminal_reasons", dict(Counter(r.get("terminal_reason") for r in hit_jobs)), "stages", dict(Counter(r.get("stage") for r in hit_jobs)))
print("no_active_owner", [{k:r.get(k) for k in ("cx","cy","cz","job_id","stage","terminal_reason","source_light_rev","published_light_rev","elapsed_ms")} for r in hit_jobs if r.get("terminal_reason") == 6][-20:])
for kind in ("mesh_schedule_trace", "visual_black_trace", "visual_lifecycle_trace"):
    selected = [r for r in rows if r.get("kind") == kind and coord(r) in hits]
    print(kind, "hit_rows", len(selected), "causes", dict(Counter((r.get("cause"), r.get("mesh_snapshot_defer_reason"), r.get("mesh_enqueue_reject_reason")) for r in selected)))
    print(kind, "last", [{k:r.get(k) for k in ("cx","cy","cz","sample_kind","frame_epoch","cause","mesh_work_owner_flags","field_light_rev","meshed_light_rev","published_light_rev","terminal_reason","reason")} for r in selected[-12:]])
debt = defaultdict(Counter)
for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
    if "stale_light_debt event=" not in line:
        continue
    c = re.search(r"coord=\((-?\d+),(-?\d+),(-?\d+)\)", line)
    ev = re.search(r"stale_light_debt event=([^ ]+)", line)
    if c and ev:
        key = tuple(map(int, c.groups()))
        if key in hits:
            debt[key][ev.group(1)] += 1
print("hit_debt_events", {c:dict(v) for c,v in debt.items()})
