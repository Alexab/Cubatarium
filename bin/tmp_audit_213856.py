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


KEYS = [
    "sim_ms",
    "wall_ms",
    "frame_ms",
    "stream_ms",
    "fps",
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
    "relight_apply_ms",
    "relight_apply_light_ms",
    "chunk_not_ready",
    "unfinished_visual",
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
    "remesh_schedule_cap",
    "first_mesh_schedule_cap",
    "relight_capture_full_n",
    "mesh_dirty_schedule_ms",
    "mesh_snapshot_ms",
]


def summ(name, u, keys):
    s = u[60:] if len(u) > 60 else u[len(u) // 2 :]
    tail = u[-15:]
    print(f"=== {name} n={len(u)} ~{len(u)*2}s steady={len(s)} ===")
    # discover fps-like keys
    sample = s[-1] if s else {}
    extra = [k for k in sample if any(x in k for x in ("fps", "wall", "frame", "sim_"))]
    print("  time keys", sorted(set(extra)))
    for k in keys:
        xs = [r.get(k) for r in s]
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:36s} med={med(xs):8.2f} p90={p90(xs):8.2f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    vb = [r.get("visible_black_focus_n") for r in s]
    print("  VB flips", flips(vb), "rate", round(flips(vb) / max(1, len(vb)), 3))
    print(
        "  mark_relit nonzero",
        sum(1 for r in s if (r.get("mark_relit_schedule_n") or 0) > 0),
        "/",
        len(s),
    )
    print(
        "  mark_relit hist",
        dict(sorted(Counter(int(r.get("mark_relit_schedule_n") or 0) for r in s).items())[:10]),
    )
    print("  apply hist", dict(sorted(Counter(int(r.get("relight_apply_n") or 0) for r in s).items())))
    app = [r for r in s if (r.get("relight_apply_n") or 0) > 0]
    mr0 = sum(1 for r in app if (r.get("mark_relit_schedule_n") or 0) == 0)
    print("  apply>0", len(app), "apply but mark_relit=0", mr0)
    print(
        "  skip_dirty on mr0",
        Counter(int(r.get("mark_relit_skip_already_dirty_n") or 0) for r in app if (r.get("mark_relit_schedule_n") or 0) == 0),
    )
    unf = [r.get("unfinished_visual") or 0 for r in s]
    print("  unfinished min/max", min(unf), max(unf))
    print("  fifo_drop enter/end", u[0].get("relight_fifo_dropped"), u[-1].get("relight_fifo_dropped"))
    print("  tail unfinished", [r.get("unfinished_visual") for r in tail])
    print("  tail no_mesh", [r.get("column_loaded_no_mesh_n") for r in tail])
    print("  tail VB", [r.get("visible_black_focus_n") for r in tail])
    print("  tail unlit_h", [r.get("chunk_meshed_unlit_hidden") for r in tail])
    print("  tail dirty_fm", [r.get("dirty_fm_n") for r in tail])
    print("  tail apply/mr", [(r.get("relight_apply_n"), r.get("mark_relit_schedule_n")) for r in tail])
    print("  tail skip/ok", [(r.get("mesh_dirty_schedule_skip_n"), r.get("mesh_dirty_schedule_ok_n")) for r in tail])
    print("  tail fifo", [r.get("relight_fifo_n") for r in tail])
    print("  tail sim", [round(float(r.get("sim_ms") or 0), 1) for r in tail])
    print("  tail wall", [round(float(r.get("wall_ms") or r.get("frame_ms") or 0), 1) for r in tail])
    print("  tail install", [round(float(r.get("relight_apply_install_ms") or 0), 2) for r in tail])
    print("  tail opaque", [r.get("opaque_refs_cpu_vis") for r in tail])


new = load(r"bin/logs/perf_20260824-213856_6744.jsonl")
old = load(r"bin/logs/perf_20260824-194347_32236.jsonl")
summ("213856 P8?", new, KEYS)
print()
summ("194347 P7", old, KEYS)

print("\n--- 213856 every 25 spikes ---")
for i, r in enumerate(new):
    if i % 25 == 0 or i == len(new) - 1:
        wall = r.get("wall_ms") or r.get("frame_ms") or 0
        print(
            f"{i:4d} {i*2:4d}s sim={float(r.get('sim_ms') or 0):6.1f} "
            f"wall={float(wall):6.1f} unf={r.get('unfinished_visual'):3} "
            f"nm={r.get('column_loaded_no_mesh_n'):3} uh={r.get('chunk_meshed_unlit_hidden'):3} "
            f"VB={r.get('visible_black_focus_n'):3} a={r.get('relight_apply_n'):2} "
            f"mr={r.get('mark_relit_schedule_n'):2} sk={r.get('mesh_dirty_schedule_skip_n'):3} "
            f"fm={r.get('dirty_fm_n'):3} fifo={r.get('relight_fifo_n'):3} "
            f"inst={float(r.get('relight_apply_install_ms') or 0):4.1f} "
            f"opaque={r.get('opaque_refs_cpu_vis')}"
        )

# apply0 long runs
run = 0
runs = []
for i, r in enumerate(new):
    a = int(r.get("relight_apply_n") or 0)
    if a == 0:
        run += 1
    elif run:
        runs.append((i - run, i - 1, run))
        run = 0
if run:
    runs.append((len(new) - run, len(new) - 1, run))
long_runs = [x for x in runs if x[2] >= 5]
print("\napply0 >=5:", long_runs[:15], "n", len(long_runs))
if runs:
    print("longest apply0", max(runs, key=lambda x: x[2]))

# sim kill
s = new[60:] if len(new) > 60 else new
over = sum(1 for r in s if float(r.get("sim_ms") or 0) > 135)
print(f"sim>135 steady frames: {over}/{len(s)} ({100*over/max(1,len(s)):.1f}%)")
