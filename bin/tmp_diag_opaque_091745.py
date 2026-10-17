"""Opaque collapse diagnosis for 091745."""
import json
from pathlib import Path
from collections import Counter

rows = []
for line in Path("bin/logs/perf_20260825-091745_23048.jsonl").read_text(
    encoding="utf-8", errors="replace"
).splitlines():
    if line.startswith("{"):
        r = json.loads(line)
        if r.get("kind") == "spike":
            rows.append(r)

sample = rows[70]
cands = [
    k
    for k in sample
    if any(
        x in k
        for x in (
            "opaque",
            "hide",
            "unlit",
            "gpu",
            "soft",
            "sticky",
            "fully",
            "drawable",
            "bind",
            "keep",
            "replace",
            "free",
            "pending_gpu",
            "mesh_apply",
            "raa",
            "publish",
            "meshed",
            "chunk_not",
            "residency",
            "cull",
        )
    )
]
print("publish-ish keys:", sorted(cands))

print("\n=== drops opaque>=50 ===")
for i in range(1, len(rows)):
    a = int(rows[i - 1].get("opaque_refs_cpu_vis") or 0)
    b = int(rows[i].get("opaque_refs_cpu_vis") or 0)
    if a - b < 50:
        continue
    r = rows[i]
    rp = rows[i - 1]
    print(
        f"drop {i-1}->{i}: {a}->{b} d={a-b} "
        f"uh {rp.get('chunk_meshed_unlit_hidden')}->{r.get('chunk_meshed_unlit_hidden')} "
        f"nm {rp.get('column_loaded_no_mesh_n')}->{r.get('column_loaded_no_mesh_n')} "
        f"unf {rp.get('unfinished_visual')}->{r.get('unfinished_visual')} "
        f"fm {rp.get('dirty_fm_n')}->{r.get('dirty_fm_n')} "
        f"rem {rp.get('dirty_remesh_n')}->{r.get('dirty_remesh_n')} "
        f"pgpu {rp.get('pending_gpu_applies_n')}->{r.get('pending_gpu_applies_n')} "
        f"apply={r.get('relight_apply_n')} mr={r.get('mark_relit_schedule_n')} "
        f"soft_av={r.get('softdefer_empty_publish_avoided')} "
        f"replace_hole={r.get('mesh_replace_hole_avoided')} "
        f"mesh_apply_stale={r.get('mesh_apply_stale_n')} "
        f"mesh_apply_super={r.get('mesh_apply_superseded_n')}"
    )

print("\n=== identity check: opaque vs (loaded - no_mesh - unlit?) proxies ===")
for i in [50, 55, 60, 65, 70, 80, 90, 110, 130, 149]:
    r = rows[i]
    op = int(r.get("opaque_refs_cpu_vis") or 0)
    uh = int(r.get("chunk_meshed_unlit_hidden") or 0)
    nm = int(r.get("column_loaded_no_mesh_n") or 0)
    unf = int(r.get("unfinished_visual") or 0)
    cnr = int(r.get("chunk_not_ready") or 0)
    pgpu = int(r.get("pending_gpu_applies_n") or 0)
    print(
        f"{i:3d} op={op:4d} uh={uh:3d} nm={nm:3d} unf={unf:3d} "
        f"cnr={cnr:4d} pgpu={pgpu:3d} fm={r.get('dirty_fm_n')} "
        f"rem={r.get('dirty_remesh_n')} skul={r.get('mesh_dirty_schedule_skip_locked_n')} "
        f"sk={r.get('mesh_dirty_schedule_skip_n')} ok={r.get('mesh_dirty_schedule_ok_n')}"
    )

# composition during low opaque
print("\n=== low opaque (<250) composition ===")
low = [r for r in rows[40:] if int(r.get("opaque_refs_cpu_vis") or 0) < 250]
hi = [r for r in rows[40:] if int(r.get("opaque_refs_cpu_vis") or 0) >= 450]
def med(xs):
    xs=sorted(xs); return xs[len(xs)//2] if xs else None
for name, group in [("low", low), ("hi", hi)]:
    print(
        name,
        "n",
        len(group),
        "uh",
        med([int(r.get("chunk_meshed_unlit_hidden") or 0) for r in group]),
        "nm",
        med([int(r.get("column_loaded_no_mesh_n") or 0) for r in group]),
        "unf",
        med([int(r.get("unfinished_visual") or 0) for r in group]),
        "fm",
        med([int(r.get("dirty_fm_n") or 0) for r in group]),
        "rem",
        med([int(r.get("dirty_remesh_n") or 0) for r in group]),
        "pgpu",
        med([int(r.get("pending_gpu_applies_n") or 0) for r in group]),
        "cnr",
        med([int(r.get("chunk_not_ready") or 0) for r in group]),
    )

# delta uh+nm vs delta opaque on big drops
print("\n=== drop accounting: d_opaque vs d_uh + d_nm ===")
for i in range(1, len(rows)):
    a = int(rows[i - 1].get("opaque_refs_cpu_vis") or 0)
    b = int(rows[i].get("opaque_refs_cpu_vis") or 0)
    if a - b < 80:
        continue
    duh = int(rows[i].get("chunk_meshed_unlit_hidden") or 0) - int(
        rows[i - 1].get("chunk_meshed_unlit_hidden") or 0
    )
    dnm = int(rows[i].get("column_loaded_no_mesh_n") or 0) - int(
        rows[i - 1].get("column_loaded_no_mesh_n") or 0
    )
    dcnr = int(rows[i].get("chunk_not_ready") or 0) - int(
        rows[i - 1].get("chunk_not_ready") or 0
    )
    print(
        f"{i-1}->{i} dop={b-a} duh={duh} dnm={dnm} dcnr={dcnr} "
        f"sum_hideish={duh+dnm}"
    )
