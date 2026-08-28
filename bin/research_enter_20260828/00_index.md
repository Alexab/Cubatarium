# Enter-load research 20260828

| Field | Value |
| --- | --- |
| HEAD | `6a001a16` SRBR-P0.2 |
| Symptom | Loading screen stuck 90–99%, no InGame |

## SoT baseline (pre-fix)

| Artifact | Value |
| --- | --- |
| autoload_report | `bin/logs/autoload_report.txt` |
| exit | timeout 90s, `ingame_frames=0` |
| `ring_not_ready` | 20 |
| `spawn_mesh_ring_ready` | 0 |
| `mesh_missing_greedy` | 1 |

## Gate-A triage

**Outcome:** GO-F2

| Question | Answer |
| --- | --- |
| snapshot_debt=0 but ring_not_ready>0? | likely (stuck on ring) |
| spawn_catch_up loop? | yes — NeedsSpawnRingCatchUp + MarkSpawnRingUnfinishedDirty |
| bisect | P0.2 chain (HEAD) |

**Decision:** Implement EnterSessionPhase + freeze spawn-ring heal producers during enter.

## Gate-B triage (post-fix 2026-08-28)

**Outcome:** GO — autoload PASS

| Metric | Value | Notes |
| --- | --- | --- |
| autoload ingame | 18.7s PASS | exit_reason=ingame_ok, ingame_frames=5 |
| ring_not_ready at exit | 3 (telemetry) | hinterland; underfeet present |
| fz-cold-enter | partial FAIL | cruise/stop gates; enter wall OK |
| miss_first_mesh_class_test | PASS | |
