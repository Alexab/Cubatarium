# Manual 093849 (R7.2 A+B) vs 172957 / 131116 / 153045

Commit: `aaa56d8c` perf(stream): Phase 5.7R7.2 cull/DropRemesh diet for FPS pacing  
Perf: `bin/logs/perf_20260910-093849_26976.jsonl`  
Scorecard: `bin/suite_reports/manual_093849_scorecard.txt`

## Verdict

**NO-GO / большой product-регресс.** Wall-diet сработал (FPS↑, cull/scene↓, SoftDefer=0, spike>160↓), но **streaming throughput схлопнулся** до класса R7-bad (`schedule_ok=1`, `dirty_fm=2`). Глаз: дыры/лаг стрима, несмотря на более «лёгкий» кадр.

Hard FAIL по R7.2 ship gate: `schedule_ok≤1`. DoD `|Δfps|≤1.6` тоже не закрыт.

| Metric (cruise spd>2) | **093849 R7.2** | 172957 R7.1 | 131116 KEEP | 153045 R7-bad |
|---|---:|---:|---:|---:|
| wall med / p90 | **118 / 178** | 139 / 188 | 143 / 195 | 127 / 177 |
| fps med / p10 | **8.49 / 5.83** | 7.17 / 5.33 | 7.00 / 5.13 | 7.88 / 5.65 |
| holes_frac | 0.52 | 0.54 | **0.43** | 0.63 |
| SoftDefer stuck | **0** | 0.5 | **0** | 2 |
| schedule_ok | **1** | 3.5 | 4 | **1** |
| dirty_fm | **2** | 8 | 9.5 | 5 |
| dirty_remesh | **0** | 10 | — | — |
| cull / scene | **10.5 / 24.6** | 15.8 / 42.8 | 20.2 / 43 | 12.6 / 35 |
| stream / emerge | 31 / 49 | 30 / 51 | 28 / 59 | 31 / 49 |
| column_meshing_n | 73 | 94.5 | 80 | 88 |
| column_lighting_n | **62** | 37 | — | — |
| fifo | **1** | 7 | 14 | 11 |
| relight_drain_ms | **9.1** | 1.7 | 1.7 | — |
| fps \|Δ\| med / p90 | 2.34 / 5.99 | 2.28 / 4.90 | **1.30 / 2.76** | 2.00 / 5.27 |
| spike wall>160 | **10%** | 25% | 27% | 22% |

`sched_ok==0` cruise: **48%** (172957: 36%). `fm≤2`: **66%** (172957: 29%).

## Что улучшилось (Cut A)

- Opaque cull −5 ms, opaque draw −10 ms, scene −18 ms → wall −22 ms / +1.3 FPS
- SoftDefer stuck 0 (лучше R7.1)
- spike>160 10% (DoD ≤20% ✓)
- discard_late abs 8 vs 251 — меньше бесполезного mesh churn

## Что регрессировало (product / eye)

### 1) FM / schedule starve — главный FAIL
- `schedule_ok` 3.5→**1** (= R7-bad), `dirty_fm` 8→**2**
- `mesh_dirty_schedule_ms` 5.0→**0.11** — почти нет schedule work
- Авто fly-heavy `200007` уже WARN `sched_ok=1`; manual подтвердил

### 2) Remesh обнулился (Cut B подозреваемый)
- `dirty_remesh_n` med **0** vs 10 на R7.1
- Fingerprint skip-walk / cheap SoftDefer continue в `DropRemeshDirtyBeyondRadius` могли перестать поднимать remesh dirty → lit/remesh ring не догоняет

### 3) Lighting backlog при «пустом» fifo
- `column_lighting_n` 37→**62**, fifo 7→**1**, но `relight_drain_ms` 1.7→**9.1**
- Колонки застревают в lighting; drain дороже, очередь короткая — не успевает / thrash

### 4) Stand / stop хвост
- Stand holes **0.75** vs 0.38 (осторожно: у 093849 только 8 stand periods в конце короткого пролёта, meshing 103–106, holes=1 почти на всех)
- ALL holes 0.57 vs 0.44 — хуже KEEP/R7.1 вне cruise

### 5) Jitter DoD не закрыт
- `|Δfps|` med 2.34 (цель ≤1.6); p90 6.0 хуже R7.1
- Wall легче, но period-to-period рывки те же/хуже — управление всё ещё рваное

## Маршрут

Короткий пролёт ~37 periods, focus (−3,3)→(8,3), spd~5.8; period[0] spd=480 (enter/teleport). Сравнимость cruise ok; stand-сравнение слабее из‑за длины.

## Вывод / next

R7.2 **не ship**. Cut A (cull period-10) оставлять можно как wall-win, но **Cut B (DropRemesh fingerprint / cheap skip) — кандидат на revert или сужение** до доказанного safe path: вернуть `dirty_remesh`/`schedule_ok`≥3.5 без возврата SoftDefer RemoveAt.

Не лечить AbortDrip↑ / PreferKick-empty / drain_cap↑ (Cut C уже PRODUCT_FAIL на auto).
