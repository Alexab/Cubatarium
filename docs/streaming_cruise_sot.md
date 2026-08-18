# Streaming cruise SoT (perf_opt10 closeout)

Single source of truth for inland cruise throughput after scaffolding
`3640bf46` and closeout phases A–G. EraN / `cruise_sot_refactor_*` plan
files are historical. LOD mesh and ocean Era30 are **follow-ups**, not
claimed done here.

## Pipeline

```
Ticket(level by Chebyshev ring)
  → ColumnRecord.desired (+ emerge dual-write)
  → WorkPoolBudget (gen | light | first_mesh | remesh≥1 | gpu)
  → execute (async, one inflight_job) → emerge bump / RenderReady settle
  → draw via IsChunkSliceRenderReady (NOT ColumnEmergeState::RenderReady)
  → raycast via QueryBlock (Unloaded ≠ AIR)
```

## SOTA invariants

| Invariant | Rule |
|-----------|------|
| Draw ≠ Ready | Opaque gate = `IsChunkSliceRenderReady` / `draw_ok`. Never gate draw on `RenderReady`. |
| GpuPacked draw SoT | `GreedyCache.GpuResident` counts only with live `GpuMeshPipeline::HasGpuMesh`; stale flags reconcile + CPU fallback / `MarkDirty`. Skip/rate-limit uses validated refs, not list size. |
| Same-frame sample | `FocusRingVisualSample.frame_epoch` must match `StreamingFrameEpoch`; else recount. |
| Remesh reservation | When RemeshQ ≠ ∅, `remesh_schedule ≥ 1` (pool slot, not `*FloorMs`). |
| Lit remesh class | Drawable lit remesh → RemeshQ (`MarkDirty`); missing/FullyDark → FirstMeshQ (`MarkDirtyPriority`). SoftDefer-hidden neighbor seam remesh uses `MarkDirty`. |
| PreferKick | Cancel-stale GPU promote only (`PreferKickPendingGpuQueued`); not a parallel remesh owner. |
| Floors | `MissEmergeFloorMs`, `LandMovingRelightDrainFloor`, `AsyncScheduleFloorUnderMiss` = 0; use pools. |
| Underfeet lease | `CancelInFlightOutsideHorizontalRadius` / `CancelAsyncInFlightKeepDirty` never drop Active/PendingGpu for Chebyshev `horiz≤1`. GPU backlog finishes underfeet first. |
| SoftDefer callbacks | `SetDeferMeshUntilLitFn` / On* installed once; per-frame POD `SoftDeferFramePolicy` on Coordinator. |
| Publication | One SoT: lit drawable or keep-prior GPU. **Reject Unlit near «ради дыр»** (Era28/29). `AllowUnlitFirstMesh` only hinterland (`horiz >` LitDrawable ring). Hide FullyDark in ring until lit / true-dark. |
| ColumnFlow miss+pending | `DeriveColumnDesiredStage`: **PendingLight owns** the column → `RelightThenMesh` before FirstMesh. FirstMesh only after light debt clears. |
| FirstMesh prune | `FirstMeshPruneKeepHoriz` = LitDrawable ring (drop hinterland FM only; never nh≤2 shell). |

## ColumnRecord

- Store: `UColumnRecordStore` on `UWorld` (`ColumnRecord.h`).
- Dual-written from `SetColumnEmergeState` / ColumnFlow `AdvanceColumn`.
- Fields: emerge, desired, revs, inflight_job, pending_light, sticky, light_complete_disk, raa_pending.
- `GetColumnEmergeState` reads **map first** (bump SoT); record is mirror.

## Tickets & pools

- `ColumnTicketMap.h`: `TicketLevelForRing`, `DesiredStageFromTicket`.
- Rings: `kVisualStageNearFovHoriz` (2) / `kVisualStageLitDrawableHoriz` (4).
- `HoleDrainPools`: steal remesh→FM but **keep 1 remesh slot** when RemeshQ ≠ ∅.

## Hot-path prep (closeout A/B + SoftDefer split)

- `mesh_emerge_prep_unfinished_ms` is a **legacy sum** — prefer split fields.
- Split telem: `prep_pending_light_ms`, `prep_black_sticky_ms`, `prep_dirty_count_ms`, `prep_softdefer_setup_ms` (POD update / Set* once).
- SoftDefer empty (after setup): `softdefer_empty_scan_ms` (full focus disk), `softdefer_empty_own_ms` (seam + ownership).
- SoftDefer empty scan: full `heal_r=focus_radius` every frame (rim-slice reverted — caused hide/show flicker).
- `prep_softdefer_setup_ms`: POD update + Set*Fn once only. `IsCreateSpawnWarmupSettled` is **outside** this bucket and latches after first true (`CreateSpawnWarmupSettledLatched`).
- Streaming writes `FocusRingVisualSample`; Coordinator reuses when epoch matches.

## Draw / heal telem (do not confuse)

