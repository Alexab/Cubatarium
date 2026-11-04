# MESH: проверка кратковременной дыры при одиночном break

Дата: 2026-11-05. Ветка: `feature/mesh-break-frozen-frame-witness`.

## Результат

**Дефект не подтверждён; production-код не изменён.** Это отрицательная итерация witness, а не доказательство того, что исходная проблема устранена.

## Два прогона

Оба запуска использовали Release `bin/Cubatarium.exe` через `tools/flight_sim_run.py`, отдельные копии `World_242_perf_baseline_source` и ровно один edit (`--break-interval-sec 60`). Общие visual/readiness gates в обоих случаях `pass=false`; эти общие долги не приписывались break.

### 1. Исходный `break-stand`

Артефакты: `bin/suite_reports/mesh_break_frozen/`.

- `break_complete_sum=1`, `break_inflight_race_sum=0`, `break_dark_face_sum=0`.
- `edit_to_publish=11,019 мс`.
- Сценарий удалил блок под ногами: перекрестие было направлено в сторону от земли. Кадры не содержат непосредственно удаляемого блока.
- Следовательно, нулевой dark-face count в этом прогоне не является witness отсутствия exposed-face дыры.

### 2. Блок в центре обзора

Повтор с `--pitch -55`; артефакты: `bin/suite_reports/mesh_break_centered/`.

- `break_complete_sum=1`, `break_inflight_race_sum=0`, `break_dark_face_sum=0`.
- `edit_immediate_n=2`, `edit_dirty_n=5`: центр и шесть face-neighbors были охвачены edit path.
- Центральный slice `(20,4,10)` получил `install_published` в frame epoch 342; `edit_to_publish=4,328 мс`.
- Neighbor publications: `(20,3,10)` — epoch 344; `(21,4,10)` и `(19,4,10)` — epoch 343 и 349 соответственно. Это подтверждает неодновременную публикацию, но само по себе не доказывает видимую дыру.
- До edit виден целый блок; после edit видны дно и стенки выемки. На доступных кадрах нет blank/exposed-face провала.
- Анализ 760 pixel probes: `dark_pixel_count=0`, все 760 voxel rays имеют hit state 1. Это не тёмный экран и не missing-depth witness.
- `post_stop_focus_visual_pending_light_end=0`; оставшийся `post_stop_focus_visual_face_debt_end=1` и другие streaming/readiness долги не совпадают с доказанной дырой при break.

## Почему нет исправления

Гипотеза о задержке Dirty/DigSeam neighbors остаётся возможной, но наблюдаемая задержка публикаций не связана с конкретным blank pixel. Менять Immediate cap, mesher или seam scheduling по этой итерации было бы исправлением вслепую.

## Оставшиеся ограничения

Скриншоты идут с интервалом 0,1 с, а подробные pixel probes — не каждый кадр. Точная координата удалённого блока не записана отдельным diagnostic event. Поэтому дефект остаётся открытым: следующий repro должен в момент `CompleteBreakSession` фиксировать target block, его шесть exposed neighbor faces и per-frame центральный pixel/depth join до первой публикации каждого затронутого slice. Нужен именно положительный witness, после которого можно писать regression test и точечный fix.
