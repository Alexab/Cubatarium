# WORLDGEN TAILS: итоговый отчёт

Дата: 2026-11-04. Ветка интеграции: `develop`.

## Влитые этапы

| Этап | Основной commit | Merge commit | Результат |
|---|---|---|---|
| GEN-1.1a surface baseline | `811033775` | `5bb5b3573` | Зафиксирован baseline |
| GEN-1.2 coarse accessor/cache | `3c13248ae` | `045a29cb4` | Единый bounded coarse accessor |
| GEN-1.3 route/wobble cache | `516cea971` | `78f670b30` | Ограниченный cache; object-related fallback измерен |
| GEN-1.1b populate baseline | `1d4979553` | `3f32ebf89` | 9 full-chunk block/fluid/light parity cases |
| GEN-1.4 scan bound audit | `c870f2293` | `0eec557bb` | Полный scan совпал на 1536 колонках; binary search отклонён |
| GEN-1.5 smoothness/quality | `49efc95f1` | `53b82754a` | Smoothness и terrain/forest matrices PASS; quality gate сохраняет FAIL |
| VEG-1 support/prune audit | `2870df744` | `3cc260958` | Unit/integration PASS; production prune не требовал изменений |
| OBJ-1 typed voxel IDs | `7a0741191` | `828cdc287` | Исправлены stale ID и failed rebind |
| MESH-1 witness/Cutout audit | `a52294136` | `ffe332c4c` | Witness записан; unsafe fix не вносился |
| REPO-1 inventory | `4392bd63d` | `b8ea97f98` | Только инвентаризация и точечные ignore-правила |

## Проверки

Release-only проверки включали полную сборку `Cubatarium`, статическую проверку Windows EXE, populate parity, vegetation unit/integration и object voxel ID regressions. Все перечисленные executable checks прошли.

Контрольный `break-stand` штатно выполнил 20 edit→publish (median 5,097 мс, p95 13,685 мс), но общий visual/readiness gate `pass=false`. Это не засчитано как визуальный PASS.

## Открытые дефекты — итог не «всё исправлено»

1. Качество `Calib12_880004`: shallow ocean, нулевой normalized tree count у spawn и flatness 73,96% остаются вне gate. Мир старый; A/B после mudflow этим не заменён.
2. Визуальное отставание за исходным кольцом сохраняется. Профили локализуют publication/GPU output backpressure и attempt churn; FirstMesh cap вслепую не увеличен.
3. Кратковременные дыры при break остаются открытыми. Повторный одиночный centered-break 2026-11-05 показал корректные дно/стенки, 0 dark pixels и публикацию центра за 4,328 мс, но не является per-frame witness каждой exposed face; production fix не вносился. Подробности: `docs/MESH_BREAK_FROZEN_FRAME_REPORT_2026-11-05_RU.md`.
4. Задержка готовности/публикации света сохраняется как часть общего visual pipeline; отдельный relight calculation не доказан как единственный bottleneck.
5. Cutout internal-face optimization не внесена: необходимы полный CPU/GPU parity и alpha A/B, включая разные ID, boundaries, unknown/fluid и все GPU mask/extract пути.
6. VEG-1 не получил новый отдельный multithread/reload и визуальный seam witness.
7. REPO-1 не удалял файлы. Очистка worlds/bin/build/tracked generated требует отдельного согласованного списка.

Никакие пороги качества/performance не ослаблялись. Локальные миры и основной `World_242` не изменялись; witness использовал отдельную копию.
