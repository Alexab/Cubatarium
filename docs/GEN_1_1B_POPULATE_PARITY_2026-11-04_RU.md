# GEN-1.1b: полный populate/voxel parity baseline

Дата: 2026-11-04.

## Что добавлено

`worldgen_populate_parity_probe` вызывает настоящий `UPipelineChunkPopulator::Populate` для девяти сценариев: `mountain`, `coast`, `mountain_caves` × seeds `777001–777003`.

Для каждого результата:

- сканируются все 256 XZ-колонок целевого чанка и весь диапазон `Y=-64..319`;
- отдельно вычисляются FNV-хэши block, fluid и light в фиксированном порядке координат;
- в block hash включены Y-bounds буфера;
- сохраняются число непустых блоков и fluid cells;
- записываются wall и populate total/sample/terrain/carve/post/seal timings.

Это содержимое фактически сгенерированного `UChunkBuffer`, а не повторный расчёт высот. Таймеры в parity не входят.

## Результат

Baseline сохранён в `src/Test/Data/WorldGenPopulateParityBaseline.json`. Немедленный повторный прогон совпал по всем block/fluid/light hashes и nonempty counters для девяти чанков. Примеры block hashes:

- mountain/777001: `0dc215ea5df81edb`;
- coast/777001: `410c5d13562d9207`;
- mountain_caves/777001: `45ceff4e6dc2c805`.

Машинный отчёт: `bin/suite_reports/worldgen_populate_parity_after.json`.

## Проверки

- Release `Cubatarium`: успешно, static Windows dependency check пройден.
- `worldgen_populate_parity_probe`: parity PASS.
- `worldgen_surface_perf_probe`: все 9 cases PASS.
- `density_refine_parity_test`: PASS.
- `git diff --check`: ошибок нет.

## Границы покрытия

Probe загружает `cubatarium_cc0_base` и default worldgen pack, но не создаёт внешний `UObjectLibrary`. В этой конфигурации object features отключены, а slots `ore_coal`/`ore_iron` не resolved; соответствующие предупреждения сохраняются в логе. Поэтому baseline покрывает доступный density/terrain/carve/post/seal pipeline, но не является parity всей production object/vegetation/ore конфигурации.

Fluid/light хэши присутствуют, хотя в текущих девяти сценариях их содержимое пусто; ненулевые fluid/light сценарии следует добавить отдельно. Сравнение object placement также требует явной загрузки object library и активных feature configs.
