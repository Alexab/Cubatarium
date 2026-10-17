import json
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

s = load("bin/logs/perf_20260825-170807_29680.jsonl")[60:]
d = []
for i in range(1, len(s)):
    a = int(s[i - 1].get("chunk_meshed_unlit_hidden") or 0)
    b = int(s[i].get("chunk_meshed_unlit_hidden") or 0)
    if a != b:
        d.append(b - a)
print("unlit flip n", len(d), "median_delta", sorted(d)[len(d) // 2] if d else None, "max_abs", max(abs(x) for x in d) if d else None)
print("stale>200 frames", sum(1 for r in s if int(r.get("dark_face_stale_near_n") or 0) > 200))
print("stale max", max(int(r.get("dark_face_stale_near_n") or 0) for r in s))
print("dark_near max", max(int(r.get("dark_face_near_n") or 0) for r in s))
ops = [int(r.get("opaque_refs_cpu_vis") or 0) for r in s]
print("opaque min/med/max", min(ops), sorted(ops)[len(ops) // 2], max(ops))
print("vb_nt med", sorted(int(r.get("visible_black_no_ticket_n") or 0) for r in s)[len(s) // 2])
print("remesh_cap hist", dict(sorted(Counter(int(r.get("remesh_schedule_cap") or -1) for r in s).items())))
print("rem_q med", sorted(int(r.get("dirty_remesh_n") or 0) for r in s)[len(s) // 2])
print("protect hist", dict(sorted(Counter(int(r.get("remesh_protect_lit_settle_n") or 0) for r in s).items())))
