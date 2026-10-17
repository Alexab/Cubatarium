"""Forensic: why unfinished grows with apply=3 on P11 120512."""
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


def steady(u):
    return u[60:] if len(u) > 60 else u[len(u) // 2 :]


def med(xs):
    xs = [float(x) for x in xs if x is not None]
    return st.median(xs) if xs else None


def summ(name, rows):
    s = steady(rows)
    print(f"\n=== {name} steady={len(s)} ===")
    keys = [
        "apply_binding",
        "relight_apply_n",
        "relight_completed_n",
        "relight_apply_ms",
        "relight_apply_light_ms",
        "relight_apply_install_ms",
        "mark_relit_invoked_n",
        "mark_relit_schedule_n",
        "mark_relit_skip_already_dirty_n",
        "mark_relit_path_primary_consume_n",
        "column_lighting_n",
        "column_meshing_n",
        "column_render_ready_n",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "unfinished_visual",
        "dirty_fm_n",
        "dirty_remesh_n",
        "dirty_admit_budget_end",
        "mesh_dirty_schedule_ok_n",
        "mesh_dirty_schedule_skip_n",
        "mesh_dirty_schedule_skip_softdefer_n",
        "mesh_dirty_schedule_skip_orphan_n",
        "mesh_dirty_schedule_skip_pipeline_n",
        "mesh_dirty_schedule_skip_snapshot_n",
        "mesh_dirty_schedule_skip_locked_n",
        "mesh_dirty_schedule_skip_remesh_starve_n",
        "mesh_dirty_schedule_skip_other_n",
        "mesh_discarded_late",
        "gpu_finish_n",
        "gpu_finish_not_ready_n",
        "pending_gpu_applies_n",
        "pending_gpu_queued_n",
        "softdefer_empty_stuck_n",
        "softdefer_empty_owned_n",
        "softdefer_empty_placeholder_n",
        "softdefer_held_n",
        "pending_light",
        "pending_light_focus",
        "visible_black_progress_n",
        "visible_black_stalled_n",
        "visible_black_no_ticket_n",
        "relight_light_skip_n",
        "relight_light_chunks_n",
        "heal_deferred_for_miss",
        "focus_missing_mesh",
        "first_mesh_schedule_cap",
        "remesh_schedule_cap",
        "mesh_schedule_final",
        "mesh_drain_final",
        "dirty_revisit_same_n",
        "dirty_touch_n",
        "capture_bg_cap_n",
        "relight_fifo_n",
        "relight_capture_full_n",
        "relight_capture_finalize",
    ]
    skip_keys = sorted(
        {
            k
            for r in s
            for k in r
            if k.startswith("mark_relit_skip") or k.startswith("mark_relit_")
        }
    )
    seen = set()
    for k in keys + skip_keys:
        if k in seen:
            continue
        seen.add(k)
        xs = [r.get(k) for r in s]
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:44s} med={med(xs):8.2f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    print(
        "  apply_binding hist",
        dict(sorted(Counter(int(r.get("apply_binding") or -1) for r in s).items())),
    )
    for a in range(0, 6):
        sub = [r for r in s if int(r.get("relight_apply_n") or 0) == a]
        if not sub:
            continue
        print(
            f"  apply={a} n={len(sub)} "
            f"unf={med([r.get('unfinished_visual') for r in sub])} "
            f"nm={med([r.get('column_loaded_no_mesh_n') for r in sub])} "
            f"uh={med([r.get('chunk_meshed_unlit_hidden') for r in sub])} "
            f"sch={med([r.get('mark_relit_schedule_n') for r in sub])} "
            f"fm={med([r.get('dirty_fm_n') for r in sub])} "
            f"ok={med([r.get('mesh_dirty_schedule_ok_n') for r in sub])} "
            f"meshing={med([r.get('column_meshing_n') for r in sub])} "
            f"ready={med([r.get('column_render_ready_n') for r in sub])}"
        )

    # net publication rate proxy
    print(
        "  schedule_ok sum",
        sum(int(r.get("mesh_dirty_schedule_ok_n") or 0) for r in s),
        "mark_schedule sum",
        sum(int(r.get("mark_relit_schedule_n") or 0) for r in s),
        "gpu_finish sum",
        sum(int(r.get("gpu_finish_n") or 0) for r in s),
        "discarded_late end-start",
        int(s[-1].get("mesh_discarded_late") or 0)
        - int(s[0].get("mesh_discarded_late") or 0),
        "pl_dropped end-start",
        int(s[-1].get("pending_light_dropped") or 0)
        - int(s[0].get("pending_light_dropped") or 0),
    )

    # unfinished decomposition identity
    eqs = 0
    for r in s:
        u = int(r.get("unfinished_visual") or 0)
        nm = int(r.get("column_loaded_no_mesh_n") or 0)
        nr = int(r.get("chunk_not_ready") or 0)
        if u == nm == nr:
            eqs += 1
    print(f"  unfinished==no_mesh==not_ready frames {eqs}/{len(s)}")

    # SoftDefer stuck age
    ages = [int(r.get("softdefer_empty_age_max_frames") or 0) for r in s]
    print(
        f"  softdefer empty_age_max med={med(ages)} "
        f"p90={sorted(ages)[int(0.9*len(ages))]} max={max(ages)}"
    )

    # mid vs late unfinished trend
    mid = s[len(s) // 3 : 2 * len(s) // 3]
    late = s[-60:]
    print(
        f"  mid unf/nm/uh/plf "
        f"{med([r.get('unfinished_visual') for r in mid])}/"
        f"{med([r.get('column_loaded_no_mesh_n') for r in mid])}/"
        f"{med([r.get('chunk_meshed_unlit_hidden') for r in mid])}/"
        f"{med([r.get('pending_light_focus') for r in mid])}"
    )
    print(
        f"  late unf/nm/uh/plf "
        f"{med([r.get('unfinished_visual') for r in late])}/"
        f"{med([r.get('column_loaded_no_mesh_n') for r in late])}/"
        f"{med([r.get('chunk_meshed_unlit_hidden') for r in late])}/"
        f"{med([r.get('pending_light_focus') for r in late])}"
    )


for label, p in [
    ("P11 120512", r"bin/logs/perf_20260825-120512_18456.jsonl"),
    ("P10 110448", r"bin/logs/perf_20260825-110448_26856.jsonl"),
    ("P7 194347", r"bin/logs/perf_20260824-194347_32236.jsonl"),
]:
    summ(label, load(p))


def dod_gate(label, path, apply_min=2):
    """FZ27-P12 final gate vs 120512/110448 targets."""
    rows = load(path)
    s = steady(rows)
    late = s[-60:] if len(s) >= 60 else s
    if not late:
        print(f"\n=== DoD {label}: empty ===")
        return
    def m(key, window=late):
        return med([r.get(key) for r in window])

    def delta_per_spike(key):
        if len(s) < 2:
            return None
        return (int(s[-1].get(key) or 0) - int(s[0].get(key) or 0)) / max(
            1, len(s) - 1
        )

    apply_rows = [r for r in s if int(r.get("relight_apply_n") or 0) >= apply_min]
    sch = med([r.get("mark_relit_schedule_n") for r in apply_rows]) if apply_rows else None
    fm = m("dirty_fm_n")
    nm = m("column_loaded_no_mesh_n")
    ratio = (fm / nm) if fm is not None and nm and nm > 0 else None
    meshing = m("column_meshing_n")
    ready = m("column_render_ready_n")
    gap = (meshing - ready) if meshing is not None and ready is not None else None
    targets = [
        ("late unfinished ≤25", m("unfinished_visual"), 25, "<="),
        ("late unlit_hidden ≤15", m("chunk_meshed_unlit_hidden"), 15, "<="),
        ("late pl_focus ≤20", m("pending_light_focus"), 20, "<="),
        ("meshing−ready med ≤35", gap, 35, "<="),
        ("fm/no_mesh ≥0.8", ratio, 0.8, ">="),
        ("mark_relit_schedule med≥1 @apply≥2", sch, 1.0, ">="),
        ("pl_dropped Δ/spike ≤2", delta_per_spike("pending_light_dropped"), 2.0, "<="),
        ("witness_retarget Δ/spike ≤3", delta_per_spike("softdefer_witness_retarget"), 3.0, "<="),
        ("opaque ≥650", m("opaque_draw_n") or m("opaque_chunks"), 650, ">="),
        ("capture_bg med ≥3", m("capture_bg_cap_n"), 3.0, ">="),
    ]
    print(f"\n=== P12 DoD {label} late={len(late)} ===")
    for name, val, tgt, op in targets:
        if val is None:
            status = "MISSING"
        elif op == "<=":
            status = "PASS" if val <= tgt else "FAIL"
        else:
            status = "PASS" if val >= tgt else "FAIL"
        print(f"  {status:7s} {name:42s} got={val} target={op}{tgt}")


# After a P12 manual run, append e.g.:
# dod_gate("P12 <stamp>", r"bin/logs/perf_<stamp>.jsonl")
dod_gate("P11 baseline 120512", r"bin/logs/perf_20260825-120512_18456.jsonl")
dod_gate("P10 ref 110448", r"bin/logs/perf_20260825-110448_26856.jsonl")
