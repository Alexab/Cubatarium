# MESH: проверка кратковременной дыры при одиночном break

Дата: 2026-11-05. Ветки: `feature/mesh-break-frozen-frame-witness`, `feature/mesh-break-per-frame-witness`, `feature/mesh-break-boundary-witness`.

## Результат

**Кратковременная дыра на стыке чанков подтверждена покадровым A/B и исправлена для одиночного boundary edit.** Ранние сценарии давали только отрицательные witness: они не проверяли конкретную боковую грань. Финальный production fix публикует центральный slice и реальные seam-neighbors одного edit до первого нового кадра.

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

## Покадровый boundary witness и A/B

Для проверки боковой грани использован фиксированный render eye `321.5 66.5 172 180 -55` и явный одиночный break `(320,64,172)`. Это отдельные копии `World_242_perf_baseline_source`; opt-in переменные `CUBA_FLIGHT_BREAK_EYE` и `CUBA_FLIGHT_BREAK_BLOCK` не меняют обычный ввод. Точный synthetic break обходит ограничение creature interaction range, но использует штатные `StartBreakSession`/`CompleteBreakSession` и production edit/mesh path.

### До правки: положительный witness

Артефакты: `bin/suite_reports/mesh_break_boundary_v16/`, покадровый переход на frame epoch 124.

- До break центральный луч видит удаляемый блок `(320,64,172)` и валидную top face. На break-кадре он уже видит `(319,64,172)`, ID 378, `entry_face=1`, voxel distance 3,47989.
- Одновременно pretransparent depth равна 1, opaque surface не валидна и не drawable; RGB соответствует небу. Это доказанный однокадровый seam-hole, а не просто тёмная грань.
- Центральный `(20,4,10)` опубликован на epoch 124 (`event_ms=230201008,2866`). Seam-neighbor `(19,4,10)` опубликован только на epoch 125 (`event_ms=230201087,2974`).
- На epoch 125 depth 0,971455, opaque distance 3,48007, MDI index count 1008, drawable/draw-ready 1, mesh-ray gap 0. Разрыв между voxel state и renderer длился один кадр.
- `edit_to_publish=6,7156 мс` относится к центральному edit и не измеряет задержку соседа.

### Production fix

Для одиночного блока на локальных координатах 0/15 определяются только реальные face-neighbors через chunk seam, включая отрицательные мировые координаты. Их приоритет следует сразу за center; добавлен deterministic tie-break.

Immediate cap именно для такого edit равен `1 + число реальных seam-neighbors` (2–4), поэтому center и все затронутые boundary slices проходят один hot path в пределах 8 мс. Обычный interior edit, multi-block burst и общий streaming Immediate cap не расширяются. Regression test покрывает `-X` boundary и center+2 seams на ребре.

### После правки: контрольный прогон

Артефакты: `bin/suite_reports/mesh_break_boundary_v17/`, break на frame epoch 114, тот же eye и блок.

- `edit_immediate_n=3`: center `(20,4,10)`, X seam `(19,4,10)` и Y seam `(20,3,10)`.
- Все три публикации произошли на epoch 114: соответственно `event_ms=231908427,6642`, `231908452,1366`, `231908491,7221`. Первая новая центральная probe записана в `231908533,9512`.
- На первом кадре после break луч уже видит `(319,64,172)`, `entry_face=1`; depth 0,971455, opaque distance 3,48007, MDI index count 1008, drawable/draw-ready 1, gap 0.
- Во всех 170 central samples voxel ray имеет hit; `dark_pixel_count=0`, число samples с voxel hit и missing opaque depth/surface равно 0.
- `edit_to_publish=21,1926 мс`. Этот instrumented прогон не является performance baseline: покадровый GPU readback и общий background backlog влияют на время.

Release-сборка и `edit_mesh_remesh_policy_test` прошли. Общие readiness/performance gates повторного wrapper-запуска остаются FAIL; они не используются для подмены покадрового критерия данного дефекта.

## Оставшиеся ограничения

Исправление подтверждено для одиночного boundary edit в указанном сценарии. Multi-block burst, разрушение под более тяжёлой очередью и другие направления граней требуют отдельного regression coverage; не доказано, что все возможные дыры устранены. Общие readiness/visual gates, отставание мира за кольцом и готовность света остаются независимыми открытыми направлениями.
