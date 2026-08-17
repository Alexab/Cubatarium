---
name: Cruise RemeshQ opaque fix
overview: "Исходный cruise SoT plan закрыл prep/SoftDefer setup и underfeet Cancel-lease, но DoD Phase 2–3 на пролёте 201626 всё ещё красные: RemeshQ≈0, miss≈93%, opaque collapse. Следующая арка — Dirty-class truth (Q2/VB→MarkDirty) + честный opaque/underfeet SoT, без floors/PreferKick."
todos:
  - id: pa-remeshq
    content: "Phase A: Q2/VB/RAA drawable → MarkDirty; StarveRemesh не опустошает RemeshQ; dirty_remesh>0 + miss≤47%"
    status: completed
  - id: pb-softdefer-own
    content: "Phase B: SoftDefer empty ownership без ежекадрового MarkDirtyPriority storm; FM flood↓"
    status: completed
  - id: pc-opaque-uf
    content: "Phase C: opaque GpuPacked telem SoT + underfeet NotLoaded(6) vs packed/GPU; late opaque≥200"
    status: completed
  - id: pd-ab-docs
    content: "Phase D: docs + fresh inland A/B vs 184340/201626"
    status: completed
isProject: false
---

# План: хвосты cruise SoT после hotfix SoftDefer (пост-201626)

## Хвосты исходного плана vs факт

| Исходная фаза | Код | DoD на `201626` | Статус |
|---|---|---|---|
| 0 Diagnosis | audit + `softdefer_empty_scan/own` | setup mystery закрыта | **done** |
| 1 SoftDefer prep | POD + Set* once; rim-slice **откатили** (flicker); latch `CreateSpawnWarmupSettled` | prep hot **0.002** (цель ≤20) | **done** |
| 2 Dirty class / RemeshQ | SoftDefer seam → `MarkDirty` only | `dirty_remesh` **0**, miss **93%**, `schedule_ok` **8** | **хвост** |
| 3 Underfeet lease | CancelOutside/KeepDirty `keep_horiz≤1` | late opaque med **226**, uf miss **45%**, reason **6** sticky | **хвост** |
| 4 Docs/A/B | docs + audit | opaque vs `184340` **не зелёный** | **хвост** |

**Hotfix подтверждён на `201626`:** `prep_softdefer_setup` **0.0003**, emerge **52**, wall **191/297**. Картинка/дыры — нет.

```mermaid
flowchart TD
  prepOk["prep SoftDefer DONE 0ms"]
  q2Prio["Q2 hole neighbors MarkDirtyPriority"]
  vbSeam["VB RemeshSeam SeamedPriority"]
  remeshEmpty["RemeshQ approx 0"]
  fmFlood["dirty_fm 250 schedule_ok 8"]
  missSticky["miss 93 percent"]
  unfinished["column_loaded_no_mesh unfinished alias"]
  opaqueDrop["opaque_cmd 415 to 50"]
  ufNotLoaded["underfeet reason 6 NotLoaded"]
  prepOk --> q2Prio
  q2Prio --> remeshEmpty
  vbSeam --> remeshEmpty
  remeshEmpty --> fmFlood
  fmFlood --> missSticky
  missSticky --> unfinished
  unfinished --> opaqueDrop
  opaqueDrop --> ufNotLoaded
```

## Поправки к диагнозу (после 201626)

1. **`underfeet_reason=6` = `NotLoaded`**, не GpuInFlight (5=`GpuInFlight`, 6=`NotLoaded`, 7=`NotReadyState`).
2. SoftDefer seam `MarkDirty` уже есть — мало: другие пути промоутят Remesh→FirstMesh.
3. `column_loaded_no_mesh` = алиас unfinished (`FocusNotRenderReady`).
4. `opaque_cmd_on` = MDI CPU refs; GpuPacked рисуется отдельно — коллапс 415→53 при `disc=0` может быть telem + реальный unfinished.

## Фазы

### Phase A — RemeshQ truth (хвост Phase 2)

**Цель:** vb>25 → `dirty_remesh` med>0; miss% ≤47%.

1. Q2 (~L2757 в Coordinator): drawable hole neighbor → **`MarkDirty`**.
2. RemeshSeam / VB heal для drawable lit → `MarkDirty` / non-Priority seamed; Priority только missing/FullyDark.
3. RAA lit drawable → `MarkDirty` (Closeout C).
4. StarveRemesh: не вычищать lit RemeshQ внутри keep horiz.
5. Тесты dual-Q + опционально `dirty_promote_fm_n` telem.

### Phase B — SoftDefer empty ownership storm

**Цель:** не заливать FirstMeshQ сверх прогресса.

1. Не re-`MarkDirtyPriority` каждый кадр на sticky Owned.
2. Не дублировать ColumnFlow FirstMesh если Contains.
3. Full-disk scan оставить (~0.27 ms OK).

### Phase C — Opaque / underfeet SoT

**Цель:** late opaque≥200; uf miss≤15.

1. Telem: `opaque_gpu_packed_n` / `opaque_draw_n` = MDI + packed.
2. Underfeet NotLoaded(6): учесть GpuPacked/PendingGpu в `GetColumnRenderableState`.
3. Retain underfeet drawable until replacement (без PreferKick).

### Phase D — A/B lock

1. Обновить `docs/streaming_cruise_sot.md` (reason enum, packed opaque, call sites).
2. Fresh inland enter (−485/50), не продолжение (−492,55).
3. `bin/audit_cruise_sot.py` vs `184340` / `201626` / новый лог.

## Порядок

A → B → C → D.

## Вне скоупа

Ocean Era30 / LOD; abort walls; enter SoftDefer lift > r≤2; повторный rim-slice SoftDefer empty.
