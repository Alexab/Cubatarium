# Bisect B4 — restore A1 atomic publish (keep B3 eager)

**Build:** tip + A2 ON + **A1 ON** (`58f65ffe` publish) + **eager** spawn ring (B3).  
**Exe:** `bin/Cubatarium.exe` Release; touch newer than Debug.

## Why

B3: blinks gone; water blacks at end remain (focus `(7,3)`, `dark_id=377`). Isolate whether water black is worsened by A1 OFF (partial publish) vs zone/lighting debt.

## Manual west eye

Prefer same direction as tip/B2: `(7,3)→(−3,3)` so water/start vs end comparable. Also note water near `(7,3)`.

| Symptom | Watch |
|---|---|
| Texture swap | returns? (A1 ON was tip FAIL mode) |
| Chunk blink | still gone? (eager still on) |
| Water blacks @ `(7,3)` | better / same / worse vs B3 |

## Decision

| Eye | Next |
|---|---|
| Water better, swap returns | A1 protects water but causes swap → re-land N01 with safer staging |
| Water same, swap returns | water ≠ A1; restore A1 OFF; dig FillWater/377 separately |
| Water same, no swap | keep A1 ON; dig water; A1 not tip swap sole cause |
| Blink returns | unexpected — check eager still present |

## Restore A1 OFF later

```powershell
git checkout 58f65ffe^ -- src/Render/Engine/GreedyGpuPublication.cpp src/Test/GreedyVertexPoolProductionTest.cpp
```
