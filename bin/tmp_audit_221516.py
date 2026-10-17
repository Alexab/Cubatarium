"""Manual post-P15 flight audit vs P13 170807 / P14 FAIL 205739 / SoftDefer 215042."""
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


def rate(rows, key):
    if len(rows) < 2:
        return None
    d = int(rows[-1].get(key) or 0) - int(rows[0].get(key) or 0)
    return d / max(1, len(rows) - 1)


def moving_rows(rows):
    out = []
    for i, r in enumerate(rows):
        if i == 0:
            continue
        if (r.get("player_x"), r.get("player_z")) != (
            rows[i - 1].get("player_x"),
            rows[i - 1].get("player_z"),
        ):
            out.append(r)
    return out


def summ(label, path):
    u = load(path)
    s = steady(u)
    late = s[-60:] if len(s) >= 60 else s
    mv = moving_rows(s)
    print(f"\n=== {label} spikes={len(u)} steady={len(s)} moving={len(mv)} ===")
    if not s:
        return
    px = [float(r.get("player_x") or 0) for r in s]
    pz = [float(r.get("player_z") or 0) for r in s]
    print(
        f"  xz ({px[0]:.0f},{pz[0]:.0f})->({px[-1]:.0f},{pz[-1]:.0f}) "
        f"dx={px[-1]-px[0]:.0f} dz={pz[-1]-pz[0]:.0f}"
    )
    keys = [
        "wall_ms",
        "keep_cols",
        "opaque_refs_cpu_vis",
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "focus_missing_mesh",
        "pending_light_focus",
        "visible_black_focus_n",
        "visible_black_no_ticket_n",
        "dark_face_stale_near_n",
        "dark_face_near_n",
        "dark_face_void_near_n",
        "dirty_fm_n",
        "dirty_remesh_n",
        "remesh_schedule_cap",
        "capture_bg_cap_n",
        "softdefer_empty_stuck_n",
        "softdefer_capture_pin_age",
        "softdefer_ingress_witness_n",
        "softdefer_capture_retarget_n",
        "black_sticky",
        "stream_pressure",
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
    vb = g(s, "visible_black_no_ticket_n")
    unf = g(s, "unfinished_visual")
    nm = g(s, "column_loaded_no_mesh_n")
    print(
        f"  flips unlit={flips(uh)} vbnt={flips(vb)} unf={flips(unf)} "
        f"no_mesh={flips(nm)}"
    )
    print(
        f"  flip_rate unlit={flips(uh)/max(1,len(s)-1):.2f} "
        f"vbnt={flips(vb)/max(1,len(s)-1):.2f} "
        f"unf={flips(unf)/max(1,len(s)-1):.2f}"
    )
    for k in [
        "softdefer_witness_retarget",
        "softdefer_capture_floor_hits",
        "softdefer_ingress_witness_n",
        "softdefer_capture_retarget_n",
        "pending_light_dropped",
        "relight_fifo_dropped",
    ]:
        r = rate(s, k)
        if r is None:
            continue
        print(f"  {k} /spike={r:.2f}")

    print("  late:")
    for k in [
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "chunk_meshed_unlit_hidden",
        "visible_black_no_ticket_n",
        "dark_face_stale_near_n",
        "focus_missing_mesh",
        "keep_cols",
        "capture_bg_cap_n",
        "softdefer_empty_stuck_n",
        "opaque_refs_cpu_vis",
    ]:
        xs = g(late, k)
        if all(x is None for x in xs):
            continue
        print(f"    {k:40s} med={med(xs):8.2f} max={max(float(x or 0) for x in xs):8.1f}")

    if mv:
        print("  moving window:")
        for k in [
            "unfinished_visual",
            "column_loaded_no_mesh_n",
            "chunk_meshed_unlit_hidden",
            "visible_black_no_ticket_n",
            "focus_missing_mesh",
            "dark_face_stale_near_n",
            "softdefer_empty_stuck_n",
        ]:
            xs = g(mv, k)
            if all(x is None for x in xs):
                continue
            print(
                f"    {k:40s} med={med(xs):8.2f} p90={p90(xs):8.1f} "
                f"max={max(float(x or 0) for x in xs):8.1f}"
            )
        # hole spikes: unfinished or no_mesh high while moving
        hole = [
            r
            for r in mv
            if int(r.get("unfinished_visual") or 0) >= 40
            or int(r.get("column_loaded_no_mesh_n") or 0) >= 40
            or int(r.get("focus_missing_mesh") or 0) > 0
        ]
        print(f"  moving hole-ish spikes (unf/no_mesh>=40 or miss): {len(hole)}/{len(mv)}")
        if hole[:5]:
            for r in hole[:8]:
                print(
                    f"    xz=({r.get('player_x')},{r.get('player_z')}) "
                    f"unf={r.get('unfinished_visual')} nm={r.get('column_loaded_no_mesh_n')} "
                    f"uh={r.get('chunk_meshed_unlit_hidden')} vbnt={r.get('visible_black_no_ticket_n')} "
                    f"miss={r.get('focus_missing_mesh')} stale={r.get('dark_face_stale_near_n')} "
                    f"stuck={r.get('softdefer_empty_stuck_n')} keep={r.get('keep_cols')}"
                )


for lab, p in [
    ("NEW MANUAL 221516", r"bin/logs/perf_20260825-221516_28600.jsonl"),
    ("post-revert SoftDefer 215042", r"bin/logs/perf_20260825-215042_1656.jsonl"),
    ("P13 170807", r"bin/logs/perf_20260825-170807_29680.jsonl"),
    ("P14 FAIL 205739", r"bin/logs/perf_20260825-205739_3064.jsonl"),
]:
    if Path(p).is_file():
        summ(lab, p)
