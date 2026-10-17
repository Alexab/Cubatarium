import json
from pathlib import Path
from statistics import median

def load(p):
    rows = []
    for l in Path(p).open(encoding="utf-8"):
        o = json.loads(l)
        if o.get("kind") == "period":
            rows.append(o)
    return rows

def mid(rows):
    # mid corridor: moving west-ish, y>=54, speed>2
    out = []
    for o in rows:
        sp = o.get("movement_speed") or 0
        y = o.get("player_y") or 0
        cx = o.get("focus_cx")
        if sp > 2 and y >= 54 and cx is not None and -10 <= cx <= 7:
            out.append(o)
    return out or rows

def summarize(label, path):
    rows = mid(load(path))
    keys = [
        "dirty_remesh_n",
        "visible_black_focus_n",
        "visible_black_fully_dark_stalled_n",
        "unfinished_visual",
        "column_loaded_no_mesh_n",
        "softdefer_held_n",
        "pass_dual_backend_same_coord_n",
        "publication_material_block_id_flip_n",
        "fog_pull_in_rd",
        "wall_ms",
    ]
    print("===", label, "n=", len(rows))
    for k in keys:
        vals = [o[k] for o in rows if o.get(k) is not None]
        if not vals:
            print(k, "MISSING")
            continue
        print(
            f"{k}: med={median(vals)} max={max(vals)} min={min(vals)}"
        )

summarize("161124", "bin/logs/perf_20260919-161124_66972.jsonl")
summarize("R1_noop", "bin/logs/perf_20260919-163147_12680.jsonl")
