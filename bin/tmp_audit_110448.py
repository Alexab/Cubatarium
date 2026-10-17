"""P10 manual 110448 vs P9 091745 / P7 194347."""
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


def steady(u):
    return u[60:] if len(u) > 60 else u[len(u) // 2 :]


def apply0_runs(u):
    run = 0
    runs = []
    for i, r in enumerate(u):
        if int(r.get("relight_apply_n") or 0) == 0:
            run += 1
        elif run:
            runs.append(run)
            run = 0
    if run:
        runs.append(run)
    return runs


KEYS = [
    "sim_ms",
    "wall_ms",
    "relight_apply_n",
    "relight_completed_n",
    "relight_fifo_n",
    "pending_light",
    "pending_light_focus",
    "visible_black_focus_n",
    "mark_relit_invoked_n",
    "mark_relit_schedule_n",
    "mark_missing_primary_n",
    "capture_bg_cap_n",
    "unfinished_visual",
    "chunk_meshed_unlit_hidden",
    "column_loaded_no_mesh_n",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_skip_n",
    "mesh_dirty_schedule_skip_orphan_n",
    "mesh_dirty_schedule_skip_remesh_starve_n",
    "mesh_dirty_schedule_skip_locked_n",
    "mesh_dirty_schedule_skip_pipeline_n",
    "mesh_dirty_schedule_skip_other_n",
    "dirty_fm_n",
    "dirty_remesh_n",
    "opaque_refs_cpu_vis",
    "opaque_refs_render_ready",
    "keep_cols",
    "keep_ring_shrink_n",
    "freechunk_live_n",
    "relight_fifo_dropped",
    "gpu_finish_n",
    "first_mesh_schedule_cap",
]


def summ(name, u):
    s = steady(u)
    tail = u[-12:]
    print(f"\n=== {name} spikes={len(u)} ~{len(u)*2}s steady={len(s)} ===")
    for k in KEYS:
        xs = [r.get(k) for r in s]
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:42s} med={med(xs):8.2f} p90={p90(xs):8.2f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    over = sum(1 for r in s if float(r.get("sim_ms") or 0) > 135)
    print(f"  sim>135 {over}/{len(s)} ({100*over/max(1,len(s)):.1f}%)")
    runs = apply0_runs(s)
    print(
        f"  apply0 runs n={len(runs)} longest={max(runs) if runs else 0} "
        f">=10={sum(1 for x in runs if x >= 10)}"
    )
    ready_ge2 = [r for r in s if int(r.get("relight_completed_n") or 0) >= 2]
    if ready_ge2:
        print(f"  when completed>=2: n={len(ready_ge2)} apply med={med([r.get('relight_apply_n') for r in ready_ge2])}")
    starve = [
        r
        for r in s
        if int(r.get("relight_fifo_n") or 0) >= 50
        and int(r.get("relight_completed_n") or 0) <= 1
    ]
    print(
        f"  fifo starve frames {len(starve)} cap hist "
        f"{dict(Counter(int(r.get('capture_bg_cap_n') or -1) for r in starve))}"
    )
    fifo50_a0 = sum(
        1
        for r in s
        if int(r.get("relight_fifo_n") or 0) >= 50
        and int(r.get("relight_apply_n") or 0) == 0
    )
    fifo50 = sum(1 for r in s if int(r.get("relight_fifo_n") or 0) >= 50)
    print(f"  fifo>=50 & apply=0: {fifo50_a0}/{fifo50}")
    print(
        "  apply hist",
        dict(sorted(Counter(int(r.get("relight_apply_n") or 0) for r in s).items())),
    )
    print(
        "  keep hist",
        dict(sorted(Counter(int(r.get("keep_cols") or 0) for r in s).items())),
    )
    print(
        "  fifo_drop enter/end",
        u[0].get("relight_fifo_dropped"),
        u[-1].get("relight_fifo_dropped"),
    )
    print(
        "  unfinished min/max",
        min(r.get("unfinished_visual") or 0 for r in s),
        max(r.get("unfinished_visual") or 0 for r in s),
    )
    print("  tail unfinished", [r.get("unfinished_visual") for r in tail])
    print("  tail no_mesh", [r.get("column_loaded_no_mesh_n") for r in tail])
    print("  tail unlit", [r.get("chunk_meshed_unlit_hidden") for r in tail])
    print("  tail opaque", [r.get("opaque_refs_cpu_vis") for r in tail])
    print("  tail keep", [r.get("keep_cols") for r in tail])
    print("  tail dirty_fm", [r.get("dirty_fm_n") for r in tail])
    print(
        "  tail apply/comp/cap",
        [
            (r.get("relight_apply_n"), r.get("relight_completed_n"), r.get("capture_bg_cap_n"))
            for r in tail
        ],
    )
    print(
        "  tail skip orphan/remesh/other/ok",
        [
            (
                r.get("mesh_dirty_schedule_skip_orphan_n"),
                r.get("mesh_dirty_schedule_skip_remesh_starve_n"),
                r.get("mesh_dirty_schedule_skip_other_n"),
                r.get("mesh_dirty_schedule_ok_n"),
            )
            for r in tail
        ],
    )
    print("  --- timeline ---")
    step = max(1, len(u) // 10)
    for i in list(range(0, len(u), step)) + (
        [len(u) - 1] if (len(u) - 1) % step else []
    ):
        r = u[i]
        print(
            f"  {i:4d} ~{i*2:4d}s sim={float(r.get('sim_ms') or 0):6.1f} "
            f"unf={r.get('unfinished_visual')} nm={r.get('column_loaded_no_mesh_n')} "
            f"uh={r.get('chunk_meshed_unlit_hidden')} a={r.get('relight_apply_n')} "
            f"c={r.get('relight_completed_n')} cap={r.get('capture_bg_cap_n')} "
            f"fm={r.get('dirty_fm_n')} opaque={r.get('opaque_refs_cpu_vis')} "
            f"keep={r.get('keep_cols')} fifo={r.get('relight_fifo_n')} "
            f"shrink={r.get('keep_ring_shrink_n')} fc={r.get('freechunk_live_n')}"
        )


p10 = load(r"bin/logs/perf_20260825-110448_26856.jsonl")
p9 = load(r"bin/logs/perf_20260825-091745_23048.jsonl")
p7 = load(r"bin/logs/perf_20260824-194347_32236.jsonl")
summ("110448 P10", p10)
summ("091745 P9", p9)
summ("194347 P7", p7)

s = steady(p10)
print("\n=== P10 DoD vs P9/P7 ===")
print(f"duration ~{len(p10)*2}s (need >=600)")
print(f"sim med {med([r.get('sim_ms') for r in s]):.1f}")
print(
    f"sim>135% {100*sum(1 for r in s if float(r.get('sim_ms') or 0)>135)/max(1,len(s)):.1f}"
)
print(f"opaque med {med([r.get('opaque_refs_cpu_vis') for r in s])} (P9=212 P7=660; FAIL if <400)")
print(f"unfinished med {med([r.get('unfinished_visual') for r in s])} tail {[r.get('unfinished_visual') for r in p10[-5:]]}")
print(f"no_mesh med {med([r.get('column_loaded_no_mesh_n') for r in s])}")
print(f"unlit med {med([r.get('chunk_meshed_unlit_hidden') for r in s])} (P7~8)")
print(f"completed med {med([r.get('relight_completed_n') for r in s])} (P9=0)")
print(f"capture_bg med {med([r.get('capture_bg_cap_n') for r in s])} (P9=1)")
print(f"dirty_fm med {med([r.get('dirty_fm_n') for r in s])}")
print(f"skip/ok {med([r.get('mesh_dirty_schedule_skip_n') for r in s])} / {med([r.get('mesh_dirty_schedule_ok_n') for r in s])}")
print(f"keep_ring_shrink sum {sum(int(r.get('keep_ring_shrink_n') or 0) for r in s)}")
print(f"freechunk_live sum {sum(int(r.get('freechunk_live_n') or 0) for r in s)}")
print(f"VB flips rate {flips([r.get('visible_black_focus_n') for r in s])/max(1,len(s)):.3f}")
