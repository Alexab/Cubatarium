"""P9 manual 091745 vs P7 194347 / P8 213856."""
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


KEYS = [
    "sim_ms",
    "wall_ms",
    "fps",
    "relight_apply_n",
    "relight_completed_n",
    "relight_fifo_n",
    "pending_light",
    "pending_light_focus",
    "visible_black_focus_n",
    "mark_relit_schedule_n",
    "mark_relit_invoked_n",
    "mark_missing_primary_n",
    "capture_bg_cap_n",
    "capture_band_cy",
    "unfinished_visual",
    "chunk_meshed_unlit_hidden",
    "mesh_dirty_schedule_ok_n",
    "mesh_dirty_schedule_skip_n",
    "mesh_dirty_schedule_skip_pipeline_n",
    "mesh_dirty_schedule_skip_snapshot_n",
    "mesh_dirty_schedule_skip_softdefer_n",
    "mesh_dirty_schedule_skip_locked_n",
    "dirty_fm_n",
    "dirty_remesh_n",
    "opaque_refs_cpu_vis",
    "column_loaded_no_mesh_n",
    "relight_fifo_dropped",
    "first_mesh_schedule_cap",
    "sticky_insert_stale_after_apply_n",
]


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


def summ(name, u):
    s = steady(u)
    tail = u[-12:]
    dur_s = len(u) * 2
    print(f"\n=== {name} spikes={len(u)} ~{dur_s}s steady={len(s)} ===")
    for k in KEYS:
        xs = [r.get(k) for r in s]
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:40s} med={med(xs):8.2f} p90={p90(xs):8.2f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    vb = [r.get("visible_black_focus_n") for r in s]
    print(f"  VB flips {flips(vb)} rate={flips(vb)/max(1,len(vb)):.3f}")
    over = sum(1 for r in s if float(r.get("sim_ms") or 0) > 135)
    print(f"  sim>135 {over}/{len(s)} ({100*over/max(1,len(s)):.1f}%)")
    runs = apply0_runs(s)
    long10 = sum(1 for x in runs if x >= 10)
    print(
        f"  apply0 runs: n={len(runs)} longest={max(runs) if runs else 0} "
        f">=10spikes={long10}"
    )
    # apply when ready>=2
    ready_ge2 = [r for r in s if int(r.get("relight_completed_n") or 0) >= 2]
    if ready_ge2:
        a_med = med([r.get("relight_apply_n") for r in ready_ge2])
        print(f"  when completed>=2: n={len(ready_ge2)} apply_n med={a_med}")
    fifo50_a0 = sum(
        1
        for r in s
        if int(r.get("relight_fifo_n") or 0) >= 50
        and int(r.get("relight_apply_n") or 0) == 0
    )
    fifo50 = sum(1 for r in s if int(r.get("relight_fifo_n") or 0) >= 50)
    print(f"  fifo>=50 & apply=0: {fifo50_a0}/{fifo50}")
    print(
        "  mark_relit_invoked hist",
        dict(
            sorted(
                Counter(int(r.get("mark_relit_invoked_n") or 0) for r in s).items()
            )[:12]
        ),
    )
    print(
        "  mark_relit_schedule hist",
        dict(
            sorted(
                Counter(int(r.get("mark_relit_schedule_n") or 0) for r in s).items()
            )[:12]
        ),
    )
    print(
        "  mark_missing_primary hist",
        dict(
            sorted(
                Counter(int(r.get("mark_missing_primary_n") or 0) for r in s).items()
            )[:12]
        ),
    )
    print(
        "  apply hist",
        dict(sorted(Counter(int(r.get("relight_apply_n") or 0) for r in s).items())),
    )
    print(
        "  fifo_drop enter/end",
        u[0].get("relight_fifo_dropped"),
        u[-1].get("relight_fifo_dropped"),
    )
    print("  unfinished min/max", min(r.get("unfinished_visual") or 0 for r in s), max(r.get("unfinished_visual") or 0 for r in s))
    print("  tail unfinished", [r.get("unfinished_visual") for r in tail])
    print("  tail no_mesh", [r.get("column_loaded_no_mesh_n") for r in tail])
    print("  tail unlit", [r.get("chunk_meshed_unlit_hidden") for r in tail])
    print("  tail VB", [r.get("visible_black_focus_n") for r in tail])
    print("  tail dirty_fm", [r.get("dirty_fm_n") for r in tail])
    print("  tail opaque", [r.get("opaque_refs_cpu_vis") for r in tail])
    print(
        "  tail apply/comp/cap",
        [
            (
                r.get("relight_apply_n"),
                r.get("relight_completed_n"),
                r.get("capture_bg_cap_n"),
            )
            for r in tail
        ],
    )
    print(
        "  tail skip locked/pipe/soft",
        [
            (
                r.get("mesh_dirty_schedule_skip_locked_n"),
                r.get("mesh_dirty_schedule_skip_pipeline_n"),
                r.get("mesh_dirty_schedule_skip_softdefer_n"),
                r.get("mesh_dirty_schedule_ok_n"),
            )
            for r in tail
        ],
    )
    print(
        "  tail inv/sched/miss",
        [
            (
                r.get("mark_relit_invoked_n"),
                r.get("mark_relit_schedule_n"),
                r.get("mark_missing_primary_n"),
            )
            for r in tail
        ],
    )
    # timeline samples
    print("  --- timeline ---")
    step = max(1, len(u) // 10)
    for i in list(range(0, len(u), step)) + ([len(u) - 1] if (len(u) - 1) % step else []):
        r = u[i]
        print(
            f"  {i:4d} ~{i*2:4d}s sim={float(r.get('sim_ms') or 0):6.1f} "
            f"unf={r.get('unfinished_visual')} nm={r.get('column_loaded_no_mesh_n')} "
            f"uh={r.get('chunk_meshed_unlit_hidden')} VB={r.get('visible_black_focus_n')} "
            f"a={r.get('relight_apply_n')} c={r.get('relight_completed_n')} "
            f"cap={r.get('capture_bg_cap_n')} fm={r.get('dirty_fm_n')} "
            f"sk={r.get('mesh_dirty_schedule_skip_n')} ok={r.get('mesh_dirty_schedule_ok_n')} "
            f"opaque={r.get('opaque_refs_cpu_vis')} fifo={r.get('relight_fifo_n')}"
        )


p9 = load(r"bin/logs/perf_20260825-091745_23048.jsonl")
p7 = load(r"bin/logs/perf_20260824-194347_32236.jsonl")
p8 = load(r"bin/logs/perf_20260824-213856_6744.jsonl")
summ("091745 P9", p9)
summ("194347 P7", p7)
summ("213856 P8", p8)

# DoD checklist for P9
s = steady(p9)
print("\n=== P9 DoD checklist ===")
print(f"duration ~{len(p9)*2}s (need >=600)")
print(f"sim med {med([r.get('sim_ms') for r in s]):.1f} (need <135)")
print(f"sim>135% {100*sum(1 for r in s if float(r.get('sim_ms') or 0)>135)/max(1,len(s)):.1f} (need <<32)")
print(f"opaque med {med([r.get('opaque_refs_cpu_vis') for r in s])} (FAIL if <400)")
print(f"unfinished med {med([r.get('unfinished_visual') for r in s])} tail { [r.get('unfinished_visual') for r in p9[-5:]] }")
print(f"no_mesh med {med([r.get('column_loaded_no_mesh_n') for r in s])}")
print(f"unlit med {med([r.get('chunk_meshed_unlit_hidden') for r in s])} (P7~8)")
print(f"VB med {med([r.get('visible_black_focus_n') for r in s])} flips rate {flips([r.get('visible_black_focus_n') for r in s])/max(1,len(s)):.3f}")
print(f"fifo_drop delta {int(p9[-1].get('relight_fifo_dropped') or 0)-int(p9[0].get('relight_fifo_dropped') or 0)}")
print(f"dirty_fm med {med([r.get('dirty_fm_n') for r in s])}")
print(f"skip/ok med {med([r.get('mesh_dirty_schedule_skip_n') for r in s])} / {med([r.get('mesh_dirty_schedule_ok_n') for r in s])}")
miss_sum = sum(int(r.get("mark_missing_primary_n") or 0) for r in s)
print(f"mark_missing_primary sum steady {miss_sum} (storm if huge every frame)")