| Metric | Meaning |
|--------|---------|
| `opaque_cmd_on` | MDI batches that passed slice-ready + cull |
| `opaque_gpu_packed_n` | GpuPacked opaque refs **drawn** this frame (live slot, not list size) |
| `opaque_draw_n` | **Draw SoT** = `opaque_cmd_on` + `opaque_gpu_packed_n` |
| `visible_black_focus_n` | Drawable FullyDark/StaleDark in focus, **even if hide-until-lit hid them** (**heal SoT**) |
| `column_render_ready_n` | FSM `RenderReady` count only — **not** draw proxy |
| `column_lighting_n` / `column_meshing_n` | Other emerge stages |
| `underfeet_reason` | `ColumnRenderableState::BlockReason`: 0 None, 1 PendingLight, 2 StickyRemesh, 3 StaleDark, 4 MissingMesh, 5 GpuInFlight, **6 NotLoaded**, 7 NotReadyState |

Hide-until-lit (`ShouldHideUncomputedFullyDarkInRing`) stays for quality (far ring). Underfeet remains presentable (full-column band + GpuPacked/PendingGpu; lease retains GPU until replacement).

## Dirty-class call sites (Closeout RemeshQ)

| Site | Lit drawable | Missing / FullyDark |
|------|--------------|---------------------|
| Q2 hole neighbor seam | `MarkDirty` | (neighbors only when drawable) |
| SoftDefer-hidden face seam | `MarkDirty` | — |
| RemeshColumnSeamTicket / SyncIdle VB | `MarkDirty` / Seamed | Priority / SeamedPriority |
| Relight `priority_mesh` / RAA commit | `MarkDirty` | `MarkDirtyPriority` |
| SoftDefer empty ownership | no re-Mark if sticky Owned / already Dirty | FirstMesh enqueue if `!Contains` |

## Contracts

- `BlockQueryResult`: Unloaded ≠ AIR (`ChunkManager::QueryBlock`).
- Soft XZ border: `WorldBorderPolicy` before hard 1e5 sanitize.

## Forbidden

- Unlit FirstMesh / FullyDark preview in LitDrawable ring «ради дыр» (Era28/29).
- New PreferKick exception owners / orphan PreferKick loops.
- New `*FloorMs` knobs (use pool redistribution).
- Wall-gated enqueue for FirstMesh desire.
- Gating opaque on `ColumnEmergeState::RenderReady`.
- Expanding enter SoftDefer lift beyond spawn **r≤2**.

## SLA inland cruise (−485/50, restore −7752/96/808)

| Metric | Target (vs 203518 / fail 102747 / 145451 / enter-fix cruise 184340) |
|--------|------------------------------------------|
| wall_ms med / p90 | ≤130 / ≤220 |
| prep hot (sum prep_*) med | ≤20 |
| softdefer_empty_scan_ms | diagnose vs prep_softdefer_setup (setup should be ≪ scan) |
| schedule_ok under holes | ≥8 (proxy) |
| dirty_remesh when lit dirty / seam | >0 |
| fifo_drop as heal | not primary |
| opaque_cmd_on / opaque_draw_n | not <200 at Δfocus≤6; late cz≥55 med ≥200 (prefer `opaque_draw_n`) |
| underfeet_missing% | ≤15 |
| visible_black_focus med | ≤25 orient |
| sky-only regress | raa_commit>0 on FullyDark enter; opaque not stuck |

### A/B closeout (perf_opt10 RemeshQ opaque fix)

Fresh inland **`perf_20260817-100319`** (−485/50→−484/55, enter `enter_lit_20260817-100351`):

| Metric | 100319 | 201626 (pre-fix) | 184340 (baseline) | Verdict |
|--------|--------|------------------|-------------------|---------|
| prep hot med | **0.0016** | 0.002 | 54.8 | **done** |
| dirty_remesh med | **36** | 0 | 0 | **done** (RemeshQ truth) |
| schedule_ok med | **8** | 8 | 5 | **done** |
| opaque_draw med | **727** | collapse | n/a | **done** early/mid |
| late cz≥55 opaque_draw | 47 | 226→50 | 10 | **tail** (fog+load+remesh cap) |
| underfeet_missing% | 32 | 45 | 42 | improved; tail NotLoaded/stream |
| visible_black med | 59 | 55 | 69 | tail (heal SoT, cruise sample/4) |
| miss/holes frame% | ~92 | ~93 | ~91 | frontier hole proxy (holes=1) |
| wall med / p90 | 182 / 295 | 191 / 297 | 233 / 397 | improved vs 184340 |

**Post-100319 tail fixes (same branch):** deep RemeshQ (`≥32`) → `remesh_schedule` 2–3 + backpressure cap 2; `keep_h=2` prune; underfeet NotLoaded→NotReadyState while column streaming; `underfeet_need` excludes incomplete camera column; load_ops not clamped during terrain load; `underfeet_has_mesh` includes GpuPacked/pending.

A/B: `python bin/audit_cruise_sot.py perf_20260817-100319_6148.jsonl perf_20260816-184340_26004.jsonl perf_20260816-201626_4992.jsonl`

Red inland after Unlit-near rollback: `docs/streaming/cruise_column_gen_diag.md` (`195810`). After RelightThenMesh P0–P4: `docs/streaming/cruise_215411_diag.md` (`215411`). After cruise time-budget A–D: `docs/streaming/cruise_083708_diag.md` (`083708`, wall 138/167; moving cz=51–54 already 104/121). Diagnose Relight/FM/residency — do not add a second draw SoT. Cost-aware complete/apply path: `docs/streaming/relight_then_mesh_industrial.md`.
