#!/usr/bin/env python3
import json
from collections import Counter
from pathlib import Path

path = Path(r"E:/Work/Home/Cubatarium/bin/logs/perf_20260821-123613_28584.jsonl")
rows = [json.loads(l) for l in path.read_text(encoding="utf-8", errors="replace").splitlines() if l.strip().startswith("{")]
spikes = [r for r in rows if r.get("kind") == "spike"]

# When PL>40, how often apply/capture zero?
hi = [r for r in spikes if int(r.get("pending_light_focus") or 0) > 40]
print(f"spikes PL>40: {len(hi)}/{len(spikes)}")
an0 = sum(1 for r in hi if int(r.get("relight_apply_n") or 0) == 0)
an1 = sum(1 for r in hi if int(r.get("relight_apply_n") or 0) == 1)
an2 = sum(1 for r in hi if int(r.get("relight_apply_n") or 0) >= 2)
print(f"  apply_n==0: {an0}  ==1: {an1}  >=2: {an2}")
cap0 = sum(1 for r in hi if float(r.get("relight_capture_ms") or 0) < 0.05)
print(f"  capture_ms~0: {cap0}")
fifo = [int(r.get("relight_fifo_n") or -1) for r in hi]
print(f"  fifo_n med={sorted(fifo)[len(fifo)//2]} max={max(fifo)} sample={fifo[:8]}")

# apply_n hist overall
print("apply_n hist", dict(sorted(Counter(int(r.get("relight_apply_n") or 0) for r in spikes).items())))
print("apply_final/partial hist",
      Counter(int(r.get("relight_apply_final_n") or 0) for r in spikes),
      Counter(int(r.get("relight_apply_partial_n") or 0) for r in spikes))

# correlate uf flip with reason
prev = None
flip_reasons = Counter()
for r in spikes:
    uf = int(r.get("underfeet_opaque_present") or 0)
    if prev is not None and uf != prev:
        flip_reasons[int(r.get("underfeet_reason") or -1)] += 1
    prev = uf
print("uf flip -> reason", dict(sorted(flip_reasons.items())))

# emerge gpu vs wall share when PL high
em = [float(r.get("mesh_emerge_ms") or 0) for r in hi]
gpu = [float(r.get("mesh_dirty_gpu_ms") or 0) for r in hi]
st = [float(r.get("stream_ms") or 0) for r in hi]
print(f"PL>40 emerge med={sorted(em)[len(em)//2]:.1f} gpu med={sorted(gpu)[len(gpu)//2]:.1f} stream med={sorted(st)[len(st)//2]:.1f}")

# look for dark_face / soft_defer fields
sample = spikes[30]
keys = sorted(k for k in sample if any(x in k for x in ("dark", "soft", "defer", "unlit", "fifo", "capture", "apply", "pending", "emerge", "dirty")))
print("relevant keys:", keys)

for k in ("soft_defer_empty_n", "softdefer_empty_n", "soft_defer_held_n", "dark_face_near_n",
          "dark_face_stale_n", "relight_capture_full_n", "relight_capture_finalize",
          "relight_fifo_n", "relight_completed_n", "mesh_async_n", "pending_async"):
    if any(k in r for r in spikes):
        vals = [r.get(k) for r in hi if k in r]
        print(k, "med", sorted(vals)[len(vals)//2], "max", max(vals))
