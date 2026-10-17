# 01 — Ownership map: streaming + render control signals

Дата: 2026-08-26 · HEAD `1f3f7675` · агент ArchMapper

## 1. Таблица ownership

| Сигнал | Кто пишет | Кто читает | Единственный authority? | Конфликт? |
|--------|-----------|------------|-------------------------|-----------|
| **focus_missing_mesh / miss witness** | `UWorldStreaming::RefreshStreamingPressure` → telemetry + `SetPromoteRelightHold` / FIFO pin. Параллельно `TickMeshEmerge` локальный `nearest_missing_hole`. | Admission, SoftDefer Capture pin, Relight FIFO, ColumnFlow, fog/stream | Частично (telem SoT = Refresh) | **Да** — local hole vs telem miss cy/horiz |
| **unfinished_visual / no_mesh** | `RefreshStreamingPressure` → `CountUnfinishedVisualNear` (+ cruise memo cd=8). `no_mesh_n` в emerge = alias UV (P12 A2) | Admission steal FM, Capture floors, PL trim gates | Да для telem count | **Да** — stale UV memo; alias ≠ true no-drawable count |
| **visible_black_focus(_raw)** | `RefreshStreamingPressure` → CountVisibleBlack; hysteresis → published; P17 stand cadence | RelightFifo consume, ColumnFlow, Admission stand heal, Cache blink | Да (published) | Слабый: ColumnFlow cached sample vs live |
| **Dirty FirstMesh vs Remesh** | FM: `MarkDirtyPriority`; Remesh: `MarkDirty`; promote/steal в admission | Dual-Q schedule в ChunkMeshCache, caps | Класс очереди — DirtySet; writers — много | **Да** — skip_already_dirty vs ForceFirstMesh; steal Remesh→FM |
| **SoftDefer capture pin** | `WorldStreaming` SoftDeferCapturePin* (retarget / land frontier / enter spawn) | Capture floor Enqueue, telem pin_age | Да — UWorldStreaming fields | **Да** с miss witness / SoftDeferEmptyStuck |
| **Relight FIFO trim/protect** | Trim: TickMeshEmerge → `TrimFarRelightFifoFarthest`. Pin: Refresh / Persistence | Protect policy, Capture boosts, admission backpressure | Trim = Persistence; pin = Persistence (+ ColumnFlow hold mirror) | **Да** — farthest vs protect_horiz / pin |
| **PendingLight trim** | Note: TryNote*/OnLitPendingNeeded. Trim: emerge + MemoryBudget gated `ShouldTrimPendingLightUnderHoles` | SoftDefer until-lit, ColumnFlow, admission PL | Set = UWorld PendingLightBeforeMesh | **Да** — trim banned under holes+UV |
| **MarkRelit schedule / skip_already_dirty** | Planner `RelightInstallPlanner` → ExecuteLitApplyPlan | DirtyQ, ColumnFlow, SoftDefer leave-in | Planner per-column; Dirty = Cache | **Да** — SkipAlreadyDirty + ForceFirstMesh on hole; P17 hold |
| **GPU publish / FreeChunk** | ChunkMeshCache apply → GpuPipeline FreeChunk; SoftDefer may avoid Free | Draw/HasDrawable, flicker policies | GPU residency = pipeline; when Free = Cache policy | **Да** — keep-prior-GPU vs FreeChunk-while-live |
| **MeshWorkAdmission mode** | Только `TickMeshEmerge` Compute + Finalize → `SetMeshWorkAdmission` | Cache admit/requeue/starve/GPU budgets | **Да** | Слабый: early vs finalize caps |
| **SoftDeferHeld / empty ownership** | Cache HoldSoftDefer*; Coordinator SoftDeferEmptyOwned/Age | DeferMeshUntilLit, publish avoid, admission requeue | Held=Cache; ownership=Coordinator | **Да** — Held без ticket vs ownership scan |

## 2. Hidden state

| Состояние | Где |
|-----------|-----|
| unfinished_sample_cd / last_unfinished_* | WorldStreaming::RefreshStreamingPressure |
| UnfinishedVisualCache | World::CountUnfinishedVisualNear |
| MissingMemo | ChunkMeshCache::HasMissingGreedyMeshInHorizontalRadius |
| vb_focus_stable_frames / published hysteresis | RefreshStreamingPressure (P17 cadence) |
| SoftDeferCapturePin* | WorldStreaming |
| MissWitnessAgeFrames / MissStuck* | ChunkEmergeCoordinator |
| SoftDeferEmptyAgeFrames / Owned / ScanOffset | Coordinator |
| SoftDeferRimScanCd / async_relief_cooldown | TickMeshEmerge statics |
| SoftDeferEmptyAvoidFrames | ChunkMeshCache |
| RelightFifoPin* | WorldPersistence |
| promote_hold_* | ColumnFlowExecutor |

## 3. Callbacks World ↔ MeshCache (`TickMeshEmerge`)

```
SetDeferMeshUntilLitFn     → SoftDeferMeshUntilLitPolicy
SetOnLitPendingNeededFn    → TryNotePendingLight + Enqueue RelightThenMesh
SetOnSoftDeferHeldFn       → HoldSoftDefer + Enqueue FirstMesh
SetOnLitDrawableCommittedFn→ ClearSticky / RenderReady
SetOnMeshColumnDirtyFn     → NoteUnfinishedColumnDirty
SetColumnFlowContainsFn    → HasRepairTicket honesty
```

## 4. Who can Forget / discard inflight (P17)

- **ForgetInflight:** `MarkDirtyPriority` supersede path — EnterLitQuiesce soft_undrawn OR (!hold). P17: `miss_undrawn=true` → hold supersede (anti-discard).
- **Epoch DiscardedLate:** DrainAll mismatched epoch.
- **CancelInFlightOutsideHorizontalRadius:** async relief / MemoryBudget — may leave orphan Active until Drain.
- **GPU:** DropQueuedPendingGpuApply / PreferKick / FreeChunk (not Forget).

Rule: FOV FirstMesh miss + live Inflight → hold, not Forget.

## 5. SoT frame order

1. RefreshStreamingPressure (miss/UV/VB)
2. TickAsync Capture (pin / floor)
3. TickMeshEmerge (admission / SoftDefer empty / schedule)
4. MarkRelit Apply
5. Cache publish / FreeChunk
