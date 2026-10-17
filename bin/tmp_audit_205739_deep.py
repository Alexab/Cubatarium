"""Deep forensics P14 205739 vs P13 170807."""
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


def flips(xs):
    xs = [x for x in xs if x is not None]
    return sum(1 for i in range(1, len(xs)) if xs[i] != xs[i - 1])


def dump(lab, p):
    u = load(p)
    print(f"\n### {lab} n={len(u)}")
    step = max(1, len(u) // 12)
    for i in range(0, len(u), step):
        r = u[i]
        print(
            f"  [{i:4d}] xz=({r.get('player_x')},{r.get('player_z')}) "
            f"unf={r.get('unfinished_visual')} uh={r.get('chunk_meshed_unlit_hidden')} "
            f"vbnt={r.get('visible_black_no_ticket_n')} "
            f"vbraw={r.get('visible_black_no_ticket_raw_n')} "
            f"stale={r.get('dark_face_stale_near_n')} "
            f"pin={r.get('softdefer_capture_pin_age')} "
            f"stuck={r.get('softdefer_empty_stuck_n')} "
            f"age={r.get('softdefer_empty_age_max_frames')} "
            f"wr={r.get('softdefer_witness_retarget')} "
            f"floor={r.get('softdefer_capture_floor_hits')} "
            f"fifo={r.get('relight_fifo_n')} "
            f"cap={r.get('capture_bg_cap_n')} wall={r.get('wall_ms')}"
        )
    r = u[-1]
    print(
        f"  [last] xz=({r.get('player_x')},{r.get('player_z')}) "
        f"unf={r.get('unfinished_visual')} uh={r.get('chunk_meshed_unlit_hidden')} "
        f"vbnt={r.get('visible_black_no_ticket_n')} "
        f"stale={r.get('dark_face_stale_near_n')} "
        f"pin={r.get('softdefer_capture_pin_age')} "
        f"stuck={r.get('softdefer_empty_stuck_n')} "
        f"wr={r.get('softdefer_witness_retarget')}"
    )
    moved = sum(
        1
        for i in range(1, len(u))
        if (u[i].get("player_x"), u[i].get("player_z"))
        != (u[i - 1].get("player_x"), u[i - 1].get("player_z"))
    )
    print(f"  position_changes={moved}/{len(u)-1} ({100*moved/max(1,len(u)-1):.1f}%)")

    pins = [int(r.get("softdefer_capture_pin_age") or 0) for r in u]
    print(
        f"  pin_age med={med(pins)} p90={sorted(pins)[int(0.9*len(pins))]} "
        f"max={max(pins)} >=40={sum(1 for x in pins if x>=40)} >=48={sum(1 for x in pins if x>=48)}"
    )
    uh = [int(r.get("chunk_meshed_unlit_hidden") or 0) for r in u]
    print(f"  unlit med={med(uh)} flips={flips(uh)} flip_rate={flips(uh)/max(1,len(u)-1):.2f}")
    vb = [int(r.get("visible_black_no_ticket_n") or 0) for r in u]
    print(f"  vbnt med={med(vb)} flips={flips(vb)} flip_rate={flips(vb)/max(1,len(u)-1):.2f}")
    vbr = [r.get("visible_black_no_ticket_raw_n") for r in u]
    if any(x is not None for x in vbr):
        vbr2 = [int(x or 0) for x in vbr]
        print(f"  vbraw med={med(vbr2)} flips={flips(vbr2)} flip_rate={flips(vbr2)/max(1,len(u)-1):.2f}")

    for a, b, name in [(0, min(60, len(u)), "enter60"), (60, len(u), "rest")]:
        if b <= a + 1:
            continue
        wr = int(u[b - 1].get("softdefer_witness_retarget") or 0) - int(
            u[a].get("softdefer_witness_retarget") or 0
        )
        fl = int(u[b - 1].get("softdefer_capture_floor_hits") or 0) - int(
            u[a].get("softdefer_capture_floor_hits") or 0
        )
        fd = int(u[b - 1].get("relight_fifo_dropped") or 0) - int(
            u[a].get("relight_fifo_dropped") or 0
        )
        print(
            f"  {name}: wr/sp={wr/max(1,b-a-1):.2f} floor/sp={fl/max(1,b-a-1):.2f} "
            f"fifo_drop/sp={fd/max(1,b-a-1):.2f} "
            f"unf={med([r.get('unfinished_visual') for r in u[a:b]])} "
            f"stale={med([r.get('dark_face_stale_near_n') for r in u[a:b]])} "
            f"uh={med([r.get('chunk_meshed_unlit_hidden') for r in u[a:b]])} "
            f"no_mesh={med([r.get('column_loaded_no_mesh_n') for r in u[a:b]])} "
            f"vbnt={med([r.get('visible_black_no_ticket_n') for r in u[a:b]])}"
        )

    # SoftDefer empty stuck sticky?
    stuck = [int(r.get("softdefer_empty_stuck_n") or 0) for r in u]
    print(f"  stuck med={med(stuck)} max={max(stuck)} flips={flips(stuck)}")

    # black proxies sticky plateaus
    for k in [
        "dark_face_stale_near_n",
        "dark_face_void_near_n",
        "visible_black_focus_n",
        "focus_unfinished_ahead",
        "focus_unfinished_behind",
    ]:
        xs = [r.get(k) for r in u]
        if all(x is None for x in xs):
            continue
        print(f"  {k}: med={med(xs)} max={max(float(x or 0) for x in xs)} flips={flips(xs)}")


for lab, p in [
    ("P14 205739", "bin/logs/perf_20260825-205739_3064.jsonl"),
    ("P13 170807", "bin/logs/perf_20260825-170807_29680.jsonl"),
]:
    dump(lab, p)
