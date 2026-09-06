# Policy strip A/B (perf-root P3 / Phase5 S4 / Phase5.1)

## Flag

- Runtime: `URuntimeTuning::StreamSimple` (env `CUBA_STREAM_SIMPLE=1` or `streaming_tune.json` `"stream_simple": true`)
- Effect: disables diet/cadence throttles in `RefreshStreamingPressure`; disables SoftDefer witness retarget blocking; RefreshProbe state is explicit on `UWorldStreaming`

## Phase 5.1 kill-switches

| Flag | Default | Env / JSON | Effect |
|---|---|---|---|
| `StreamingPhaseBudgetMs` | **5.0** | `CUBA_STREAMING_PHASE_BUDGET_MS` / `streaming_phase_budget_ms` | Hard wall for TickWorldStreamingPhase SoT; enter/lit auto-floor **24** |
| `MissReservedMs` | **2.0** | `miss_reserved_ms` | Miss carve inside phase (clamped ≤ phase at apply) |
| `MissEmergeFloorMs` | **2.0** | `miss_emerge_floor_ms` | Floor emerge when stream overruns general |
| `ScheduleShedUv1` | **true** | `CUBA_SCHEDULE_SHED_UV1=0` to disable | UV=1 no longer forever-blocks schedule shed |

## Phase 5.2.0 enter settle

| Mechanism | Default | Effect |
|---|---|---|
| Drop NeedsEnterGameMeshWarmup epoch memo | always | Loading/PrepareView re-samples blockers every tick (epoch only advances InGame) |
| `ShouldForceEnterLoadSoftCleanDebt` | 12s soft wall | `combined_debt==0 && underfeet` → settle (load-world path only) |
| `settle_reason=` INFO | one-shot | `live_blockers\|soft_clean\|soft_force\|abort_underfeet\|lit_stall` |
| `EnterForceInGameMs` / `EnterMeshAbortMs` | 150s / 120s | last-resort unchanged |

## Phase 5.2.1 opaque attribution flags (FPM JSONL)

| Key | Meaning |
|---|---|
| `scene_opaque_refresh_ms` | `RefreshPassRefs` opaque |
| `scene_opaque_cull_ms` | `ApplyGpuCompactCull` |
| `scene_opaque_gpu_draw_ms` | `DrawGreedyGpuBatches` |
| `scene_opaque_packed_ms` | leftovers packed draw (near r≤4) |
| `scene_opaque_cross_ms` | `DrawCrossInstancedBatches` |

## Phase 5.3 empty-drip FPM keys

| Key | Meaning |
|---|---|
| `empty_backlog_n` | max(unfinished, colnm, chunk_not_ready) |
| `phase_abort_heavy` | StreamingPhaseBudget overrun latch |
| `skip_empty_emerge` | TickMeshEmerge skipped this frame |
| `abort_schedule_final` / `abort_drain_final` | schedule/drain after AbortDripCap |
| INFO `empty_batch_event` | rise+20 then drop−20 within 2s |

## How to A/B

```powershell
python tools/simple_ab_suite.py
```

## Status 2026-09-05 (Phase5 S4 complete)

### land-stand hang

- **Root cause:** enter-warmup stuck `mesh_missing` without underfeet after coop abort-drain.
- **Fix:** `ShouldForceEnterLoadSoftExit` (150s + fov_debt==0).
- **Verify:** `hang_killed=false`, periods>0 (`phase5_landstand_verify.json`, SimpleAB suites).

### Phase5.1 enter hang (ring already ready)

- **Root cause:** AbortDrain only arms on `!ring_ready`; soft-exit never fired; `EndEnterLitGate` cleared quiesce → `IsCreateSpawnWarmupSettled` O(FOV) every cruise frame (~45ms `prep_warmup`).
- **Fix:** soft-exit without AbortDrain; latch settled on gate end / post-session.
- **Verify:** trio v4 `hang_killed=false`.

### Phase 5.2.0 PrepareView@100% stick

- **Root cause:** `NeedsEnterGameMeshWarmup` memo keyed on frozen `StreamingFrameEpoch` during Loading.
- **Fix:** drop memo early-return; soft_clean@12s; `settle_reason=` telem.
- **Verify:** no-teleport fz-cold-enter + land-stand (harness A).

### Phase 5.3 empty-batch / SoftDefer plateau

- **Root cause:** Phase 5.2 abort/`skip_empty_emerge` starved rim FirstMesh → unfinished/colnm 64–85 → HoleDrain burst EMPTY packs; SoftDefer stuck_horiz sticky telem.
- **Fix:** AbortDripCap + CarveUpTo2 + forbid skip on backlog + SoftDefer escape + HoleDrain FM drip≤4 + OpaqueCullSkipStable (camera-safe).
- **Verify:** `phase53_flyheavy` empty_backlog med~2 / stuck_horiz frac0 / empty_batch_event0 / hang=false; hard gates still red.

### SimpleAB 2026-09-05

| Suite | report | land-cruise wall | land-stand wall | fz-cold-enter wall | hang |
|---|---|---:|---:|---:|---|
| SimpleAB-off | `20260905-011150_suite_summary.json` | 37.8 | 37.0 | 62.4 | false |
| SimpleAB-on | `20260905-012222_suite_summary.json` | **126** | 58 | 68 | false |

**Verdict:** StreamSimple=ON uniquely worsens land-cruise wall (~3×). Diet/cadence / SoftDefer heuristics **KEEP** — do **not** delete Policy headers. Full suite still `pass=false` due to promoted FPS hard gates (expected until wall≤16.6).

## Heuristic → gate → cost table

| Heuristic | Gate it holds | Cost (ms, FPM 010702) | Keep? |
|---|---|---:|---|
| diet_cruise_cadence / AntiFlicker | wall_ms_fly / stream | ~0.01 | **KEEP** (AB: ON worse) |
| unfinished sample cadence | unfinished_visual | TBD | KEEP |
| SoftDefer capture pin | holes / flicker | ~0.05 | KEEP |
| facing rim cadence | facing_ms | ~0.08 | KEEP |
| rim_witness_idle_diet | witness_latch_diet_share | TBD | KEEP |
| schedule policy (FIFO/ocean/idle) | stream / emerge | ~1.3 after shed (was 15.8) | KEEP envelope; shed under idle |

## Headers annotated with BUDGET_MS

| Header | BUDGET_MS | Source |
|---|---:|---|
| SoftDeferEmptyPolicy.h | 0.05 | softdefer_empty + prep_softdefer_pre |
| SoftDeferFramePolicy.h | 0.01 | prep_softdefer_policy_ms |
| RelightFifoPolicy.h | 15.8 | historical schedule envelope (post-shed ~1.3) |
| OceanCruisePolicy.h | 15.8 | shares schedule envelope |
| IdleRecoveryPolicy.h | 15.8 | shares schedule envelope |
| AntiFlickerPolicy.h | 0.01 | diet flags |

Remaining `*Policy.h` keep `0.0` stubs. Gate `policy_headers_have_budget` green.

## Deleted headers

**None** — SimpleAB-on does not uniquely attribute a hard gate fail that justifies deletion; OFF is better on wall.
