import json
from pathlib import Path

p = Path(r"bin/logs/perf_20260824-162715_23128.jsonl")
rows = []
for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
    if line.startswith("{"):
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)

print("n", len(rows))
skip_keys = sorted(
    k
    for k in rows[-1]
    if any(s in k for s in ("skip", "inflight", "hole", "unlit", "fifo", "pending_gpu", "first_mesh"))
)
print("keys", skip_keys)

last = -1
for i, r in enumerate(rows):
    d = r.get("relight_fifo_dropped") or 0
    if d != last:
        print(
            "drop",
            last,
            "->",
            d,
            "i",
            i,
            "t",
            i * 2,
            "fifo",
            r.get("relight_fifo_n"),
            "unf",
            r.get("unfinished_visual"),
        )
        last = d

run = 0
runs = []
for i, r in enumerate(rows):
    a = int(r.get("relight_apply_n") or 0)
    if a == 0:
        run += 1
    elif run:
        runs.append((i - run, i - 1, run))
        run = 0
if run:
    runs.append((len(rows) - run, len(rows) - 1, run))
print("apply0_runs", runs)

print("pending_gpu_tail", [r.get("pending_gpu_applies_n") for r in rows[-15:]])
print("light_skip_tail", [r.get("relight_light_skip_n") for r in rows[-15:]])
print("opaque_tail", [r.get("opaque_refs_cpu_vis") for r in rows[-15:]])
print("pl_focus_tail", [r.get("pending_light_focus") for r in rows[-15:]])
print("stalled_tail", [r.get("visible_black_stalled_n") for r in rows[-15:]])


def corr(a, b):
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va * vb == 0:
        return 0.0
    return sum((a[i] - ma) * (b[i] - mb) for i in range(n)) / (va * vb) ** 0.5


s = rows[60:]
nm = [r.get("column_loaded_no_mesh_n") or 0 for r in s]
uh = [r.get("chunk_meshed_unlit_hidden") or 0 for r in s]
vb = [r.get("visible_black_focus_n") or 0 for r in s]
un = [r.get("unfinished_visual") or 0 for r in s]
print("corr_no_mesh_unlit", round(corr(nm, uh), 3))
print("corr_vb_unlit", round(corr(vb, uh), 3))
print("corr_vb_no_mesh", round(corr(vb, nm), 3))
print("corr_unf_unlit", round(corr(un, uh), 3))
print("corr_unf_no_mesh", round(corr(un, nm), 3))

print("--- last 15 ---")
for i, r in enumerate(rows[-15:], start=len(rows) - 15):
    print(
        i,
        i * 2,
        "unf",
        r.get("unfinished_visual"),
        "nm",
        r.get("column_loaded_no_mesh_n"),
        "uh",
        r.get("chunk_meshed_unlit_hidden"),
        "VB",
        r.get("visible_black_focus_n"),
        "a",
        r.get("relight_apply_n"),
        "mr",
        r.get("mark_relit_schedule_n"),
        "ls",
        r.get("relight_light_skip_n"),
        "gpu",
        r.get("pending_gpu_applies_n"),
        "ok",
        r.get("mesh_dirty_schedule_ok_n"),
        "sk",
        r.get("mesh_dirty_schedule_skip_n"),
    )
