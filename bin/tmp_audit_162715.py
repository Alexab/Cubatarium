import json
import statistics as st
from collections import Counter
from pathlib import Path


def load(p):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def p90(xs):
    xs = sorted(float(x) for x in xs if x is not None)
    return xs[min(len(xs) - 1, int(0.9 * len(xs)))] if xs else None


def flips(xs):
    return sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1])


def summ(name, u, keys):
    s = u[60:] if len(u) > 60 else u[len(u) // 2 :]
    tail = u[-15:]
    print(f"=== {name} n={len(u)} ~{len(u)*2}s steady={len(s)} tail={len(tail)} ===")
    for k in keys:
        xs = [r.get(k) for r in s]
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:36s} med={med(xs):8.2f} p90={p90(xs):8.2f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    print(
        "  mark_relit hist",
        dict(sorted(Counter(int(r.get("mark_relit_schedule_n") or 0) for r in s).items())[:12]),
        "nonzero",
        sum(1 for r in s if (r.get("mark_relit_schedule_n") or 0) > 0),
        "/",
        len(s),
    )
    print("  apply_n hist", dict(sorted(Counter(int(r.get("relight_apply_n") or 0) for r in s).items())))
    print(
        "  completed hist",
        dict(sorted(Counter(int(r.get("relight_completed_n") or 0) for r in s).items())[:12]),
    )
    vb = [r.get("visible_black_focus_n") for r in s]
    print("  VB flips", flips(vb), "rate", flips(vb) / max(1, len(vb)))
    app = [r for r in s if (r.get("relight_apply_n") or 0) > 0]
    mr0 = [r for r in app if (r.get("mark_relit_schedule_n") or 0) == 0]
    print("  apply>0", len(app), "apply but mark_relit=0", len(mr0))
    print(
        "  skip_dirty",
        Counter(int(r.get("mark_relit_skip_already_dirty_n") or 0) for r in mr0),
    )
    print("  capture_bg_cap", Counter(int(r.get("capture_bg_cap_n") or -1) for r in s).most_common(6))
    print("  remesh_cap", Counter(int(r.get("remesh_schedule_cap") or 0) for r in s))
    print("  fifo_drop first/last", s[0].get("relight_fifo_dropped"), s[-1].get("relight_fifo_dropped"))
    print("  fifo_drop enter/end", u[0].get("relight_fifo_dropped"), u[-1].get("relight_fifo_dropped"))
    unf = [r.get("unfinished_visual") or 0 for r in s]
    print("  unfinished min/max", min(unf), max(unf))
    print("  tail unfinished", [r.get("unfinished_visual") for r in tail])
    print("  tail no_mesh", [r.get("column_loaded_no_mesh_n") for r in tail])
    print("  tail VB", [r.get("visible_black_focus_n") for r in tail])
    print("  tail unlit_h", [r.get("chunk_meshed_unlit_hidden") for r in tail])
    print("  tail not_ready", [r.get("chunk_not_ready") for r in tail])
    print("  tail dirty_fm", [r.get("dirty_fm_n") for r in tail])
    print("  tail apply/relit", [(r.get("relight_apply_n"), r.get("mark_relit_schedule_n")) for r in tail])
    print("  tail fifo_drop", [r.get("relight_fifo_dropped") for r in tail])
    print("  tail fifo_n", [r.get("relight_fifo_n") for r in tail])
    print("  tail skip/ok", [(r.get("mesh_dirty_schedule_skip_n"), r.get("mesh_dirty_schedule_ok_n")) for r in tail])


KEYS = [
    "sim_ms",
    "stream_ms",
    "relight_apply_n",
    "relight_completed_n",
    "relight_fifo_n",
    "pending_light",
    "pending_light_focus",
    "visible_black_focus_n",
    "visible_black_stalled_n",
    "mark_relit_schedule_n",
    "mark_relit_skip_already_dirty_n",
    "relight_light_skip_n",
    "relight_apply_install_ms",
    "chunk_not_ready",
    "unfinished_visual",
    "focus_missing_mesh",
    "chunk_meshed_unlit_hidden",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_skip_n",
    "dirty_revisit_same_n",
    "dirty_fm_n",
    "dirty_remesh_n",
    "pending_gpu_applies_n",
    "opaque_refs_cpu_vis",
    "column_loaded_no_mesh_n",
    "relight_fifo_dropped",
    "relight_fifo_drop_n",
    "relight_trim_far_n",
    "relight_fifo_pin_saved_n",
    "capture_bg_cap_n",
    "remesh_schedule_cap",
    "first_mesh_schedule_cap",
    "relight_capture_full_n",
]

new = load(r"bin/logs/perf_20260824-162715_23128.jsonl")
old = load(r"bin/logs/perf_20260824-154945_26956.jsonl")
summ("162715 P6", new, KEYS)
print()
summ("154945 P1-P4", old, KEYS)

print("\n--- 162715 every 10 spikes ---")
print("i  t   unf mesh VB  a  mr  skip fm  fifo drop cap remesh")
for i, r in enumerate(new):
    if i % 10 == 0 or i == len(new) - 1:
        print(
            f"{i:3d} {i*2:3d}s {r.get('unfinished_visual'):3} "
            f"{r.get('column_loaded_no_mesh_n'):3} {r.get('visible_black_focus_n'):3} "
            f"a={r.get('relight_apply_n'):2} mr={r.get('mark_relit_schedule_n'):2} "
            f"sk={r.get('mesh_dirty_schedule_skip_n'):3} fm={r.get('dirty_fm_n'):3} "
            f"fifo={r.get('relight_fifo_n'):2} drop={r.get('relight_fifo_dropped'):4} "
            f"cap={r.get('capture_bg_cap_n')} rem={r.get('remesh_schedule_cap')} "
            f"trim={r.get('relight_trim_far_n')} pin_saved={r.get('relight_fifo_pin_saved_n')}"
        )
