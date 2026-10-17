# N01 v2.1 R4 note

## Done in v2.1

- **ring_dirty:** spawn-ring cache invalidated when `NeedsSpawnRingCatchUp()` (see `ChunkEmergeCoordinator.cpp`).

## Deferred (per plan: only if R1–R2 eye still shows age reset)

- **Per-key debt age:** `AdvanceOldestDebtAgeFrames` remains class-aggregate in `WorldStreaming.cpp`; `TouchDebtProgress` is per-column on published identity change only. Revisit if mid stalled stays high after operator eye on v2.1 build.
