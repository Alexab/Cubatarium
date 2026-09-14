---
name: N01 rework mid-black
overview: "Сверка с аудитом 2026-09-14 и WT: A2/A3 KEEP; N01 v2 = audit group-commit (не double-buffer default); S2 measure-first N04; ring cache; N03 после eye. Autofly: --visible + dual-lane stop-line."
todos:
  - id: s0-interim-docs
    content: "S0: interim baseline docs; footer reopen N01; N02/N06 KEEP; N04/N03/N05 OPEN"
    status: pending
  - id: s0b-autofly-protocol
    content: "S0b: --visible warn/strict; adequacy≠merge; dual_lane_stop_line_* in report"
    status: pending
  - id: s1-n01-v2
    content: "S1: N01 audit group-commit + no dirty/revision on partial; tests + west manual --visible"
    status: pending
  - id: s2a-n04-metrics
    content: "S2a: metric split stale_vl_rev vs fully_dark_census (no policy change yet)"
    status: pending
  - id: s2b-n04-policy
    content: "S2b: after baseline — narrow stale demand; MarkRelit/World callers; mid stalled gate 205626"
    status: pending
  - id: s3-ring-cache
    content: "S3: spawn-ring cache off-enter (не assume true); telem prep_spawn_ring"
    status: pending
  - id: s4-n03-diet
    content: "S4: BeginFrame before UpdateStreaming (~1068); diet only after green eye"
    status: pending
isProject: false
---

# N01 rework + mid-black + честный autofly

SoT плана в репо: [`.cursor/plans/n01_rework_mid-black.plan.md`](.cursor/plans/n01_rework_mid-black.plan.md).  
Аудит: [`docs/streaming/CURRENT_STATE_AUDIT_2026-09-14.md`](docs/streaming/CURRENT_STATE_AUDIT_2026-09-14.md) (snapshot HEAD `420335e9`).  
Visual anchors: manual PASS `154921`; tip FAIL `200927`; sticky SoT `205626`; bisect B1–B4.

**Повторная сверка (2026-09-14 вечер):** план скорректирован — double-buffer убран из default; S2 разбит на measure→policy; callers/`BeginFrame` строки поправлены; N05 loci добавлены. Interim WT совпадает с описанием ниже.

---

## 0. Соответствие аудиту (кратко)

| Аудит | План | Статус |
|---|---|---|
| N01 transactional chunk+pass | S1 group-commit + no dirty/rev on partial | **align** (не side-table default) |
| N02 row frustum | KEEP (уже на tip/WT) | **done / KEEP** |
| N06 structural CooldownKey | KEEP | **done / KEEP** |
| N04 census≠StaleVL; не снимать защиты вслепую | S2a metrics → S2b policy | **align** (было опасно сжато) |
| N05 per-key debt age | S2b loci `AdvanceOldestDebtAgeFrames` / `TouchDebtProgress` | **добавлено** |
| D3.3 lazy spawn (не assume-true) | S3 cache | **align** + bisect B3 |
| N03 BeginFrame до stream | S4 (после green eye) | **align**; это кусок D5, не весь N03 |
| D0 gates / Q9 | частично A0; S0b stop-line | **частично** |
| D2 full pixel oracle | **out of scope** этого трека | честно OPEN |
| «развивать, не откатывать» | A2/A3 KEEP; A1→v2 не вечный OFF | **align** |

Порядок аудита D0→D1→D2→D3: при уже закрытых N02/N06 допустим N01→N04(measure)→ring→N03. **Не** менять remesh policy в S2 до metric baseline.

---

## 1. Почему autofly «не показал» проблемы

### 1.1 Сценарий верный — окно скрыто

Wall-diet: west `product-174657` **без** `--visible` → `InitializeHidden` / `GLFW_VISIBLE=FALSE` ([`flight_sim_run.py`](tools/flight_sim_run.py) ~398–401, ~1550). Не null-renderer, не north/teleport. Окно на экране не появлялось.

