#!/usr/bin/env python3
from pathlib import Path
import re, json

INFO = Path("bin/logs/Cubatarium.exe.TIMLENOVO.Bakhshiev.log.INFO.20260906-170328.13836")
text = INFO.read_text(encoding="utf-8", errors="replace")

print("=== key enter lines ===")
for pat in (
    r"settle_reason=",
    r"soft_force",
    r"soft_clean",
    r"abort_drain",
    r"abort_underfeet",
    r"GpuWarmup",
    r"ring_ready=",
    r"visibility_debt=",
    r"InGame",
    r"PrepareView",
    r"mesh_missing=1",
    r"EndEnterLitGate",
    r"EnterSession",
):
    pass

# All EnterWarmup / LitRing / settle related
for line in text.splitlines():
    if any(k in line for k in (
        "settle_reason", "soft_force", "soft_clean", "abort_drain",
        "abort_underfeet", "force_ingame", "EnterWarmup]",
        "LitRing", "NeedsEnterGameMeshWarmup", "spawn ring",
        "GpuWarmup", "EnterSession", "EndEnterLitGate",
        "ring_blocker",
    )):
        # shorten
        s = line
        if len(s) > 320:
            s = s[:320] + "..."
        print(s)

print("\n=== heartbeat debt/miss/ring timeline ===")
for line in text.splitlines():
    if "[EnterWarmup] heartbeat" in line:
        m = re.search(
            r"elapsed_ms=([\d.]+).*?debt=(\d+).*?mesh_missing=(\d).*?gpu_pending=(\d+).*?ring=(\d).*?ring_blocker=(\S+)",
            line,
        )
        if m:
            print(f"  t={float(m.group(1)):8.0f} debt={m.group(2):>3} miss={m.group(3)} gpu={m.group(4)} ring={m.group(5)} blocker={m.group(6)}")
        else:
            m2 = re.search(r"elapsed_ms=([\d.]+).*?debt=(\d+).*?mesh_missing=(\d).*?gpu_pending=(\d+).*?ring=(\d)", line)
            if m2:
                print(f"  t={float(m2.group(1)):8.0f} debt={m2.group(2):>3} miss={m2.group(3)} gpu={m2.group(4)} ring={m2.group(5)}")

# file time span
print("\n=== log time span ===")
times = re.findall(r"I20260906 (\d+:\d+:\d+\.\d+)", text)
if times:
    print(f"  first={times[0]} last={times[-1]} lines={len(text.splitlines())}")

# enter_lit full dump keys
ENTER = Path("bin/logs/enter_lit_20260906-170343.jsonl")
rows = [json.loads(l) for l in ENTER.read_text(encoding="utf-8", errors="replace").splitlines() if l.startswith("{")]
print(f"\n=== enter_lit keys sample ===")
if rows:
    print(sorted(rows[-1].keys()))
    for r in rows:
        print({k: r.get(k) for k in sorted(r.keys()) if k in (
            "elapsed_ms","visibility_debt","snapshot_debt","ring_not_ready",
            "mesh_missing_greedy","mesh_gpu_pending_near","fifo_n","mesh_dirty",
            "mesh_async_pending","underfeet_present","fov_debt","phase","reason"
        ) or "debt" in k or "ready" in k or "phase" in k})
