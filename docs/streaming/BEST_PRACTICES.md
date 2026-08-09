# Best Practices Comparison

Full engine perf audit (all subsystems, Phase A–F roadmap, baseline evidence):
[`../PERF_AUDIT_ENGINE.md`](../PERF_AUDIT_ENGINE.md).

## Главный Инвариант

В mature voxel engines mesh sampling и light propagation синхронизированы так,
чтобы рендер видел только итоговое состояние света. Preview без готового света
либо не рисуется вовсе, либо рисуется специальным нейтральным placeholder.

Для Cubatarium это означает: `GreedyCache` сам по себе не должен считаться
достаточным критерием «визуально готово».

## Сравнение

| Практика | Industry | Cubatarium | Gap |
|----------|----------|------------|-----|
| Light before first visible mesh | Minecraft-like clients, Veloren | ранее разрешался preview mesh при `PendingLight` | критический |
| Skylight source seeding from top / neighbor context | Starlight, Minecraft lighting optimizations | relight обычно стартовал после commit как отдельный шаг | высокий |
| Do not propagate into not-yet-ready chunks | Starlight | trail columns могли долго жить в backlog | высокий |
| Dirty-only rebuild with bounded budget | common chunk managers | есть, но смешивался с starvation heuristics | средний |
| Separate visibility contract from render contract | typical production engines | `visual_holes` vs unfinished/black sticky; gap закрывается V2a | критический |
| Single owner for column lifecycle | explicit job graphs | ownership размазан (Admit/Recover/Refresh/Drain) — V2b | высокий |
| Explicit memory budgets + fill% telemetry | UE streaming pools, vertex pools | throughput-caps есть; byte-budget / fill% — Era 12 / `MEMORY_BUDGET.md` | высокий |
| Bounded result queues (drop-oldest + requeue) | job graphs with backpressure | Completed mesh/relight grow-only → rings | высокий |
| Grow-only GPU buffers with Reserve/Max | common GPU upload pools | `GreedyVertexPool` grow-only; Reserve/Max — Era 12 | средний |

## Gap После V2–V5 migration (arch/streaming-v2-v4, 2026-07-28)

Architecture landed (R0–R7): SeedDecision fail→PendingLight, ColumnFlowExecutor,
FOV/keep visual SLA (not terrain PendingLight gate), lighting seed factory
Cpu≠Gpu, idle Capture progress, CollectAll removed. Evidence: `edge_R1`/`R2`/`R3`
`run_outcome=success`.

Era13 architecture DoD (026–030): **done** — Hide⇒Ticket via ColumnFlow
Contains, AllowUnlitFirstMesh SoT SoftDefer, FocusPressure≠hole, Capture floor
on UnfinishedVisual, FocusIngress Stage SLA. Remaining gate DoD (not architecture):

- ARCH_D3 `wall_ms_med≤30` (lit remesh clamp; evidence autofly).
- F2/C/CB residuals on edge; TD-ARCH-011 blue_screen; TD-ARCH-015 store contract
  landed (worker Capture still off).

Research alignment: Luanti/Minetest chunk job ownership, UE streaming memory
budgets, Qt RHI capability backends, 0fps-style lighting-before-mesh — mapped to
V3 seed, V4 executor, V5 visual SLA, E4 factory (see TD-ARCH closed table).

## Gap После Era 11 (historical)

- Preview mesh при `PendingLight` всё ещё возможен (R5).
- Draw не гейтится `RenderReady`.
- R15–R18 подтверждены логами `perf_165208…181020`; откат к `2cb85f3c` обязателен перед V2.

## Memory (Era 12)

Industry: UE-style memory budgets, vertex pooling, toroidal/chunk pools, overflow
policies (drop reproducible work; never drop sole world state). Cubatarium: см.
[`MEMORY_BUDGET.md`](MEMORY_BUDGET.md) — Soft/Budget Mb, Completed rings,
Dirty/Pending soft-caps, GPU Reserve/Max, `MemoryBudgetController`, chunk free-list.

Правило overflow: drop oldest/farthest **только** если результат можно
пересоздать (remesh/relight); gen/IO — block admit; cold PendingLight — не erase.

## Что Совпадает С Industry

- Async mesh rebuild с validation по revision.
- Budgeted scheduler для rebuild и IO.
- 3x3 remesh на колонных границах.
- Отдельный perf harness для flight/load regression.

## Что Не Совпадало

### 1. Preview Mesh До Готового Света

Это главная причина «чёрной земли без дыр».

