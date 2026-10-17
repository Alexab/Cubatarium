#!/usr/bin/env python3
"""Manual enter stick audit for latest 170330 flight."""
from __future__ import annotations
import json
import re
from pathlib import Path
from statistics import median

PERF = Path("bin/logs/perf_20260906-170330_13836.jsonl")
INFO = Path("bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260906-170328.13836")
ENTER = Path("bin/logs/enter_lit_20260906-170343.jsonl")

SETTLE = re.compile(r"settle_reason=(\S+).*?(?:elapsed_ms=([\d.]+))?.*?(?:visibility_debt=(\d+))?.*?(?:combined_debt=(\d+))?")
HB = re.compile(
    r"\[EnterWarmup\] heartbeat elapsed_ms=([\d.]+).*?debt=(\d+).*?"
    r"mesh_missing=(\d).*?gpu_pending=(\d+).*?ring=(\d).*?"
    r"(?:visibility_debt=(\d+))?"
)

def load_periods(p: Path):
    rows = []
    for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            if r.get("kind") == "period":
                rows.append(r)
    return rows

def main():
    text = INFO.read_text(encoding="utf-8", errors="replace")
    print("=== INFO settles ===")
    for m in re.finditer(r".*settle_reason=.*", text):
        print(m.group(0)[:300])
    print("\n=== EnterWarmup soft/abort ===")
    for pat in ("soft_force", "soft_clean", "abort_underfeet", "live_blockers", "force_ingame", "PrepareView"):
        n = text.count(pat)
        if n:
            print(f"  {pat}: {n}")

    hbs = []
    for m in re.finditer(r".*\[EnterWarmup\] heartbeat.*", text):
        line = m.group(0)
        hbs.append(line)
    print(f"\n=== EnterWarmup heartbeats n={len(hbs)} ===")
    if hbs:
        print("  first:", hbs[0][:280])
        print("  mid:  ", hbs[len(hbs)//2][:280])
        print("  last: ", hbs[-1][:280])
        # parse elapsed
        el = []
        for line in hbs:
            mm = re.search(r"elapsed_ms=([\d.]+)", line)
            if mm:
                el.append(float(mm.group(1)))
        if el:
            print(f"  elapsed range {el[0]:.0f}..{el[-1]:.0f} ms ({el[-1]/1000:.1f}s)")

    # enter_lit jsonl
    if ENTER.exists():
        erows = []
        for line in ENTER.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("{"):
                erows.append(json.loads(line))
        print(f"\n=== enter_lit n={len(erows)} ===")
        if erows:
            keys = ["elapsed_ms", "snapshot_debt", "visibility_debt", "ring_not_ready",
                    "mesh_missing_greedy", "mesh_gpu_pending_near", "fifo_n",
                    "underfeet_present", "phase"]
            for i in (0, len(erows)//2, -1):
                r = erows[i]
                bits = {k: r.get(k) for k in keys if k in r or True}
                # print interesting
                print(f"  [{i}] elapsed={r.get('elapsed_ms')} vis_debt={r.get('visibility_debt')} "
                      f"snap={r.get('snapshot_debt')} ring={r.get('ring_not_ready')} "
                      f"miss={r.get('mesh_missing_greedy')} gpu={r.get('mesh_gpu_pending_near')} "
                      f"fifo={r.get('fifo_n')} uf={r.get('underfeet_present')} phase={r.get('phase')}")
            # last 5 with debt
            print("  last5:")
            for r in erows[-5:]:
                print(f"    t={r.get('elapsed_ms')} vis={r.get('visibility_debt')} snap={r.get('snapshot_debt')} "
                      f"ring={r.get('ring_not_ready')} miss={r.get('mesh_missing_greedy')} "
                      f"gpu={r.get('mesh_gpu_pending_near')}")

    periods = load_periods(PERF)
    print(f"\n=== perf periods n={len(periods)} ===")
    if not periods:
        print("  NO PERIODS — stuck in Loading / never InGame")
        # spikes?
        spikes = 0
        for line in PERF.read_text(encoding="utf-8", errors="replace").splitlines():
            if '"kind": "spike"' in line or '"kind":"spike"' in line:
                spikes += 1
        print(f"  spikes={spikes}")
        return
    print(f"  first wall={periods[0].get('wall_ms')} unfinished={periods[0].get('unfinished_visual')} "
          f"miss={periods[0].get('focus_missing_mesh')} unsync={periods[0].get('pool_unsync_uploads')}")
    print(f"  last  wall={periods[-1].get('wall_ms')} unfinished={periods[-1].get('unfinished_visual')} "
          f"miss={periods[-1].get('focus_missing_mesh')}")

if __name__ == "__main__":
    main()
