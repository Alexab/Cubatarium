# GEN-1.5: quality gates после worldgen оптимизаций

Дата: 2026-11-04.

## Прогоны

- Release `worldgen_smoothness_test`: **PASS**, включая три density terrain seeds и mountain biome checks.
- `tools/terrain_full_metrics.py` на `Calib12_880001–880010`: **10/10 GATE ok**, по 160 000 колонок на мир.
- `tools/forest_metrics.py` на тех же 10 мирах: **10/10 GATE ok**. Два океанических мира не содержат деревьев; скрипт принимает их текущие gates. Для остальных зафиксированы 455–1473 дерева, Clark–Evans 0.310–0.699, dispersion 16.682–33.087, trees on steep 0–8.826%.
- `tools/worldgen_quality_gates.py bin/worlds/Calib12_880004`: **FAIL**.

Артефакты: `bin/suite_reports/gen_1_5_terrain.json`, `gen_1_5_forest.json`.

## Открытое отклонение

Комплексный gate сообщил:

- ocean depth p50 = 8 при требуемом >= 14;
- ocean depth p90 = 18 при требуемом >= 30;
- spawn tree blocks land normalized = 0 при требуемом >= 1500;
- flatness = 73.96% при пределе 68%.

Пороги не изменялись. Это старый сохранённый мир, а не заново сгенерированная одинаковая A/B пара до/после mudflow, поэтому прогон не локализует регрессию и не доказывает влияние GEN-1.2/1.3. Он выявляет качество выбранного сохранённого snapshot относительно текущих gates.

GEN-1.5 нельзя считать полностью зелёным. Следующий диагностический шаг — создать отдельные одноseeding snapshots соответствующей исторической и текущей сборки с одинаковыми packs/settings и сравнить ocean/flatness/spawn vegetation; до этого никаких подгонок terrain или порогов.