С точки зрения пользователя проблема выглядит как «мир не достроен», но в
телеметрии долгое время это не считалось hole, потому что mesh действительно
существовал.

### 2. Свет Как Отдельный Долг После Commit

Если relight запускается уже после того, как колонка попала в visual ring,
система начинает жить в режиме долга:

- колонка уже нужна игроку;
- колонка уже может претендовать на draw;
- итоговый light state ещё не готов.

### 3. Несколько Recovery-Путей На Одну И Ту Же Колонку

В industry-практиках чаще используется единая job ownership model. В
Cubatarium recover/admit/promote paths постепенно эволюционировали отдельно,
из-за чего стало сложнее предсказать судьбу конкретной колонки.

## Источники

- [0fps: Meshing in a Minecraft Game](https://0fps.net/2012/06/30/meshing-in-minecraft/)
- [PaperMC Starlight Technical Details](https://github.com/PaperMC/Starlight/blob/fabric/TECHNICAL_DETAILS.md)
- [Let's Make a Voxel Engine: Chunk Management](https://sites.google.com/site/letsmakeavoxelengine/home/chunk-management)
- [Gamedev StackExchange: voxel lighting with occlusion](https://gamedev.stackexchange.com/questions/19207/how-can-i-implement-voxel-based-lighting-with-occlusion-in-a-minecraft-style-gam)

## Gap После manual_1752 / Era 13 (2026-07-29)

| Практика | Industry | Cubatarium после Era13 closeout | Gap |
|----------|----------|--------------------------------|-----|
| Hide ⇒ guaranteed repair ticket | Job graph + TTL/requeue | ColumnFlow Contains + sticky enqueue TickDerived | **closed** TD-ARCH-026 |
| Throughput floor when FOV unfinished | Raise async/apply floor | AllowUnlitFirstMesh + schedule floors | **closed** TD-ARCH-027 |
| Single ColumnRenderable SoT | One stage flag | GetColumnRenderableState + FocusPressure split | **closed** TD-ARCH-028/030 |
| FirstMesh queue ≠ Remesh thrash | Separate priorities | FirstMesh tickets + lit remesh clamp | **closed** TD-ARCH-029 |
| SoftDefer with Capture floor | Light debt must progress | Capture on UV\|missing; FocusIngress Stage SLA | **closed** TD-ARCH-030 |
| Frontier rim first-mesh latency | Stage SLA | FocusIngress unfinished/stale-dark | **partial** TD-ARCH-033 |
| GPU mesher end-to-end | Resident GPU mesh | Hybrid extract + packed path; cost ≠ readiness | средний (cost track) |

## Gap После Era14 (2026-08, manual_151212)

Architecture DoD Era13 ≠ gate DoD. New gap: **Frame contract** — streaming/mesh
must not nest inside locomotion (`RunLegacyPhysicsFrame`); heal admission must
not require calm `last_frame_ms`. See
[`ROOT_CAUSE_2026-08.md`](ROOT_CAUSE_2026-08.md),
[`ERA14_POSTMORTEM.md`](ERA14_POSTMORTEM.md).

| Практика | Industry | Cubatarium Era14 | Gap |
|----------|----------|------------------|-----|
| Main thread bookkeeping + budgeted apply | CryEngine / streaming ch.23 | stream+emerge inside DoMovement | **critical** TD-040 |
| Never starve FOV FirstMesh on dirty wall | N rebuilds/frame + async floor | Imm/stale enqueue wall-gated | **critical** TD-041 |
| Derived chunk DesiredStage | voxel job graph | stand/cruise Imm forks | **high** TD-042 |
| Land FOV validation | scenario matrix | ocean cruise misses tops | **high** TD-043 |

## Gap После Era14.1 residual (2026-08-07)

| Практика | Industry | Cubatarium Era14.1 | Gap |
|----------|----------|--------------------|-----|
| High-priority FOV FirstMesh quota (MC dual-queue) | PreferKick nearest tops every miss frame | **landed** | TD-043 holes≤0.10 still open |
| SoftDefer held escape under miss | requeue floor≥1 | **landed** | — |
| Time-sliced streaming phase budget (UE) | 24ms + miss carve-out | **landed** | ocean wall≤30 open TD-048 |
| Period phase telem avg | avg like wall | **landed** | — |
| Cross-platform frame contract | Android TickWorldStreamingPhase | **landed** | — |
| IDLE sticky remesh budget | calm sticky≥2 drains | **IDLE_CLEAN GO** | — |

## Gap После Era15 plan (2026-08-08)

Architecture-first residual after Era14.1. Knobs (`sticky_r`, PreferKick-only, SoftDeferHeld
force-N) are not DoD — SoT closes the gap.

| Практика | Industry | Cubatarium pre-Era15 | Era15 SoT |
|----------|----------|----------------------|-----------|
| Keep old GPU mesh until new upload ready | minecraft-renderer pendingReplace; VoxelMan commit; Transvoxel hole-free | GPU packed: staging+`BindCommittedSlot` OK; CPU Apply/Immediate `FreeChunk` before write | **P1** MeshResidency TD-049 |
| Placeholder Unlit + guaranteed lit remesh | light-before **or** stable Unlit then lit | `AllowUnlitFirstMesh` + remesh-on-lit gated by `sticky_r≤1` idle | **P2a** UnlitPublished→LitPending→LitReady; sticky_r≠gate TD-050 |
| Single owner / Hide⇒ticket in job graph | continuous `should_mesh`; ColumnFlow Contains | SoftDeferHeld parallel zoo outside Flow | **P2b** SoftDeferHeld→ColumnFlow ticket TD-050 |
| FirstMesh until Drawable (dual-queue) | MC HP rebuild; FirstMesh≠Remesh | PreferKick Queued-only; Held/prune compete cy0 | **P3 landed partial** PreferKick Kicked; TD-051/043 holes residual |
| Strict light-before-first-draw entire FOV | mature engines often hide unlit | AllowUnlitFirstMesh (TD-027 hole SLA) | **keep** — Unlit is temporary stage, not final RenderReady |

**Era15 closeout (2026-08-08):** TD-049/050 **done**; TD-051/043 **partial** (`era15_p3b_land` holes≈0.24 miss_stuck=8 sticky=0 wall≈45). FLY_CLEAN + IDLE_CLEAN + IDLE_WARM GO. Ocean ARCH_D3 / TD-048 still open. TD-046 Capture deferred.

## Gap После Era20 plan (2026-08-08) — autofly GO; TD-056 **partial** (manual eye)

| Практика | Industry | Cubatarium | Era20 SoT |
|----------|----------|------------|-----------|
| HP FirstMesh must progress under miss | MC dual-queue | tops class cy≤1; Imm off wall>50; async=0 sticky | **done** miss class cy≤3/mh≤4 + cold-async Imm |
| Hide/not-draw empty until ready | Hide⇒Ticket | SoftDefer empty HasGreedy∧!Drawable flicker | **done** keep-prior FreeChunk; SoftDefer empty !ready |
| Enter time-sliced | UE spawn limits | RD+1 ring + drain×8 → app_update≈2s | **done** r≤2 gate; enter_app≈100; SpawnRingCatchUp |
| Autofly ≠ visual merge | PREMERGE | Era19 autofly GO; manual `214034` holes/black | autofly GO; **manual `214034` still required** |

**Evidence:** `era20_p3_fly` FLY; `era20_p3_idle` IDLE_CLEAN; `era20_p3_land3`
ARCH_D3_LAND; `era20_p3_warm` IDLE_WARM. Baseline `214034` not visual-merge.

**Baseline:** `perf_20260808-214034` / `manual_214034_analyze.json`. Keep
FrameStreamingBudget. Reject Imm primary; Era18 VB Capture storm; autofly-only close.

## Gap После Era21 plan (2026-08-09) — autofly GO; TD-057 **partial** (manual eye)

| Практика | Industry | Cubatarium | Era21 SoT |
|----------|----------|------------|-----------|
| Keep old GPU until new upload ready | BindCommitted / pendingReplace | GPU path OK; CPU remesh FreeChunk | **done** defer FreeChunk until Bind |
| HP FirstMesh not blocked by LP tickets | dual-queue | Relight ticket skips SoftDefer Capture under miss | **done** FirstMesh-only Capture gate |
| Satisfying SoT for empty SoftDefer | hide until ready | RecoverUnlit HasGreedy treats empty as mesh | **done** Satisfying/Drawable |
| Autofly ≠ visual merge | PREMERGE | Era20 autofly GO; manual `102236` flicker/FOV | autofly GO; **manual `102236` still required** |

**Evidence:** `era21_p3_fly` FLY; `era21_p12_idle` IDLE_CLEAN; `era21_p3_warm`
IDLE_WARM; `era21_p12_land9` ARCH_D3_LAND. Baseline `102236` not visual-merge.

**Baseline:** `perf_20260809-102236` / `manual_102236_analyze.json`.

## Gap После Era19 plan (2026-08-08) — autofly CLOSED; **manual superseded by 214034**

| Практика | Industry | Cubatarium | Era19 SoT |
|----------|----------|------------|-----------|
| Per-frame time budget mesh/stream | Cubyz `maximumMeshTime`; UE cell/spawn limits | Era18 count `max` floors without ms SoT | **done** `FrameStreamingBudget` + kill-switches |
| Dual-queue HP FirstMesh vs LP heal | MC highPriorityQuota | VB Capture/bg compete under miss/hitch | **done** miss-first — **under-healed** rim on `214034` |
| Column light→mesh stage exclusivity | Unity/Burst stages | dual Remesh+Relight | **done** P2 PendingLight owns column |
| Autofly ≠ visual merge | PREMERGE | Era18 closed on autofly while `191229` FPS collapse | autofly GO; manual `214034` holes↑ no_ticket↑ → TD-056 |

**Evidence:** `era19_p3_fly` FLY_CLEAN GO; `era19_p3_warm` IDLE_WARM GO;
`era19_p3b_idle`/`era19_p1c_idle` IDLE_CLEAN GO; `era19_p1_land2` ARCH_D3_LAND GO.
Manual `214034` = new SoT (not visual merge).

## Gap После Era18 plan (2026-08-08) — **regressed FPS/miss** (light-debt soft OK)

| Практика | Industry | Cubatarium | Era18 SoT |
|----------|----------|------------|-----------|
| Void/black ⇒ focus PendingLight gate | light-before-lit-draw | RecoverUnlit FIFO+MarkDirty without Note | **done** P1 NotePendingLight TD-054 |
| Drain/capture while VisibleBlack | never starve FOV light | floors keyed only on pending_light_focus | **done** P1–P2 VB floors — **caused** heal-on-hot (`191229`) |
| Manual land-exit / stand-in-black SoT | autofly ≠ eye | autofly GO, manual `165953` pre-fix | **regressed** wall/holes/miss on `191229` → TD-055 |

**Baseline:** `perf_20260808-165953` → post-fix `191229`. Autofly GO ≠ merge.
Light-debt soft fails fixed; FPS collapse + holes↑ — do not claim CLOSED.

## Gap После Era17 plan (2026-08-08) — partial (superseded light-debt)

| Практика | Industry | Cubatarium | Era17 SoT |
|----------|----------|------------|-----------|
| Ticket = work in flight not live-window | Hide⇒Ticket | Contains ∨ Dirty/Inflight/PendingLight | **done** P0 TD-053 |
| Heal until predicate false | continuous should_mesh | derive while VB>0; void RelightThenMesh | **partial** ticket without focus PendingLight |
| FirstMesh priority class under miss tops | MC dual-queue | remesh_schedule=0 when miss cy≤1 | **done** P2 (exit miss residual TD-054) |

**Era17 closeout:** IDLE_CLEAN + ARCH_D3_LAND GO (`era17_p1_idle`, `era17_p2_land`);
manual `144227`/`165953` class **open** via TD-054 (autofly ≠ visual merge).

## Gap После Era16 plan (2026-08-08) — superseded by Era17

| Практика | Industry | Cubatarium | Era16/17 SoT |
|----------|----------|------------|--------------|
| Visibility debt = column SoT not sticky-set | job graph truth | `black_sticky` ⊆ StickyRemeshAfterLight | **done** VisibleBlackFocusN / NoTicketN TD-052 |
| Hide/degraded ⇒ real repair ticket | Hide⇒Ticket | Era16 arm/live-window + Remesh noop | **Era17 P0** Contains-only + Progress/Stalled TD-053 |
| Continuous should_mesh for dark drawable | derive until predicate false | nearest-ring ticket / NoTicket DoD | **Era17 P1** heal while VB>0 |

**Era16 closeout:** P3 matrix GO — `era16_p3_fly` / `idle` / `warm` / `land`;
ocean ARCH_D3 soft residual TD-048 (`era16_p3_ocean` wall≈226, no_ticket=0).
**Manual residual `144227`/`165953`:** open via Era18 TD-054 (ticket≠focus light debt).

## Практический Вывод Для Cubatarium

Наиболее полезные заимствования:

1. Строгий `RenderReady` / `ColumnRenderable` контракт.
2. Commit-time skylight seed для загруженного соседнего ring.
3. Отдельная метрика `unfinished_visual` как источник правды для gates.
4. Постепенное схлопывание watchdog zoo в единый column scheduler.
5. **Hide⇒RepairTicket** — никогда не прятать геометрию без job в ColumnFlow.
6. **Async throughput floor** при unfinished FOV; cap только Immediate/sync.
7. **FirstMesh ≠ Remesh** в dirty admission.
