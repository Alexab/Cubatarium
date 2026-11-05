# MESH: проверка кратковременной дыры при одиночном break

Дата: 2026-11-05. Ветки: `feature/mesh-break-frozen-frame-witness`, `feature/mesh-break-per-frame-witness`.

## Результат

**Дефект не подтверждён; production scheduling/mesher не изменены.** Добавлена только opt-in диагностика. Это отрицательный witness, а не доказательство того, что исходная проблема устранена во всех условиях.

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

## Покадровый центральный witness

Ветка `feature/mesh-break-per-frame-witness` добавляет opt-in `CUBA_VISUAL_BLACK_TRACE_CENTER_EVERY_FRAME=1`: один центральный RGB/depth/stencil/voxel-ray sample на каждом render frame. Обычная grid/cadence без этого флага не меняется. Полная Release-сборка и static EXE check прошли.

Прогон `--pitch -65`, артефакты `bin/suite_reports/mesh_break_per_frame_65/`:

- Выполнен ровно один break в frame epoch 205; `break_inflight_race=0`, `break_dark_face=0`.
- До break центральный луч попадал в блок ID 549 `(323,66,168)`, top face 4; opaque surface и depth были валидны.
- Уже на break-кадре луч попадает в exposed блок ID 378 `(323,65,168)`, top face 4. То есть в центре видна донная поверхность выемки, а не удалённый блок и не пустой пиксель.
- Центральный slice `(20,4,10)` drawable и draw-ready; mesh revision 4, published geom revision 579, visible MDI index count 1656; `voxel_mesh_ray_gap=0`.
- `edit_to_publish=2,591 мс`; во всех 556 покадровых central samples voxel ray имеет hit, `dark_pixel_count=0`.
- Скриншоты до/после подтверждают непрерывные дно и стенки; частицы разрушения не классифицируются как дыра.

Это отрицательный witness для центральной exposed top face в данном сценарии. Он не доказывает отсутствие дефекта на боковых гранях, chunk boundaries или при другой загрузке pipeline. Production scheduling/mesher не изменялись. Сам режим диагностики добавляет GPU readback каждый кадр и поэтому не пригоден как performance baseline.

## Оставшиеся ограничения

Для исходной пользовательской проблемы всё ещё нужен положительный witness на конкретной боковой/boundary face. Текущий сценарий не даёт основания менять Immediate cap, Dirty/DigSeam или свет. Общие readiness/visual gates остаются независимым открытым направлением.
