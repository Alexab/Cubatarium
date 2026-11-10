# GEN-1.2: единый chunk-local coarse path

Дата: 2026-11-04.

## Изменение

`UComposableWorldGenerator::SampleCoarseSurfaceY` теперь является единым accessor: в активном chunk scope он использует `ChunkCoarseCache`, вне scope или за пределами pad вызывает прямой sampler.

Через accessor направлены:

- preliminary surface в density `BuildColumnSample`;
- gradient и coast;
- `Ctx.CoarseSurfaceYAt`;
- существующий biome callback;
- heightmap `ColumnSampleBuilder` продолжает использовать biome callback с тем же cache.

Убраны прямые density coarse lambdas. `PrimeChunkCoarseY`, Begin/End scope и ранние выходы populate сохранены. Cache в const sampling-пути объявлен mutable, поскольку memoization не меняет возвращаемые высоты.

Добавлены счётчики `Hits`, `Misses`, `OutOfRange`; они сбрасываются при Reset и выводятся в populate-лог.

## Проверки

- Release `Cubatarium` собран, статическая Windows dependency check прошла.
- `worldgen_surface_perf_probe`: все 9 exact/coarse/chunk-surface parity cases прошли.
- `density_refine_parity_test`: density parity и новый cache unit test прошли. Проверены повторный hit, вычисление только один раз, fallback вне диапазона и reset счётчиков.
- Два запуска `flight_sim_run.py` с существующим `World_242`: маршруты выполнены, приложение не упало.

В последнем smoke-прогоне 18 записанных populate показали 8 326–37 015 hits и 1 232–1 466 misses на чанк; `coarse_out_of_range=0` во всех записях. Это подтверждает реальное использование нового пути и достаточность текущего pad для наблюдавшихся запросов.

Инструментированный 45-секундный полёт не прошёл часть прежних visual/readiness/performance гейтов: на конце маршрута оставались missing/readiness debt и сцена не сошлась. Эти дефекты соответствуют уже известной проблеме отставания визуала; один прогон без A/B не доказывает регрессию от coarse-cache.

## Ограничения приёмки

Surface parity сохранён, но полный voxel/populate hash ещё не добавлен в GEN-1.1a. Сравнение sample/coarse времени требует одинакового A/B протокола; значения разных полётов не используются как доказательство ускорения. Поэтому этап закрывает единый cache path и его фактические hits, но не утверждает измеренный процент ускорения или полный chunk-content parity.
