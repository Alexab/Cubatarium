# R6: полная матрица ракурсов 56 видов + 5 скинов

[Все 61 лист с наблюдениями](index.md). Каждый лист прямо открыт Codex:
исходник b35f00a75 и текущая собственная модель, обычный цвет и силуэт.
Это reviewer inspection, не пользовательская подпись и не завершение R6.

## Итоговый набор

`native_accepted/matrix/manifest.json`: 61 ресурс × 3 позы × 10 ракурсов =
1830 настоящих рендеров; цвет + silhouette дают 3660 PNG. Для baseline
снята такая же матрица тем же Release EXE/shaders: ещё 3660 PNG. Восемь
yaw 0…315° с шагом 45°, pitch 15°; дополнительные pitch строго +90°/−90°.
Позы: rest/Idle t=0 и противоположные primary-фазы 1/8, 5/8 (Walk, Swim
либо Fly согласно habitat). У amphibious seal первичная matrix — Walk;
его Swim остаётся отдельным state/habitat гейтом.

Фазы измеряются по реальному glTF clip duration / speed либо procedural
phase. Для B aerial учтён четырёхкратный wing beat. Пара 1/8 и 3/8 не
используется: синусоидальный процедурный gait давал бы одинаковую позу.
Matrix сообщает фактически выбранный clip и наличие явного state mapping.
У исходного butterfly mapping и idle clip отсутствовали — его bind pose
сохранена в baseline как реальное поведение, а не заменена выдуманным Fly.
В текущем наборе таких primary fallbacks нет.

Силуэт получен из alpha coverage настоящего shader/discard прохода на
прозрачной подложке. Это не порог по цвету тела и не отдельный упрощённый
mesh. `audit_accepted_catalog.json` проверяет комплектность, размер PNG,
свежесть, точные входные hashes, yaw/pitch/phase и соответствие силуэта alpha.
61-resource `visual_review.json` отдельно записывает ручной просмотр и SHA
каждого листа. На листах камера автоматически вписывает версии независимо;
они предназначены для anatomy/UV, а не для сравнения абсолютного world scale.

## Найденные и исправленные дефекты

Первый диагностический набор `native_current` выявил обрезание bunny/cow/
sheep/rigid_demo_walker на части ракурсов. B preview использовал только
rest geometry, R — collider; диагональ и движение не помещались в кадр.
Теперь B/R envelope собирается по настоящим SubmitDraw requests на полном
procedural cycle (65 фаз), включая преобразованные vertices/части. Камера
вписывает охватывающую сферу. Envelope кешируется по виду/скину/состоянию и
ревизиям; Invalidate очищает fit caches. Крайние верх/низ используют устойчивый
up vector. Коллайдеры, мировая геометрия, анимация и текстуры этим не меняются.
В итоговом наборе border clipping — **0**. Все 1015 прежних G preview frames
до palette-коррекции sand побайтно совпали: `preview_fit_parity.json`.

Просмотр сверху обнаружил бирюзовый accent на макушке sand_monster. В
собственном recipe он заменён тёплым минеральным оттенком. Изменились ровно
30 pixels head-up UV rectangle и 2 pixels производной иконки; PNG alpha,
model.gltf/model.bin и creature.json побайтно прежние. Скелет, веса, UV,
клипы и world scale не менялись. `sand_cap/payload_change.json`, отдельные
matrix/motion/UI/world captures и exact `sand_cap/review.json`. Исправление
опубликовано; 29 обычных native и все 60 matrix PNG совпали с кандидатом.
У остальных 60 ресурсов все 3600 matrix PNG остались точными копиями ранее
просмотренных кадров: `production_parity.json`. Их листы тоже побайтно те же.

## Осознанные ограничения и отдельные проверки

- У вертикального плоского billboard fire_spirit шесть top/bottom projections
  пусты, как у исходника: взгляд вдоль плоскости. PNG существуют и проверены.
  Его восемь пар primary-поз одинаковы, поскольку sprite статичен в обоих
  вариантах. Это явно отражено в audit, а не скрыто общим PASS.
- Реальный actor общего итогового flight — sand_monster, после physics.
  Мир остальных 55 видов не проверен одним этим запуском; прежние R5 world
  evidence сохранены, полноценные routes остаются отдельными.
- У crab боковая ориентация клешней отмечена в review для отдельной проверки
  направления движения/дна. Цвет/статический вид не доказывает маршрут.
- Исторические 19 cropped projections исходного octopus устранены: fit теперь
  охватывает реальную root-only geometry и root animation, включая off-grid
  ключи. Обе версии всех 61 ресурсов заново сняты одним Release EXE:
  `native_root_fit_baseline`, `native_root_fit_current`; clipping 0 у обеих.
  `root_fit_parity.json`: все 1630 legacy + 3660 matrix current PNG совпали
  до/после изменения renderer. 59 парных листов побайтно прежние, изменились
  только reference framing butterfly/octopus; оба прямо повторно просмотрены.
  Актуальные ссылки/SHA: `visual_review.json`, `index.md`,
  `sheets_root_fit_current`, `audit_root_fit_{baseline,current}.json`.
- У tree_monster собственная открытая ветвящаяся корона отличается от плотной
  исходной; ветви, лицо и корневые стопы просмотрены со всех сторон. Модель
  не считается идентичной исходному силуэту.
- Матрица трёх поз не проверяет все clips, переходы, travelScale, contact/
  stride, skins/overlays/equipment/FP/save-load/cache, culling и performance.
  Эти строки главного R6 README остаются pending.

`native_current`, `native_final`, `native_final_matrix` и незавершённый первый
`native_baseline` — промежуточные данные. До root-fit исправления использовались `native_accepted`,
`native_baseline_final`, `sheets_accepted_catalog`, `audit_accepted_catalog.json`;
актуальные парные листы перечислены выше.
Первая baseline-попытка честно остановилась на отсутствии butterfly mapping;
экспортёр затем приведён к фактическому fallback поведения runtime.

## Проверки и воспроизводимость

Требуемая Release сборка прошла; статический EXE проверен на 6 system DLL.
9 выбранных Release C++ tests, targeted elemental fidelity и 12 quality tests
проходят. Khronos `khronos_published/summary.json`: 35 G, ноль errors/warnings/
infos, включая опубликованные PNG. `ledger.json`: 61 exact review/current
source contexts/file records/published+bin matches, manifest 56+5 и bin mirror.

После palette-коррекции обе полные 61-resource output-сборки повторены:
`repro_summary.json`. Все байты совпали, неожиданных Python build reads нет,
runtime payload совпал с текущим опубликованным набором. Это две независимые
output-директории на текущем host, не две чистые ОС/контейнера. Предыдущий
`../repro/summary.json` относится к версии до исправления sand crown.

Повторение matrix: `run_creature_pilot_qa.py --production --species <world-id>
--preview-species <56 ids> --matrix ...`; фотографии только через
`flight_sim_run.py`/`bin/Cubatarium.exe`, Release. Для листов и целостности:
`render_creature_native_matrix.py`. Полный checklist — `../README.md`.
