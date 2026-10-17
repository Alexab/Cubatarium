import json
from pathlib import Path

p = Path("bin/logs/enter_lit_20260827-102235.jsonl")
rows = [json.loads(l) for l in p.open(encoding="utf-8") if l.strip()]
for r in rows[::10]:
    print(
        f"t={r['elapsed_ms']:.0f} miss={r['mesh_missing_greedy']} "
        f"dn={r['dirty_n']} async={int(r['mesh_async_pending'])} "
        f"gpu={r['mesh_gpu_pending_near']} pg={r['pending_gpu_global']} "
        f"raa={r['remesh_after_apply_n']} skip_inf={r['dirty_schedule_skip_inflight_n']} "
        f"top=({r['top_dirty_cx']},{r['top_dirty_cz']}) "
        f"q={r['enter_lit_quiesce']} frozen={r['streaming_frozen']} "
        f"debt={r['visibility_debt']} gate_done={r['gate_done_n']}"
    )
