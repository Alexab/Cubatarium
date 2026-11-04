# GEN-1.4: аудит DensityScanTop

Дата: 2026-11-04.

## Решение

Production `DensityScanTop` не изменён. Более узкая граница и бинарный поиск не внедрялись: caves/FBM density не дают монотонной функции по Y.

В `worldgen_surface_perf_probe` добавлен независимый test-only reference: для terrain без caves выполняется прямой нисходящий `SampleTerrainDensity` от `MaxHeight=319` до `MinHeight=-64`, затем результат сравнивается с `SurfaceYAt`, использующим `DensityScanTop`.

Reference выполняется на отдельном sampler и не входит в таймеры/счётчики performance cases.

## Проверка

- 2 профиля (`mountain`, `coast`) × 3 seeds × 256 колонок = **1536 parity сравнений**.
- Все сравнения прошли: оптимизированный поиск не пропустил более высокую terrain surface.
- Все 9 сохранённых surface baseline cases прошли.
- Все 9 полных populate block/fluid/light baseline cases прошли.

Caves не включены в независимый full-scan terrain reference: их результат проверяется сохранёнными surface и populate baseline, но это не математическое доказательство для произвольных cave params. Поэтому никакого дополнительного сужения окна на основании этого теста не предлагается.
