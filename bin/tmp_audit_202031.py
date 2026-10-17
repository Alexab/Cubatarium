import json
import statistics as st
from pathlib import Path

rows = [
    json.loads(l)
    for l in Path("bin/logs/perf_20260920-202031_6988.jsonl")
    .read_text(encoding="utf-8")
    .splitlines()
    if l.strip()
]

print("periods with kick>16 or finish>16 or emerge>33 or render>80:")
for i, r in enumerate(rows):
    kick = r.get("mesh_gpu_kick_ms") or 0
    fin = r.get("mesh_gpu_finish_ms") or 0
    em = r.get("mesh_emerge_ms") or 0
    rt = r.get("render_total_ms") or 0
    if kick > 16 or fin > 16 or em > 33 or rt > 80:
        print(
            f"  p{i}: emerge={em:.1f} kick={kick:.1f} finish={fin:.1f} "
            f"render={rt:.1f} dirty_tick={r.get('mesh_dirty_tick_ms', 0):.1f} "
            f"unf={r.get('unfinished_visual')} holes={r.get('near_focus_holes')} "
            f"softdefer_held={r.get('softdefer_held_n')} "
            f"stale_vis={r.get('mesh_apply_stale_visual')}"
        )

held = [r.get("softdefer_held_n", 0) for r in rows]
print("softdefer_held_n med/max", st.median(held), max(held))
ph = [r.get("softdefer_empty_placeholder_n", 0) for r in rows]
print("softdefer_empty_placeholder_n med/max", st.median(ph), max(ph))
print("spikes med/max", st.median([r.get("spikes", 0) for r in rows]), max(r.get("spikes", 0) for r in rows))
print("mesh_apply_stale_visual med/max", st.median([r.get("mesh_apply_stale_visual", 0) for r in rows]), max(r.get("mesh_apply_stale_visual", 0) for r in rows))