### 1.2 Merge = adequacy, не картинка

| Сигнал | Смысл | Wall-diet |
|---|---|---|
| `adequacy_pass` | FAIL-класс miss/stuck/**VB≥40** | true → «green» |
| analyzer `pass` | product gates | **false** на всех `wall_diet_*` |
| `operator_visual` | глаз | нет |
| dual-lane upper (VB/StaleVL≤84.5, unlit_max≤15c/19w) | stop-line плана | **не enforced** (a6 cold VB~89) |

Texture-swap / sticky mid **нет** в gates (`near_focus_holes≈0` и у tip FAIL).

### 1.3 Числа

| Run | VB | oracle StaleVL | unlit max |
|---|---:|---:|---:|
| a6 cold autofly | ~87–89 | ~85 | ~50 |
| tip `200927` | ~69 | ~65 | ~69 |
| B3b `205626` | ~72 | ~70 | ~5 |

---

## 2. Код vs tip / WT (факт на диске)

HEAD tip: `7a25581b`. Working tree (interim eye-safe):

| | Tip | WT | Решение |
|---|---|---|---|
| A1 `GreedyGpuPublication` | group/`group_ok` (`58f65ffe`) | **pre-A1** sequential dirty erase | S1 N01 v2 |
| A2 `Frustum.h` rows | ON | ON | KEEP |
| A3 structural `CooldownKey` | ON | ON | KEEP |
| A4 telem substages | ON | ON | KEEP |
| A4 lazy ring | ON (assume ready off-enter) | **eager Bisect B3** | S3 cache |
| N03 BeginFrame | после stream (~1068) | same | S4 |
| N04 dark→StaleVL | open | sticky mid `205626` | S2a→S2b |

**Два разных N01-дефекта (не путать):**

1. **WT / pre-A1:** любой успешный batch → `published_ok.insert(coord)` → erase dirty → mixed materials (аудит E repro).
2. **Tip A1:** группа OK только целиком, dirty erase только `published_ok`, **но** `cache.meshRevision/cull/sort` пишутся **всегда** в конце `PublishPassInputs`; B4 eye FAIL (дыры/swap/мигания) — не возвращать as-is.

---

## 3. Протокол исполнения

```powershell
python tools/flight_sim_run.py --world World_164 --scenario product-174657 `
  --visible `
  --report bin/suite_reports/g1_a10_relight/<step>_<cold|warm>.json
```

Merge = conjunction:

1. `adequacy_pass`
2. `dual_lane_stop_line_pass` (VB/StaleVL≤84.5; unlit_max≤15 cold / ≤19 warm)
3. После S2b: mid-cruise `visible_black_fully_dark_stalled` med (focus cx∈[2,5]) ≤5 (SoT from `205626` mid≈36)
4. Code-шаги publish/light/ring → **обязательный manual west eye**

Запреты: FM-first, SoftDefer-for-holes, Capture/budget raise, dual-lane order break, escalate remesh от VB alone, G1 CLOSED / pixel-oracle claim.

---

## 4. План по коду

### S0 — Interim docs

- Footer аудита: **N01 reopen** (A1 eye FAIL); N02/N06 KEEP; D3.3 lazy → cache pending; N04/N03/N05 OPEN; G1/oracle/Q9 OPEN. Не «N01 closed».
- README g1_a10_relight: tip vs interim vs `154921` / `205626` / bisect links.
- wall-diet A5 todo → deferred.
- Опционально commit WT interim (без push).

### S0b — Autofly tooling

[`tools/flight_sim_run.py`](tools/flight_sim_run.py):

1. `product-174657` без `--visible` → WARN; `CUBA_FLIGHT_REQUIRE_VISIBLE=1` → FAIL.
2. Report: `dual_lane_stop_line_pass`, `dual_lane_stop_line_fails[]`.
3. Не green при `adequacy && !stop_line`.
4. Docs: adequacy / stop-line / eye — три разных сигнала.

### S1 — N01 v2 (align audit §N01 / D1.1)

**Файлы:** [`GreedyGpuPublication.cpp`](src/Render/Engine/GreedyGpuPublication.cpp) `PublishPassInputs` (~142–259 WT); [`GreedyVertexPoolProductionTest.cpp`](src/Test/GreedyVertexPoolProductionTest.cpp).

**Алгоритм (default = текст аудита, не double-buffer):**

1. Группировать uploads по `chunkCoord` (+ pass identity как сейчас в одном `PublishPassInputs` call).
2. Stage все batches группы; при любом `!pooled` — discard staged группы, **retain predecessors**, **не** erase dirty этой coord.
3. При `group_ok` — заменить batches только этой coord; соседи независимо.
4. **Не** advance `meshRevision` / `cullRevision` / `sortRevision` / не `PendingGeometryDirty.clear()` оптом, если в вызове был partial fail **или** остался dirty для любого input coord. Advance revisions только когда все запрошенные dirty coords этого publish успешно закоммичены (или явный per-group epoch без пометки всего pass done).
5. Old pooled free только после согласованной группы и завершения читателей (существующий fence/retire path KEEP; не Free-before-draw).

**Optional spike (не default):** side-table / double-buffer draw table — только если unit+eye после (1–5) всё ещё B4-класс thrash.

**Тесты:** audit multi-batch OOM (A new/B stuck/dirty≠0/retry after capacity); neighbor OK while this fails; material remove/reorder; assert revisions **unchanged** on partial.

**Приёмка:** unit + autofly `--visible` + manual west — запрет B4 (`200927`/`205048`).

### S2a — N04 metrics first (D2.1 caution)

**Без смены remesh policy.**

- JSONL / telem split: `stale_vl_rev_n` (revision mismatch) vs `fully_dark_census_n` / keep existing stalled counters with `source=cpu_census` в docs.
- Locus export: [`WorldStreaming.cpp`](src/World/Streaming/WorldStreaming.cpp) ~1307–1338; [`DrawOracle.h`](src/World/Diagnostics/DrawOracle.h) FullyDark→StaleVL mapping (~233–329) — документировать как census proxy, не менять work в том же коммите.
- Снять baseline mid third на interim build (`205626`-class) **до** S2b.

### S2b — N04/N05 policy after baseline

Аудит: *не удалять защиты без измерения.* Near-path уже частично безопаснее:

```2110:2116:src/World/Core/World.cpp
      const bool stale_o1 =
          probe.gpu_resident
              ? (horiz_from_focus <= 2
                     // R4.5.2: near SoT ignores lone GpuHasDarkFace stickiness.
                     ? IsMeshLightStale(probe.meshed_light_rev, field_rev)
                     : IsMeshLightStaleGpu(true, probe.gpu_has_dark_face,
                                          probe.meshed_light_rev, field_rev))
```

**Callers `IsMeshLightStaleGpu` (факт):** [`MarkRelitInstall.cpp`](src/World/Core/MarkRelitInstall.cpp), [`World.cpp`](src/World/Core/World.cpp) (far branch), [`MeshLightStalePolicyTest.cpp`](src/Test/MeshLightStalePolicyTest.cpp).  
**Не** caller: `AntiFlickerPolicy.h` (свой `gpu_has_dark_face` discard ~233–257 — трогать отдельно только если evidence).

Политика (после S2a evidence):

1. Не делать глобальный «всегда игнор dark-face» первым шагом. Сузить: far/MarkRelit demand от dark-face alone → только при revision mismatch **или** явном ticket+age; mid-focus stalled обязан иметь light progress.
2. [`ColumnFlowExecutor.cpp`](src/World/Streaming/ColumnFlowExecutor.cpp) `relight_critical` (~527–531): оставить critical path, но не от голого census dark без revision/ticket age.
3. **N05 loci (обязательно в том же треке, если трогаем stall age):**
   - `AdvanceOldestDebtAgeFrames` в [`DrawOracle.h`](src/World/Diagnostics/DrawOracle.h) + использование в [`WorldStreaming.cpp`](src/World/Streaming/WorldStreaming.cpp) (~1346–1358, `GetLastGpuFinishN`)
   - [`ColumnRecordCoordinator.cpp`](src/World/Streaming/ColumnRecordCoordinator.cpp) `SyncFromWorldTruth` / `TouchDebtProgress` (~42, ~93–136)
   - Цель: age на **конкретный** key; чужой GPU finish не обнуляет oldest.
4. FillWater / `CountVisibleBlackFocusMeshes` band (~3438+) — **не** менять в S2.

**Приёмка:** mid stalled med ≤5; witness dist shrink ⇒ black clears or LegalDark; no B4 regress; unit обновляет «GPU dark face conservative» expectation только если policy действительно меняется.

### S3 — Spawn-ring cache (D3.3 + B3)

[`ChunkEmergeCoordinator.cpp`](src/World/Streaming/ChunkEmergeCoordinator.cpp) ~2109 (сейчас eager `Bisect B3`).

- Не `spawn_ring_ready=true` off-enter (A4 blink).
- Не полный `IsSpawnMeshRingReady` каждый cruise frame (~25 ms).
- Cache: invalidate on enter-edge, focus chunk change, optional ring_dirty; query when enter active (gate) or cache miss; `PrepSpawnRingQueryMs` только на real query.
- `ShouldSuppressRelightSeamDirtyForEnterGate(enter, cached_ready, base)` — семантика helper уже игнорит ring при `!enter` ([`EnterVisualWarmupPolicy.h`](src/World/Streaming/EnterVisualWarmupPolicy.h) ~1315); cache всё равно нужен если вызываем query off-enter для других читателей / будущих путей — **не** подставлять ложный true.

**Приёмка:** no B2 rare blink; `prep_spawn_ring_query_ms` cruise med ≪ 25.

### S4 — N03 slice + diet (после S1–S3 green eye)

[`WorldViewBinding.cpp`](src/World/Core/WorldViewBinding.cpp): сейчас `UpdateStreaming` **1039**, `TickAsyncChunkSystems` **1041**, `BeginFrame` **1068** — deadline **после** stream (аудит N03).  
Перенос: `BeginFrame(phase_budget)` **до** `UpdateStreaming`, тот же budget consumers. Это **не** полный N03 (floors/extra drains/FrameWorkContext) — только критичный порядок.

Diet (ex-A5): только по A4 evidence (`prep_sched_*`, `streamer_update_ms`, `async_io_ms`); dual-lane + eye stop-line.

---

## 5. Коммиты / успех

1. `docs: interim eye-safe baseline after wall-diet bisect`
2. `test: product-174657 dual-lane stop-line and visible warn`
3. `fix: N01 v2 chunk-pass publish without partial dirty/revision advance`
4. `perf: split stale_vl revision vs fully_dark census metrics`
5. `fix: narrow dark-face StaleVL demand; per-key debt age`
6. `perf: cache spawn-ring readiness when enter gate inactive`
7. позже: `fix: begin frame deadline before streaming` (+ diet)

| Gate | Pass |
|---|---|
| Manual west | Нет B4; mid blacks сходят (`205626`) |
| Autofly | `--visible` + adequacy + dual-lane stop-line |
| Unit | N01 partial OOM; stale policy tests; ring cache table |
| Docs | footer честный; D2 pixel oracle всё ещё OPEN |

---

## 6. Не делать

- Возврат `58f65ffe` as-is.
- Double-buffer publish как первый/единственный N01 fix без audit group-commit.
- Снятие `gpu_has_dark_face` stale **до** S2a baseline.
- Откат A2/A3.
- Merge по одному `adequacy_pass`.
- SoftDefer/floors/Capture raise; G1 CLOSED; claim pixel-oracle done.
