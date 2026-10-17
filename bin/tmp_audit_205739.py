"""P14 manual 205739 vs P13 170807 / P12 154246."""
import json
import statistics as st
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
    xs = [x for x in xs if x is not None]
    return sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1])


def steady(u):
    return u[60:] if len(u) > 60 else u[len(u) // 2 :]


def g(rows, key):
    return [r.get(key) for r in rows]


def summ(label, path):
    u = load(path)
    s = steady(u)
    late = s[-60:] if len(s) >= 60 else s
    n = len(s)
    early, mid, late3 = s[: n // 3], s[n // 3 : 2 * n // 3], s[2 * n // 3 :]
    print(f"\n=== {label} spikes={len(u)} steady={len(s)} ===")

    pz = [float(r.get("player_z") or 0) for r in s]
    px = [float(r.get("player_x") or 0) for r in s]
    print(
        f"  player xz start=({px[0]:.0f},{pz[0]:.0f}) "
        f"end=({px[-1]:.0f},{pz[-1]:.0f}) "
        f"dz={pz[-1]-pz[0]:.0f} dx={px[-1]-px[0]:.0f}"
    )
    keys = [
        "wall_ms",
        "keep_cols",
        "opaque_refs_cpu_vis",
        "opaque_refs_render_ready",
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "focus_missing_mesh",
        "pending_light_focus",
        "visible_black_focus_n",
        "visible_black_no_ticket_n",
        "dark_face_near_n",
        "dark_face_stale_near_n",
        "dark_face_void_near_n",
        "dirty_fm_n",
        "dirty_remesh_n",
        "remesh_schedule_cap",
        "first_mesh_schedule_cap",
        "remesh_protect_lit_settle_n",
        "capture_bg_cap_n",
        "relight_apply_n",
        "relight_fifo_n",
        "black_sticky",
        "softdefer_empty_stuck_n",
        "softdefer_empty_age_max_frames",
        "softdefer_witness_retarget",
        "softdefer_capture_pin_age",
        "mesh_dirty_schedule_skip_remesh_starve_n",
        "keep_ring_shrink_n",
        "freechunk_live_n",
        "pending_light_dropped",
        "stream_pressure",
        "vb_no_ticket_repair_cap",
        "visible_black_no_ticket_raw_n",
    ]
    for k in keys:
        xs = g(s, k)
        if all(x is None for x in xs):
            continue
        print(
            f"  {k:42s} med={med(xs):8.2f} p90={p90(xs):8.1f} "
            f"max={max(float(x or 0) for x in xs):8.1f}"
        )

    uh = g(s, "chunk_meshed_unlit_hidden")
    bs = g(s, "black_sticky")
    op = g(s, "opaque_refs_cpu_vis")
    vb = g(s, "visible_black_no_ticket_n")
    print(
        f"  flips unlit={flips(uh)} black_sticky={flips(bs)} "
        f"opaque={flips(op)} vb_no_ticket={flips(vb)}"
    )
    if len(s) >= 2:
        for k in [
            "pending_light_dropped",
            "softdefer_witness_retarget",
            "relight_fifo_dropped",
            "softdefer_capture_floor_hits",
        ]:
            d = int(s[-1].get(k) or 0) - int(s[0].get(k) or 0)
            print(f"  {k} delta={d} /spike={d/max(1,len(s)-1):.2f}")

    print("  thirds:")
    for name, w in [("early", early), ("mid", mid), ("late", late3)]:
        print(
            f"    {name}: unf={med(g(w,'unfinished_visual'))} "
            f"uh={med(g(w,'chunk_meshed_unlit_hidden'))} "
            f"stale={med(g(w,'dark_face_stale_near_n'))} "
            f"vbnt={med(g(w,'visible_black_no_ticket_n'))} "
            f"rem_cap={med(g(w,'remesh_schedule_cap'))} "
            f"prot={med(g(w,'remesh_protect_lit_settle_n'))} "
            f"opaque={med(g(w,'opaque_refs_cpu_vis'))} "
            f"fm={med(g(w,'dirty_fm_n'))} rem={med(g(w,'dirty_remesh_n'))} "
            f"keep={med(g(w,'keep_cols'))} "
            f"z={med(g(w,'player_z'))}"
        )

    print("  late window:")
    for k in [
        "unfinished_visual",
        "chunk_meshed_unlit_hidden",
        "dark_face_stale_near_n",
        "dark_face_near_n",
        "visible_black_no_ticket_n",
        "visible_black_focus_n",
        "column_loaded_no_mesh_n",
        "remesh_schedule_cap",
        "remesh_protect_lit_settle_n",
        "dirty_fm_n",
        "dirty_remesh_n",
        "opaque_refs_cpu_vis",
        "capture_bg_cap_n",
        "pending_light_focus",
        "keep_cols",
        "wall_ms",
        "softdefer_witness_retarget",
        "softdefer_capture_pin_age",
        "mesh_dirty_schedule_skip_remesh_starve_n",
    ]:
        xs = g(late, k)
        if all(x is None for x in xs):
            continue
        print(f"    {k:40s} med={med(xs):8.2f} max={max(float(x or 0) for x in xs):8.1f}")

    print("  P14 DoD vs 170807:")
    checks = [
        ("vb_no_ticket med<=15", med(g(late, "visible_black_no_ticket_n")), 15, "<="),
        ("witness_retarget /spike <=3", (int(s[-1].get("softdefer_witness_retarget") or 0) - int(s[0].get("softdefer_witness_retarget") or 0)) / max(1, len(s) - 1), 3, "<="),
        ("late unfinished <=30", med(g(late, "unfinished_visual")), 30, "<="),
        ("late unlit <=25", med(g(late, "chunk_meshed_unlit_hidden")), 25, "<="),
        ("late stale <=20 (P13 win)", med(g(late, "dark_face_stale_near_n")), 20, "<="),
        ("capture >=3 KEEP", med(g(late, "capture_bg_cap_n")), 3, ">="),
        ("opaque >=650 KEEP", med(g(late, "opaque_refs_cpu_vis")), 650, ">="),
        ("keep_cols >=160 KEEP", med(g(late, "keep_cols")), 160, ">="),
        ("PL drop/spike ~0 KEEP", (int(s[-1].get("pending_light_dropped") or 0) - int(s[0].get("pending_light_dropped") or 0)) / max(1, len(s) - 1), 0.5, "<="),
        ("dirty_fm >=40", med(g(late, "dirty_fm_n")), 40, ">="),
        ("rem_cap >=2", med(g(late, "remesh_schedule_cap")), 2, ">="),
    ]
    for name, val, tgt, op in checks:
        if val is None:
            stt = "MISSING"
        elif op == "<=":
            stt = "PASS" if val <= tgt else "FAIL"
        else:
            stt = "PASS" if val >= tgt else "FAIL"
        print(f"    {stt:7s} {name:36s} got={val} {op}{tgt}")


for lab, p in [
    ("P14 NEW 205739", r"bin/logs/perf_20260825-205739_3064.jsonl"),
    ("P13 170807", r"bin/logs/perf_20260825-170807_29680.jsonl"),
    ("P12 154246", r"bin/logs/perf_20260825-154246_29372.jsonl"),
]:
    summ(lab, p)
