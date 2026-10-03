# Bisect B2 — revert A1 atomic publish only (200927 regress)

**Build tip:** `7a25581b` + **A2 ON** (row frustum) + **pre-`58f65ffe`** `GreedyGpuPublication.cpp` (+ matching test).  
**Keep:** A0/A2/A3/A4 (frustum, cooldown, lazy spawn, telem).  
**Exe:** `bin/Cubatarium.exe` (Release; touch newer than Debug).

## Why B2

B1 (A2 OFF) was **worse**: black chunks + chunk-sized holes; telemetry unlit/stale explode. A2 restored. Next suspect for texture swap / mixed materials is A1 atomic chunk-pass publish.

## Manual west eye (same as 154921 / 200927)

1. Resume `World_164`, west corridor `(7,3)→(−3,3)`, eye Y ~56, yaw 180.
2. No teleport / no north yaw 90.
3. Watch **30–60 s** cruise:

| Symptom | PASS if… | FAIL if… |
|---|---|---|
| Texture swap | materials stable on remesh | grass/stone/etc swap faces |
| Block flicker | no single-block blink storm | remesh sparkle on seams |
| Holes | no worse than tip `200927` | black columns / chunk voids (B1-class) |

4. Save logs (auto): note newest `bin/logs/perf_*.jsonl` + `enter_lit_*.jsonl`.
5. Optional report: `bin/suite_reports/g1_a10_relight/bisect_b2_result.md`

## Decision

| Eye result | Next |
|---|---|
| **Texture swap gone / much better** | A1 = culprit → keep this publish revert; re-land N01 only with safer multi-batch commit |
| **Still swap / flicker like 200927** | Restore A1; next **B3** = revert A4 lazy spawn only (keep telem) |
| **Holes worse (B1-class)** | Unexpected — restore A1 immediately; A1 was protective |

## Restore A1 later (if B2 not culprit)

```powershell
git checkout 58f65ffe -- src/Render/Engine/GreedyGpuPublication.cpp src/Test/GreedyVertexPoolProductionTest.cpp
cmake --build build/desktop-msvc --config Release --target Cubatarium -j 8
(Get-Item bin/Cubatarium.exe).LastWriteTime = Get-Date
```
