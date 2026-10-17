"""P12 manual 154246 vs P11 120512 / P10 110448 — black chunk regression check."""
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


def steady(u):
    return u[60:] if len(u) > 60 else u[len(u) // 2 :]


def g(rows, key):
    return [r.get(key) for r in rows]


def summ(label, path):
    u = load(path)
    s = steady(u)
    late = s[-60:] if len(s) >= 60 else s
    print(f"\n=== {label} spikes={len(u)} steady={len(s)} ===")
    keys = [
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "chunk_not_ready",
        "pending_light",
        "pending_light_focus",
        "visible_black_focus_n",
        "visible_black_no_ticket_n",
        "visible_black_stalled_n",
        "dark_face_near_n",
        "dark_face_void_near_n",
        "focus_dark_mesh",
        "focus_missing_mesh",
        "dirty_fm_n",
        "dirty_remesh_n",
        "column_meshing_n",
        "column_render_ready_n",
        "mark_relit_schedule_n",
        "mark_relit_skip_already_dirty_n",
        "mark_relit_enqueue_first_mesh_n",
        "relight_apply_n",
        "capture_bg_cap_n",
        "opaque_refs_cpu_vis",
        "opaque_refs_render_ready",
        "remesh_schedule_cap",
        "first_mesh_schedule_cap",
        "pending_light_dropped",
        "pending_light_trim_emerge_n",
        "pending_light_trim_memory_n",
        "softdefer_empty_stuck_n",
        "softdefer_empty_age_max_frames",
        "softdefer_witness_retarget",
        "softdefer_capture_pin_age",
        "mesh_dirty_schedule_skip_remesh_starve_n",
        "keep_cols",
        "relight_fifo_n",
        "wall_ms",
    ]
    for k in keys:
        xs = g(s, k)
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:42s} med={med(xs):8.2f} p90={p90(xs):8.1f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    print("  late window:")
    for k in [
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "pending_light_focus",
        "visible_black_focus_n",
        "visible_black_no_ticket_n",
        "dirty_fm_n",
        "dirty_remesh_n",
        "opaque_refs_cpu_vis",
        "mark_relit_schedule_n",
        "mark_relit_enqueue_first_mesh_n",
        "first_mesh_schedule_cap",
        "remesh_schedule_cap",
        "capture_bg_cap_n",
    ]:
        xs = g(late, k)
        if all(x is None for x in xs):
            continue
        print(
            f"    {k:40s} med={med(xs):8.2f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )
    if len(s) >= 2:
        for k in [
            "pending_light_dropped",
            "softdefer_witness_retarget",
            "relight_fifo_dropped",
        ]:
            d = int(s[-1].get(k) or 0) - int(s[0].get(k) or 0)
            print(f"  {k} delta={d} per_spike={d / max(1, len(s) - 1):.2f}")
    fm = med(g(late, "dirty_fm_n"))
    nm = med(g(late, "column_loaded_no_mesh_n"))
    if fm is not None and nm and nm > 0:
        print(f"  late fm/no_mesh={fm / nm:.3f}")
    meshing = med(g(late, "column_meshing_n"))
    ready = med(g(late, "column_render_ready_n"))
    if meshing is not None and ready is not None:
        print(f"  late meshing-ready={meshing - ready:.1f}")
    print(
        "  apply hist",
        dict(sorted(Counter(int(r.get("relight_apply_n") or -1) for r in s).items())),
    )
    n = len(s)
    for name, w in [
        ("early", s[: n // 3]),
        ("mid", s[n // 3 : 2 * n // 3]),
        ("late", s[2 * n // 3 :]),
    ]:
        print(
            f"  {name}: unf={med(g(w, 'unfinished_visual'))} "
            f"uh={med(g(w, 'chunk_meshed_unlit_hidden'))} "
            f"vb={med(g(w, 'visible_black_focus_n'))} "
            f"plf={med(g(w, 'pending_light_focus'))} "
            f"opaque={med(g(w, 'opaque_refs_cpu_vis'))} "
            f"fm={med(g(w, 'dirty_fm_n'))} "
            f"rem={med(g(w, 'dirty_remesh_n'))} "
            f"rem_cap={med(g(w, 'remesh_schedule_cap'))}"
        )

    # DoD vs P12 targets
    print("  P12 DoD:")
    targets = [
        ("late unfinished <=25", med(g(late, "unfinished_visual")), 25, "<="),
        ("late unlit <=15", med(g(late, "chunk_meshed_unlit_hidden")), 15, "<="),
        ("late pl_focus <=20", med(g(late, "pending_light_focus")), 20, "<="),
        (
            "meshing-ready <=35",
            (meshing - ready) if meshing is not None and ready is not None else None,
            35,
            "<=",
        ),
        (
            "fm/no_mesh >=0.8",
            (fm / nm) if fm is not None and nm and nm > 0 else None,
            0.8,
            ">=",
        ),
        ("opaque >=650", med(g(late, "opaque_refs_cpu_vis")), 650, ">="),
        ("capture_bg >=3", med(g(late, "capture_bg_cap_n")), 3, ">="),
    ]
    apply_rows = [r for r in s if int(r.get("relight_apply_n") or 0) >= 2]
    sch = med(g(apply_rows, "mark_relit_schedule_n")) if apply_rows else None
    targets.append(("schedule@apply>=2 >=1", sch, 1.0, ">="))
    pl_d = (
        (int(s[-1].get("pending_light_dropped") or 0) - int(s[0].get("pending_light_dropped") or 0))
        / max(1, len(s) - 1)
        if len(s) >= 2
        else None
    )
    targets.append(("pl_drop/spike <=2", pl_d, 2.0, "<="))
    wr = (
        (
            int(s[-1].get("softdefer_witness_retarget") or 0)
            - int(s[0].get("softdefer_witness_retarget") or 0)
        )
        / max(1, len(s) - 1)
        if len(s) >= 2
        else None
    )
    targets.append(("witness_retarget/spike <=3", wr, 3.0, "<="))
    for name, val, tgt, op in targets:
        if val is None:
            status = "MISSING"
        elif op == "<=":
            status = "PASS" if val <= tgt else "FAIL"
        else:
            status = "PASS" if val >= tgt else "FAIL"
        print(f"    {status:7s} {name:36s} got={val} target={op}{tgt}")


for lab, p in [
    ("P12 NEW 154246", r"bin/logs/perf_20260825-154246_29372.jsonl"),
    ("P11 120512", r"bin/logs/perf_20260825-120512_18456.jsonl"),
    ("P10 110448", r"bin/logs/perf_20260825-110448_26856.jsonl"),
]:
    summ(lab, p)
