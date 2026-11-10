# GEN-1.1a: воспроизводимый surface baseline

Дата: 2026-11-04.

## Цель и границы

Добавлен исполняемый `worldgen_surface_perf_probe` для измерения и контроля детерминированного результата `DensityFieldSampler` до оптимизации coarse/surface запросов.

Это **surface baseline**, а не полный populate benchmark. Harness покрывает точный и coarse поиск высоты на сетке 16 × 16, координатный хэш поверхности, счётчики и фазовые таймеры. Он не заполняет voxel chunk, не хэширует растительность и не измеряет полный populate конвейер. Поэтому полный GEN-1.1 по voxel/populate parity остаётся открытым.

## Метод

Проверяются девять сочетаний:

- профили `mountain`, `coast`, `mountain_caves`;
- seeds `777001`, `777002`, `777003`;
- базовые координаты mountain `(3072, -2048)`, coast `(-2048, 3072)`;
- 256 уникальных колонок в каждом случае;
- отдельный вариант `cavesInDensity=true`.

Эталон — сохранённые результаты `SurfaceYAt` с полным нисходящим `DensityScanTop`. Проверяются exact/coarse/chunk-surface хэши и идентичность сценария; машинно-зависимые таймеры в parity не входят. Счётчики сохраняются для последующего сравнения оптимизаций.

Профилирование sampler отключено по умолчанию и включается только harness. Оно рассчитано на single-threaded probe; production-поведение density не изменялось.

## Наблюдения

| Профиль | RouteAt на case | Wobble на case | TerrainDensity по seeds | Wall, 3 seeds |
|---|---:|---:|---:|---:|
| mountain | 512 | 512 | 1779 / 1535 / 1280 | 2,939 мс |
| coast | 512 | 512 | 1403 / 1672 / 1356 | 2,705 мс |
| mountain_caves | 768 | 768 | 2035 / 1791 / 1536 | 3,690 мс |

Во всех случаях 256 `SurfaceYAt`, 256 `CoarseSurfaceYAt`, 512 `DensityScanTop`. Для caves дополнительно 256 `PrepareColumn` и 256 cave-density вызовов.

Это микроbenchmark: таймеры нельзя экстраполировать на FPS или populate мира. Счётчики уже подтверждают повторные route/wobble вычисления и дают количественную цель для GEN-1.2.

## Проверки

- Release `Cubatarium`: успешно; статическая Windows-проверка зависимостей пройдена.
- `worldgen_surface_perf_probe`: все девять parity-сценариев прошли, повторный прогон после полной сборки также прошёл.
- `density_refine_parity_test`: все проверки прошли.
- `git diff --check`: ошибок нет.

Машинный отчёт: `bin/suite_reports/worldgen_surface_perf_after.json`.

## Следующий шаг

GEN-1.2: единый chunk-local coarse accessor через существующий `ChunkCoarseCache`, с сохранением surface parity. Отдельно необходимо расширить harness до настоящего voxel/populate hash перед оптимизациями, затрагивающими заполнение блоков.
