# R6: воспроизводимость и происхождение 56 видов + 5 скинов

Этот проход сохраняет ранее принятый R5 внешний вид. Это не завершение R6.
Reviewer — Codex; пользовательскую подпись приёмки не предполагаем.
Этот набор исторический до последующей коррекции sand crown. Текущая полная
пересборка/ledger и pose matrix находятся в ../matrix/README.md; прежняя
проверка against pre_r6_provenance не должна считать эту намеренную
изменённую верхнюю грань ошибкой metadata-only исходного прохода.

## Проверенные кандидаты

`summary.json`: две полные сборки в разных пустых выходных каталогах
run_a/run_b дали одинаковые 61 пакет, включая manifest и LICENSE.txt.
Обе выполнялись в одном Python-процессе на текущем Windows host, а не в двух
чистых ОС или контейнерах. CPython 3.13.7, Pillow 11.3.0, zlib и PyYAML
записаны в каждом manifest; production requirements зафиксированы.

Python audit hook регистрировал readable open во время генерации. Разрешены
собственные исходники/рецепты/палитры/контракты, определения creature/skin JSON,
выходные файлы и установленный Python runtime. Reference mesh/PNG, архивы
исходников и production PNG/geometry/bin не читались. Socket connect запрещён.
Это проверка Python I/O данной сборки, а не системная песочница. До сравнения
с production hook переключался из build-фазы; сравнение не является входом
генератора. `audit_candidate.json` дополнительно подтверждает, что изолированные
сборки оставили весь live models tree неизменным относительно заранее
сохранённой копии pre_r6_provenance/models.

Все файлы, влияющие на runtime, включая creature.json/skin.json, совпали
побайтно с принятыми ресурсами. Первая попытка обнаружила дрейф rigid walker:
общая доработка exporter добавляла tail pivot. В рецепте теперь явно задан
center pivot хвоста; accepted rigid_model.json воспроизводится точно.
Проверка не была ослаблена ради этого отличия.

## Provenance и лицензирование собственных outputs

Manifest содержит hash исходников, рецепта и палитры, functional contract,
toolchain, seed/seed_hex, роли файлов, SHA файлов и PNG pixels, отдельные
mesh/clip records для glTF, связь с базовой геометрией скина. Review связывает
toolchain и functional contract с точным bundle hash. Метаданные приёмки
контракта исключены из functional hash, чтобы не создавать рекурсивную
зависимость review → bundle → review.

У скинов список текстур берётся из skin.json; содержимое опубликованного
каталога больше не определяет новые outputs. Global manifest включает все
56 видов и 5 скинов, явно отмечает отсутствующий provenance и не считает
непроверенный export принятым. Runtime-копия global manifest синхронизируется.

Старые upstream notices не описывали уже заменённые собственные outputs.
Их копия сохранена в pre_r6_provenance/models. Новые LICENSE.txt используют
существующую корневую MIT-лицензию проекта для собственных generated files,
перечисленных в manifest. Это не изменение лицензии чужих, не входящих в эти
пакеты файлов. Из fire_spirit при публикации удаляются только прежние
неиспользуемые model.gltf/model.bin: действующий backend — billboard.

## Сохранность изображения

- `candidate_catalog_parity.json`: 1630/1630 exact RGBA frames, все 56 видов.
  Включены дополнительные human previews двух скинов и equipment.
- `candidate_{sheep_wool_black,sheep_wool_golden,wolf_snow}_parity.json`:
  дополнительно 87/87 кадров трёх остальных скинов.
- Сверены EXE/shader hashes, свежесть PNG и hashes фактически поданного
  candidate каталога. Это тот же Release EXE до/после.
- `khronos_candidate/summary.json`: все 35 G, ноль errors/warnings/infos,
  включая собственные PNG, объявленные в glTF.

Сводный лист всех 56, отдельные skin previews, badger world и дополнительные
skin world кадры открыты reviewer. В общем запуске реальный world actor —
только badger. Остальные world proofs в `review.json` явно наследуются из
индивидуальных R5 приёмок при неизменном payload. Дополнительные овечьи world
кадры показывают спину, у волка есть перекрытие травой; это supplemental
spawn-проверки, они не заменяют прежние полноценные индивидуальные evidence.

`review.json` переносит прежние R5 решения только на эти точные metadata
bundles. 29 кадров вида не заменяют 8 yaw × 3 позы + top/bottom/silhouette.
Маршруты/состояния, save-load, resolved keys/cache, полная экипировка/FP,
чистая сборка/package и performance gates остаются отдельными задачами.

## Опубликованный результат

Все 61 metadata bundles опубликованы через exact review gate.
`audit_published.json` подтверждает source/bin runtime bytes и notices;
`../ledger_after_repro.json` — 61/61 exact reviews, current source contexts,
file records и published/bin matches, manifest 56+5, runtime global mirror.
Khronos published: 35 G, ноль errors/warnings/infos.

`production_catalog_parity.json` и три `production_*_parity.json`:
повторный обычный bin/models каталог дал те же 1630 + 87 = 1717 кадров.
Production badger world кадр прямо просмотрен после physics. Внешний вид,
геометрия, текстуры, authored motion и skin definitions сохранены.
36 fidelity, 12 quality и 9 выбранных Release C++ tests проходят.
Полная R6 остаётся pending по причинам, перечисленным выше.

## Повторение

1. `python -X utf8 tools/research/rebuild_creature_catalog.py`
2. `python -X utf8 tools/research/audit_creature_provenance_rebuild.py`
3. Release native capture через `run_creature_pilot_qa.py` и
   `audit_creature_native_equivalence.py`; scopes задаются явно в context.
4. Публикация требует exact `review.json`, структурный успех её не разрешает.
5. После публикации: `audit_creature_provenance_rebuild.py --published`,
   ledger, Khronos и обычный production capture.

Повторная сборка после изменения generator source или toolchain требует
нового bundle review; старое решение автоматически не переиспользуется.
