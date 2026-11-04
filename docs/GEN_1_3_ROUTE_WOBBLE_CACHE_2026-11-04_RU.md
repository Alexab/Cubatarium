# GEN-1.3: ограниченный RouteAt/wobble cache

Дата: 2026-11-04.

## Изменение

`UDensityFieldSampler` получил opt-in chunk sampling scope:

- `BeginChunkSamplingCache(base_x, base_z, pad)` задаёт прямоугольник чанка и halo, очищает обе таблицы и счётчики;
- `EndChunkSamplingCache` отключает memoization и освобождает записи;
- exact `DensityRouteParams` и float `horizontalWobble` кэшируются независимо по целочисленному `(x,z)`;
- запросы вне диапазона вычисляются напрямую и не сохраняются, поэтому размер ограничен квадратом chunk + halo;
- cache используется всеми существующими путями `SurfaceYAt`, `CoarseSurfaceYAt`, `PrepareColumn` и density fill.

`UComposableWorldGenerator` открывает/закрывает этот scope одновременно с coarse scope. Существующие отменённые и обычные populate выходы продолжают вызывать End.

В populate-лог добавлены route/wobble hits/misses и общий sampling out-of-range counter.

## Проверки

- Release `Cubatarium` собран, статическая Windows dependency check прошла.
- `worldgen_surface_perf_probe`: все 9 baseline cases прошли без активного scope.
- `density_refine_parity_test`: добавлено 256 колонок × exact/coarse в scoped/direct режимах; все высоты совпали. Проверены 256 route hits + 256 misses и столько же wobble hits/misses, отсутствие out-of-range в тестовом halo и корректный результат после очистки.
- Smoke flight `World_242`, 30 секунд: маршрут выполнен, аварийного завершения нет.

В 27 записанных populate наблюдались 768–1152 route hits и столько же wobble hits на чанк. Для большинства чанков out-of-range равен нулю. На трёх чанках обнаружены object-related запросы за pad: 1156–1804 route/wobble fallback и 568–884 coarse fallback. Это безопасный путь без кэширования, но реальный максимальный радиус object/neighbor запросов требует отдельной локализации перед расширением pad.

## Ограничения

Проверен exact/coarse surface parity; полный voxel/populate content hash всё ещё не реализован. Процент ускорения не заявляется без идентичного многопроходного A/B. Известные проблемы отставания mesh/light этим этапом не устраняются.
