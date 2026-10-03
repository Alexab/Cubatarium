# Bisect B1 — revert A2 frustum only (200927 regress)

**Build tip:** `7a25581b` + `Frustum.h` restored to pre-`4d638637` (column extraction).  
**Keep:** A0/A1/A3/A4 (publish, cooldown, lazy spawn, telem).  
**Exe:** `bin/Cubatarium.exe` (Release; touch newer than Debug).

## Manual west eye (same as 154921 / 200927)

1. Resume `World_164`, west corridor `(7,3)→(−3,3)`, eye Y ~56, yaw 180.
2. No teleport / no north yaw 90.
3. Watch **30–60 s** cruise:

| Symptom | PASS if… | FAIL if… |
|---|---|---|
| Holes | no sticky missing columns mid-corridor | gaps like 200927 |
| Texture swap | materials stable on remesh | grass/stone/etc swap faces |
| Block flicker | no single-block blink storm | remesh sparkle on seams |

4. Save logs (auto): note newest `bin/logs/perf_*.jsonl` + `enter_lit_*.jsonl`.
5. Optional report stub: `bin/suite_reports/g1_a10_relight/bisect_b1_no_a2_manual.md`

## Decision

| Eye result | Next |
|---|---|
| **Much better (holes gone)** | A2 = culprit → keep this Frustum revert; re-land N02 only with stronger guards + CPU/GPU parity before row math |
| **Still holes / texture swap** | Restore A2 Frustum; next bisect **B2** = revert A1 publish (`58f65ffe`) |
| **Holes OK, flicker remains** | Restore A2; next **B3** = revert A4 lazy spawn only (keep telem) |

## Restore A2 later (if B1 not culprit)

```powershell
git checkout 4d638637 -- src/Render/Camera/Frustum.h
cmake --build build/desktop-msvc --config Release --target Cubatarium -j 8
(Get-Item bin/Cubatarium.exe).LastWriteTime = Get-Date
```
