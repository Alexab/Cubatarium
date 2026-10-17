import json
from pathlib import Path

def load(p):
    rows = []
    for line in Path(p).read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "spike":
                rows.append(r)
    return rows

u = load("bin/logs/perf_20260825-154246_29372.jsonl")
s = u[60:]
print("timeline")
for i in [0, 30, 60, 120, 300, 600, 900, 1200]:
    if i >= len(s):
        break
    r = s[i]
    print(
        "i=%d dark=%s stale=%s void=%s unf=%s uh=%s rem_q=%s rem_cap=%s"
        % (
            i,
            r.get("dark_face_near_n"),
            r.get("dark_face_stale_near_n"),
            r.get("dark_face_void_near_n"),
            r.get("unfinished_visual"),
            r.get("chunk_meshed_unlit_hidden"),
            r.get("dirty_remesh_n"),
            r.get("remesh_schedule_cap"),
        )
    )
