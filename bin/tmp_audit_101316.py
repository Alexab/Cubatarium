#!/usr/bin/env python3
"""Forensic audit for 101316 steady segment."""
import json
import statistics as st
from collections import Counter
from pathlib import Path

p = "bin/logs/perf_20260824-101316_32932.jsonl"
rows = [json.loads(l) for l in Path(p).read_text(encoding="utf-8").splitlines()
        if l.strip() and json.loads(l).get("kind") == "spike"]
steady = rows[60:]

print(f"total_spikes={len(rows)}  steady={len(steady)}  dur~{len(rows)*2}s")

# apply_n distribution by late steady
late = steady[40:]
early = steady[:40]
print(f"\n--- early steady ({len(early)} frames) ---")
c = Counter(int(r.get("relight_apply_n", 0)) for r in early)
print("  apply_n:", dict(sorted(c.items())))
bc = Counter(int(r.get("apply_binding", -1)) for r in early if r.get("apply_binding") is not None)
print("  binding:", dict(sorted(bc.items())))
lu = [r.get("relight_apply_light_ms", 0) / max(1, r.get("relight_apply_n", 1)) for r in early if r.get("relight_apply_n", 0) > 0]
print(f"  light_unit med={st.median(lu):.2f}  p90={sorted(lu)[int(len(lu)*0.9)]:.2f}")

print(f"\n--- late steady ({len(late)} frames) ---")
c2 = Counter(int(r.get("relight_apply_n", 0)) for r in late)
print("  apply_n:", dict(sorted(c2.items())))
bc2 = Counter(int(r.get("apply_binding", -1)) for r in late if r.get("apply_binding") is not None)
print("  binding:", dict(sorted(bc2.items())))
lu2 = [r.get("relight_apply_light_ms", 0) / max(1, r.get("relight_apply_n", 1)) for r in late if r.get("relight_apply_n", 0) > 0]
if lu2:
    print(f"  light_unit med={st.median(lu2):.2f}  p90={sorted(lu2)[int(len(lu2)*0.9)]:.2f}")

# breakdown by binding
print("\n--- binding breakdown (steady) ---")
for b, name in [(0, "QueueEmpty"), (1, "TimeSlice"), (2, "CountCap"), (3, "FatUnit")]:
    sub = [r for r in steady if int(r.get("apply_binding", -1)) == b]
    if not sub:
        continue
    an = [r.get("relight_apply_n", 0) for r in sub]
    lums = [r.get("relight_apply_light_ms", 0) / max(1, r.get("relight_apply_n", 1))
            for r in sub if r.get("relight_apply_n", 0) > 0]
    lu_med = f"{st.median(lums):.2f}" if lums else "n/a"
    ams = [r.get("relight_apply_ms", 0) for r in sub]
    print(f"  {name}({b}): count={len(sub)}  apply_n_med={st.median(an):.1f}  "
          f"apply_ms_med={st.median(ams):.1f}  light_unit_med={lu_med}")

# TimeSlice+n=1 forensic
ts1 = [r for r in steady if int(r.get("apply_binding", -1)) == 1 and int(r.get("relight_apply_n", 0)) == 1]
print(f"\n--- TimeSlice n=1 ({len(ts1)} frames) ---")
for k in ["relight_apply_ms", "relight_apply_light_ms", "relight_fifo_n", "pending_light", "world_streaming_phase_ms"]:
    xs = [r.get(k) for r in ts1 if r.get(k) is not None]
    if xs:
        print(f"  {k}: med={st.median(xs):.2f}  p90={sorted(xs)[int(len(xs)*0.9)]:.2f}")

# When apply_n>=3 achieved — what was unit?
ge3 = [r for r in steady if int(r.get("relight_apply_n", 0)) >= 3]
print(f"\n--- apply_n>=3 ({len(ge3)} frames) ---")
if ge3:
    lu3 = [r.get("relight_apply_light_ms", 0) / max(1, r.get("relight_apply_n", 1)) for r in ge3]
    print(f"  light_unit med={st.median(lu3):.2f}  p90={sorted(lu3)[int(len(lu3)*0.9)]:.2f}")
    print(f"  apply_ms med={st.median([r.get('relight_apply_ms',0) for r in ge3]):.2f}")

# opaque recovery
opq = [r.get("opaque_refs_cpu_vis", 0) for r in steady if r.get("opaque_refs_cpu_vis")]
print(f"\nopaque_refs: med={st.median(opq):.0f}  p90={sorted(opq)[int(len(opq)*0.9)]:.0f}  max={max(opq):.0f}")

# pending_light improvement
pl = [r.get("pending_light", 0) for r in steady]
print(f"pending_light: med={st.median(pl):.0f}  p90={sorted(pl)[int(len(pl)*0.9)]:.0f}")

# unit_apply_light_ms check (gate FAIL at 6.5)
lall = [r.get("relight_apply_light_ms", 0) / max(1, r.get("relight_apply_n", 1))
        for r in steady if r.get("relight_apply_n", 0) > 0]
print(f"light_unit all steady: med={st.median(lall):.2f}  p90={sorted(lall)[int(len(lall)*0.9)]:.2f}  max={max(lall):.2f}")
