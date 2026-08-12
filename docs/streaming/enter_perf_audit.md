# Enter-load performance audit (Era44–Era45)

Manual reference runs: World_174.

| Run | Log | Symptom |
|-----|-----|---------|
| Pre-Era44 | `20260812-153650` | gpu_warmup ~120s, mesh abort forced InGame with holes |
| Era44 variant A | `20260812-163342` | coop PrepareView >8 min, plateau fifo≈6–13, gpu≈15–26, mesh_dirty=1 |
| Era45 target | autoload `--timeout-sec 300` | phase=done ≤3 min, mesh_dirty/gpu/fifo→0 at settle |

## Timeline (163342 — R4 livelock baseline)

| Time | Phase | Key metrics |
|------|-------|-------------|
| 16:34:07 | coop mesh_warmup | 511 dirty → mesh build |
| 16:34:10 | prepare_view frame 0 | BeginEnterLitGate, fifo=81 |
| 16:34:12+ | coop PrepareView drain | no phase=done 8+ min |
| plateau | stuck | mesh_dirty=1, gpu=15–26, fifo≈6–13, debt=0, ring=0 |

## R4 root cause (confirmed)

**R4** — ownership livelock: Relight → MarkRelit → RequestRemeshAfterApply → GPU commit → MarkDirty → rebuild → new GPU pending → repeat.

Primary enter multiplier: Path C (`SuppressRelightSeamDirty` + idle + `ShouldRemeshAfterApplyOnlyOnIdleDrawable`).

Evidence from 163342:
- `debt=0` while fifo>0 (lit snapshot vs mesh-ready semantic gap — R6)
- `suppress_relight_seam=1` during enter idle
- `mesh_dirty=1` + `gpu_pending>0` with status wrongly showing «Lighting queue»

## Era44b fixes (UX + abort wall)

1. Shared `BuildEnterWarmupStatus` — mesh/gpu blockers priority over fifo.
2. Coop PrepareView `coop_abort_drain` after `enter_mesh_abort_ms` (gate stays, no force done).
3. `MaybeLogHeartbeat` in coop path; `EnterWarmupCombinedDebt` weights mesh_dirty.

## Era45 R4 fixes (ownership)

1. **B1 diagnostics:** `GetRemeshAfterApplyCount`, `FindFirstDirtyInHorizontalRadius`, `mark_relit_raa_total`, `suppress_relight_seam` in jsonl/heartbeat.
2. **B2:** `ClassifyRemeshAfterLitApply` — skip dirty/raa, PreferKick when gpu pending, skip inflight; applied to all MarkRelit RAA paths.
3. **B3:** RAA commit coalesce — skip MarkDirty if already dirty (GPU + CPU defer + CPU normal).
4. **B4:** MarkDirty/MarkDirtyPriority guard — no double RAA insert.
5. **B5:** `ShouldSuppressRelightSeamDirtyForEnterGate` — disable suppress until spawn ring ready.

## Redundancy hypotheses (updated)

| ID | Verdict | Notes |
|----|---------|-------|
| R1 | Likely | Coop relight + gate FIFO duplicate |
| R2 | Confirmed | 6× TickMeshEmerge per frame |
| R3 | Partial | DrainEnterGameMeshWarmup + gate overlap |
| R4 | **Confirmed + fixed** | RAA↔MarkDirty loop; Path C on enter |
| R5 | Inconclusive | Partial band finalize=false |
| R6 | Observed | snapshot debt=0 masks mesh churn |
| R7 | Suspected | GPU drain starved under gate |

## Verification

```text
cd bin
.\Cubatarium.exe --console --autoload-last-world --visible --timeout-sec 300
```

Success criteria (World_174):

| Metric | Before (163342) | After target |
|--------|-----------------|--------------|
| coop PrepareView wall | >8 min | ≤3 min to phase=done |
| mesh_dirty at settle | 1 | 0 |
| gpu_pending at settle | 15–26 | 0 |
| fifo/inflight at settle | plateau 6/10 | 0 |
| remesh_after_apply_n | >0 plateau | 0 at ring ready |
| status when gpu>0 | «Lighting queue» | «Building terrain…» |

Unit: `miss_first_mesh_class_test` — `ClassifyRemeshAfterLitApply`, `EnterWarmupStatusPrefersMeshOverFifo`.
