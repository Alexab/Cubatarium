#!/usr/bin/env python3
import json
from pathlib import Path

d = json.loads(
    Path("bin/suite_reports/g1_a10_relight/manual_191112_audit.json").read_text(
        encoding="utf-8"
    )
)
c, s = d["current"], d["sot"]
print("cx", c.get("focus_cx_range"), "periods", c["periods"])
keys = [
    "visible_black_focus",
    "visible_black_fully_dark_repair",
    "unfinished_visual",
    "prior_lit_hold_n",
    "publication_material_block_id_flip_n",
    "pass_dual_backend_same_coord_n",
    "transparent_cmd_reorder_n",
    "dark_face_void_near",
    "dark_face_stale",
    "fog_pull_in_rd",
    "gpu_kick_n",
    "ok_remesh_n",
    "mark_relit_invoked_n",
    "stale_accepted_refresh",
    "mesh_apply_stale_geom",
]
for k in keys:
    print(
        f"{k}: CUR fly {c[k]['fly_med']}|{c[k]['fly_max']} tail {c[k]['tail_med']}|{c[k]['tail_max']}  ||  SOT fly {s[k]['fly_med']}|{s[k]['fly_max']}"
    )
print("TAIL:")
for t in c["tail_timeline"]:
    print(t)
