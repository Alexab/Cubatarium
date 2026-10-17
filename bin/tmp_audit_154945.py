import json
from collections import Counter
from pathlib import Path

rows = []
for line in Path(r"bin/logs/perf_20260824-154945_26956.jsonl").read_text(
    encoding="utf-8", errors="replace"
).splitlines():
    if line.startswith("{"):
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)

s = rows[60:]
print("steady frames", len(s), "dur_s", len(rows) * 2)
app = [r for r in s if (r.get("relight_apply_n") or 0) > 0]
mr0 = [r for r in app if (r.get("mark_relit_schedule_n") or 0) == 0]
print(
    "apply>0",
    len(app),
    "apply+relit",
    sum(1 for r in app if (r.get("mark_relit_schedule_n") or 0) > 0),
    "apply but mark_relit=0",
    len(mr0),
)
print("  skip_dirty", Counter(int(r.get("mark_relit_skip_already_dirty_n") or 0) for r in mr0))
print("  skip_inflight", Counter(int(r.get("mark_relit_skip_inflight_n") or 0) for r in mr0))
print("  skip_quiesce", Counter(int(r.get("mark_relit_skip_enter_lit_quiesce_n") or 0) for r in mr0))
print("  consume_path", Counter(int(r.get("mark_relit_path_primary_consume_n") or 0) for r in mr0))
print(
    "  empty_relit_ms>0.001",
    sum(1 for r in mr0 if (r.get("mark_relit_empty_relit_ms") or 0) > 0.001),
)
print(
    "  mark_relit_total_ms>0.05",
    sum(1 for r in mr0 if (r.get("mark_relit_total_ms") or 0) > 0.05),
)
print(
    "  exec_ms>0.001",
    sum(1 for r in mr0 if (r.get("mark_relit_exec_ms") or 0) > 0.001),
)
print(
    "  mark_dirty_ms>0.001",
    sum(1 for r in mr0 if (r.get("mark_relit_mark_dirty_ms") or 0) > 0.001),
)
print("  apply_n", Counter(int(r.get("relight_apply_n") or 0) for r in mr0))
print("  light_skip", Counter(int(r.get("relight_light_skip_n") or 0) for r in mr0))
print(
    "  install_ms p50",
    sorted(float(r.get("relight_apply_install_ms") or 0) for r in mr0)[len(mr0) // 2]
    if mr0
    else None,
)
print(
    "  total_ms p50",
    sorted(float(r.get("mark_relit_total_ms") or 0) for r in mr0)[len(mr0) // 2]
    if mr0
    else None,
)

print("\ni  t   unf mesh nt  VB   a  mr  skip fm  fifo drop gpu")
for i, r in enumerate(rows):
    if i % 10 == 0:
        print(
            f"{i:3d} {i*2:3d}s {r.get('unfinished_visual'):3} "
            f"{r.get('column_loaded_no_mesh_n'):3} {r.get('visible_black_no_ticket_n'):3} "
            f"{r.get('visible_black_focus_n'):3} a={r.get('relight_apply_n'):2} "
            f"mr={r.get('mark_relit_schedule_n'):2} sk={r.get('mesh_dirty_schedule_skip_n'):3} "
            f"fm={r.get('dirty_fm_n'):3} fifo={r.get('relight_fifo_n'):2} "
            f"drop={r.get('relight_fifo_dropped'):4} gpu={r.get('pending_gpu_applies_n')}"
        )

unf = [r.get("unfinished_visual") or 0 for r in s]
nm = [r.get("column_loaded_no_mesh_n") or 0 for r in s]
print("steady unfinished min/max", min(unf), max(unf))
print("steady no_mesh min/max", min(nm), max(nm))
print("steady fifo_drop first/last", s[0].get("relight_fifo_dropped"), s[-1].get("relight_fifo_dropped"))
print("enter fifo_drop", rows[0].get("relight_fifo_dropped"), rows[29].get("relight_fifo_dropped"))

# VB vs unfinished correlation
print("\nVB unique", Counter(int(r.get("visible_black_focus_n") or 0) for r in s).most_common(8))
print("stalled unique", Counter(int(r.get("visible_black_stalled_n") or 0) for r in s).most_common(8))
