# Дыры в поверхности и производительность: корневые причины (2026-10-21)

Этот документ фиксирует результаты расследования «прозрачных дыр» (кубы, грани,
треугольники, чёрные клинья) и первых шагов по производительности. Предыдущие
Era-документы пытались лечить симптомы политиками SoftDefer/ColumnFlow; ниже —
реальные дефекты данных/геометрии, найденные измерением.

## Найденные причины (все исправлены, с тестами)

| # | Симптом | Причина | Коммит | Тест |
|---|---------|---------|--------|------|
| 1 | Треугольные дыры в квадах | `vshader_packed_greedy.glsl` рисовал треугольники (0,1,2),(1,2,3) вместо (0,1,2),(0,2,3) | caef6740 | `packed_quad_parity_test` |
| 2 | Кубы/грани/«щели L-формы» на пляжах и ступенях, «парящие плиты» | GPU greedy-ядро (`kGreedyRectCompute`) хранило индекс плоскости (voxel+1 для +граней) в `rect.slice`, а эмиттеры и CPU `GreedyQuad` считают slice координатой вокселя → все +грани GPU-мешей смещены на блок. Только opaque-only чанки (песок/камень/гравий) проходят GPU-extract, поэтому дыры были «случайными» и на пляжах. Те же +1 и транспонированные u/v были в `ExtractOpaqueFacesCpu`/`DecodeFaceMasks` (Android/worker) | a9b69cea | `gpu_extract_mesher_parity_test` |
| 3 | Щели вдоль границ чанков | Колонки материализуют слои только до верхнего непустого; сосед по X/Z на уровне выше → `Unknown` → грань скрыта навсегда | 1d339ffd | `seam_absent_neighbor_test` |
| 4 | Чёрные клинья/«чёрные чанки» у берега | Грани на верхнем слое чанка (пляж y=63 = верхний слой cy=3) сэмплировали свет в отсутствующем воздушном слое → 0 → меш запечён чёрным | a3be170d | `seam_absent_neighbor_test` |

Дополнительно: вместо «скрыть меш, пока нет света» (`ShouldDrawProvisionalLightPreview`)
тёмные меши без владельца ремонта света теперь рисуются с ambient-превью (8040ab1f).
`ReconcilePublishedMeshInputs` — уровневая сверка опубликованных мешей со штампами
входов (страховка от потерянной инвалидации).

## Инструменты (работают автономно, без участия пользователя)

- Создать мир: `bin/Cubatarium.exe --console --create-world --name X --seed N`
- Прогон с кадрами и аудитом (PowerShell/bash из каталога `bin`):
  `CUBA_FLIGHT_CAPTURE_DIR=<abs> CUBA_FLIGHT_CAPTURE_INTERVAL_SEC=12 CUBA_MESH_AUDIT_AFTER_SEC=40 Cubatarium.exe --flight-sim --world X --no-fly --seconds 60 --idle 0 --pitch -35 --yaw 45`
- `bin/mesh_audit.json` (`UMeshAuditDiagnostics`): ожидаемые верхние грани (полный перебор
  по вокселям) против свежего меша и против опубликованного CPU-меша
  (`render.gpu_packed_meshing=false`), плюс тёмные грани при освещённом поле.
  После исправлений: 0 расхождений на seed 20261020 и 410039.
- `CUBA_TRACE_CHUNK=x,y,z` — трассировка capture/commit одного чанка.
- `block inspect` в консоли — состояние `draw_gate` чанка и соседей.
- `CUBA_SAMPLE_PROFILE=1` (+ `_DELAY_SEC`, `CUBA_SAMPLE_CALLERS_OF=<имя>`) —
  семплирующий профайлер главного потока → `bin/sample_profile.txt`.
- `CUBATARIUM_PERF_LOG=1` включает `bin/logs/perf_*.jsonl` и в flight-sim.

## Производительность (median wall за период, тот же мир и маршрут)

44.6 мс → ~28–31 мс. Что сделано по профилю: мемоизация `ChunkHasStaleDarkFaces`
(было ~17% главного потока), мемоизация сигнатур light-halo на чанк, кэш
env-uniform'ов по шейдеру, мемоизация `HasNearbyFluidSurface` и пропуск air в
скане колонки, один копирование `EnvironmentState` на эпоху и ресемплинг погоды
раз в 8 эпох, O(1) `IsAirOnly` вместо скана 4096 вокселей.

Остаётся (по профилю): `GetColumnRenderableState`/`SyncFocusRingColumnJobStages`/
`RefreshStreamingPressure` (~25% — сканы колонок кольца каждый кадр), per-chunk
uniform/draw в `DrawGreedyGpuBatches` (драйверные затраты), `ChunkMeshSnapshot::Capture`
(копирование halo). Следующий шаг — убрать зависимость геометрии от света и
свернуть набор политик (см. план в чате/ENGINE_REMEDIATION_PLAN).
