# VEG-1: аудит support/prune растительности

Дата: 2026-11-04.

## Проверенная реализация

После terrain, features и seal чанка `PruneFloatingVegetationInChunk` проходит все 256 колонок и вызывает `PruneFloatingPlantsInColumn`. Алгоритм итеративно обходит removable non-solid растения выше `topSolid + 2` и удаляет те, для которых `HasBlockSupportBelow` не находит ни вертикальной, ни одной из четырёх диагональных твёрдых опор. Удаление возвращается к проверке ранее пропущенных нижних блоков, поэтому не зависит от простого однопроходного порядка сверху вниз.

Отдельного embedded prune по coarse height в актуальном пути не найдено. Он не добавлялся: coarse surface не является финальной поверхностью после refine, carvers, fluid и features.

## Проверки

Release-сборка и запуск:

- `worldgen_vegetation_placement_test`: PASS, включая `TestPruneFloatingLeaves` и `TestSealAndPruneCoastalChunk`.
- `worldgen_vegetation_placement_integration_test`: PASS. Integration строит область 5×5 чанков, выполняет populate в прямом и обратном порядке и сравнивает полные отсортированные snapshots блоков.

До этапа unit target не линковался из-за `CubatariumLogInfo`, используемого диагностикой `WorldGenStages.cpp`. В target добавлены `src/App/Platform/Log.cpp` и `glog::glog`; это исправление тестовой сборки, а не изменение production-алгоритма.

Предупреждение о missing decoration prefab `cubatarium_worldgen_cc0::asset_mossy_ruin_piece` сохраняется и относится к данным объектов, а не к данному тесту prune.

## Границы результата

Production-код prune не изменялся: существующий код удовлетворяет проверенным support и order invariants, нового failing witness нет.

Полный acceptance исходного этапа не закрыт: отдельный многопоточный/reload witness и новый визуальный seam witness не добавлены. Поэтому результат этапа — аудит и восстановление проверок, без утверждения о полном закрытии всех эксплуатационных сценариев.
