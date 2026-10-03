# Bisect B3 — revert A4 lazy spawn only (keep telem)

**Build:** tip + **A2 ON** + **A1 OFF** (as B2) + **eager** `IsSpawnMeshRingReady` every emerge tick.  
**Keep:** A4 schedule-policy telem, GPU ms wipe-on-skip, stream residual timers.  
**Exe:** `bin/Cubatarium.exe` (Release; touch newer than Debug).

## Why B3

B2 eye: partial recovery vs tip `200927` (rare blink + fading blacks). A1 implicated; residuals remain.  
Note: when enter gate is off, `ShouldSuppressRelightSeamDirtyForEnterGate` **ignores** ring ready — so A4 lazy is a **weak** cruise-semantic suspect. B3 still rules it out.

## Manual west eye (same corridor)

1. Resume `World_164`, west `(7,3)→(−3,3)`, eye Y ~56, yaw 180.
2. Watch 30–60 s cruise:

| Symptom | PASS if… | FAIL if… |
|---|---|---|
| Chunk blink | rare/absent vs B2 | same as B2 |
| Fading blacks | gone / much shorter | same fade as B2 `203535` |
| Texture swap | still absent (B2 win) | swap storm returns |

3. Note newest `bin/logs/perf_*.jsonl` + `enter_lit_*.jsonl`.

## Decision

| Eye | Next |
|---|---|
| **Clearly better than B2** | A4 lazy = residual culprit; keep eager ring; re-land D3.3 carefully |
| **Same as B2** | Restore A4 lazy; keep A1 OFF; dig fading-black as **partial-publish cost of A1 OFF** (or A3/other) |
| **Worse** | Restore A4 lazy immediately |

## Restore A4 lazy later

```powershell
git checkout b521eea0 -- src/World/Streaming/ChunkEmergeCoordinator.cpp
# or re-apply the enter_gate_active-gated ring query block
cmake --build build/desktop-msvc --config Release --target Cubatarium -j 8
(Get-Item bin/Cubatarium.exe).LastWriteTime = Get-Date
```
