# Lobster v3: восстановление корпуса, постановки ног и клешней

Опубликован normal build/validate/review/publish после явного прямого R5 revision
review ассистента. Это не user approval и не финальная R6: все56 существ+5
скинов остаются в полном объёме программы. `revision_review.json` относится к
точному staging bundle; `publication_audit.json` подтверждает фактическую доставку.

## Что исправлено

Похожий общий AABB скрывал другое распределение массы: прежние собственные
ноги стояли узко, колени были ниже, клешни коротки, carapace тонок. Простое
утолщение всех частей этого не исправляло. После сравнений original / own-before
/ proposal выбрана собственная геометрия с более полной оболочкой, широкой
постановкой восьми ходильных ног, длинными объёмными неравными клешнями.

- Carapace: высота5→7 texels с понижением центра, без увеличения общей модели.
- Колени: absX6→8,5, Y5,5→8,5; стопы absX8,5→12,5, ширина1→2. Шахты
  пересобраны по центрам суставов и соединены нормализованным skin blend.
- Palms: длина4→7; подвижные шарниры пальцев перенесены Z−15→−18, пальцы
  длиной5. Сохранены большая и малая клешни, антенны, глаза и segmented tail fan.
- Собственный ventral цвет стал стальным синим вместо больших бледных плоскостей.
  UV allocation пересчитан под новые размеры; surface-space материал запечён
  заново, старый atlas не растянут и reference pixels не использованы.

Результат ближе к исходной форме, но не является копией исходного mesh/rig/atlas.
Fixed-world-camera GPU coverage в Idle: front63,3%→83,5%, side73,5%→89,4%,
oblique67,2%→95,5% от original. Это площадь видимой проекции, не физический
объём и не автоматическое art approval. Решение сделано после непосредственного
просмотра трёх сравнительных листов, трёх full-turn листов и игровых кадров.
Точные SHA и наблюдения — `visual_review.json`, численные площади —
`native_coverage_comparison.json`.

## Скелет и анимация: refit, а не заявление о byte identity

Сохранены все51 сустав, имена, иерархия, animation channel targets, исходные
времена ключей и все90 угловых каналов побайтно. Изменены18 bind-позиций:
16 коленей/стоп и2 шарнира пальцев. Inverse bind matrices пересчитаны и
проверены обратным произведением с bind world matrices. Definition, collider,
eye height, state map и playback speeds сохранены.

**16 позиционных треков стоп перенастроены вместе с bind.** Их старые значения
задают clip-local translations и не должны оставаться от прежней длины ноги.
Пропорциональное масштабирование всего Y тоже неверно: оно увеличило высоту
подъёма стопы в v1/v2 на0,016875 блока. В v3 меняется только компенсация
bind-расстояния с учётом текущего X-угла голени, а прежний подъём сохраняется.
Для именно этих одноосных ветвей применяется
`new_ty(t) = old_ty(t) + delta_bind_y / (16*cos(knee_x(t)))`.
Это проверенная операция данного рецепта, не универсальный 3D retargeter.

`rig_contact_audit.json`:

- Макс. отличие вертикальной траектории суставов стоп —0,000021336 блока
  на257 фазах idle/walk/punch. Небольшая разница между ключами ожидаема при
  сохранении их LINEAR расписания и нелинейной компенсации; gate0,0001 блока,
  <0,004 texel. Root Y этих клипов сохранён. Порог контакта с опорой0,001.
- 18 центров сечений ног/пальцев совпадают со своими суставами с ошибкой
  около6e−9 блока. Это проверка attachment centres, не всей watertight surface.
- Отрицательный контроль: возврат старых translation keys к новому bind даёт
  регрессию0,075 блока. Проверка ловит её, даже когда root contact correction
  пытается скрыть проблему снижением корпуса.
- Веса finite/normalized, UV внутри atlas, треугольники не вырождены.
  672 triangles и51 joint — как до refit. Atlas128×128 и BIN134600 bytes
  также не выросли; это не само по себе performance acceptance.
- 257-phase full-mesh/effectors contact diagnostics сохранены для всех четырёх
  asset clips. `die` остаётся asset capability, не новым runtime Death state.

## Native и мир

`native_audit.json`:126 main original/candidate PNG, равное мировое framing,
max offline/native bounds error0,000007862 блока. Найденные visible edge
fragments сохраняются аналитическим envelope. `native_current/transition_probe.json`:
100 cold cases/1100 кадров, supported bottom/ground poses остаются над опорой,
envelope покрывает фактические submissions. Это CPU переходы, не GPU fade proof.

В `world_tread/` — отдельный20-second обычный input/motor прогон:1036 записей,
18,655 sim секунд,5,006 блока суммарного XZ пути по дну в обе стороны,
621 moving Tread sample. В `world_published/` повторены уже обычные bin/models
без QA override:834 записи/18,311 sim секунд,4,775 блока,513 moving Tread samples.
Frozen и solid-overlap записей нет. `world_motion_audit.json` в каждой папке
независимо проверяет model/shader/EXE SHA, derived state, onFluidBottom и путь.
Input driver задаёт только CreatureIntent; не форсирует позу/факты/позицию.

Первый world прогон при `envelope_current` сохранился **failed**: postcheck
ошибочно требовал Swim от benthic маршрута, тогда как наблюдался Tread. Его
GPU/CPU preview evidence достоверен и проверен отдельно; failed world verdict
не подменён успехом. Runner получил явный `--world-fluid-state Tread`; новый
успешный маршрут сохранён отдельно. Default Swim для amphibious проб не ослаблен.

Original/own-before reference captures переиспользованы из v2 после проверки
**byte-exact profile, того же EXE, всех models/textures/shaders**; их context
timestamps сохранены. Происхождение — `reference_evidence_reuse.json`.

## Публикация и regression

Собственный pre-publish snapshot: `assetgen_output/creatures/lobster_anatomy_pre_publish/`,
16 файлов с SHA. `reproduction_audit.json` подтверждает точное построение v3 из
него после публикации. `publication_audit.json`:

| Gate | Проверенный результат |
| --- | --- |
| Reviewed candidate → staging/models/bin | glTF semantic exact, BIN/PNG byte exact;89 native RGBA exact |
| Production56+5 matrix |5290 кадров:5201 RGBA exact;89 изменений только lobster |
| FP |936 RGBA exact к предыдущей production;1872 GPU arm draws,6 retained cases |
| Isolated entity persistence |128 cases/256 actual restores; не whole-user/world save |
| Ledger |61/61 свежих reviewed bundles/inputs/file records/runtime payload |
| Khronos |35 QA glTF,0 errors/warnings/infos |
| Tests |15 targeted Release PASS; свежий bounds audit **Failed17**, не скрыт |

Release EXE не менялся в этой ревизии:
`a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f`.
196 CTest зарегистрированы; полный CTest не принят.

## Отклонённые версии и оставшееся

`../lobster_anatomy_v1/`: полезная постановка ног, но тонкие длинные пальцы
читались как вилки. `../lobster_anatomy_v2/`: более полные palms, но неправильное
масштабирование foot lift и доминирующие бледные поверхности. Их изображения,
решения и failed rig checks сохранены. Format/floor green не стал art/rig acceptance.

Остаются R6 stride/sliding и характер Walk относительно original, сложный
terrain/переходы/реальная атака/AI, полная watertight/surface проверка, retained
reload нового bind, control/pack/UI, matched CPU/GPU p95/memory и package.
Далее другие виды из17-species triage: oerkki, seahorse, whale; затем orc/puffin
и прочие motion gates. Цель active, все61 R6 pending.

## Воспроизведение и источники

```powershell
python tools/research/build_creature_lobster_anatomy_pilot.py --qa-name lobster_FRESH --variant v3 --source-root assetgen_output/creatures/lobster_anatomy_pre_publish
python tools/research/audit_creature_lobster_anatomy.py --qa-name lobster_FRESH --out docs/research/lobster_FRESH --before-root assetgen_output/creatures/lobster_anatomy_pre_publish
```

Native run — через `run_creature_pilot_qa.py` / `flight_sim_run.py`, Release в
bin, свежие output/label/native dirs. Для bottom route явно `--world-motion
water_pause --world-fluid-state Tread --water --seconds 20`; CLI записывает
ожидаемое состояние отдельно от действительных facts.

[NOAA Fisheries: American lobster, Appearance](https://www.fisheries.noaa.gov/species/american-lobster)
описывает различие мощной дробящей и более тонкой режущей клешни. Это
анатомический ориентир для сохранённой собственной асимметрии, не источник mesh.
[Khronos glTF2.0, skins и animation targets](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins)
задаёт связь joint order/inverse binds и анимируемых TRS свойств. Необходимость
перенастройки translations при изменении bind дополнительно доказана нашим
negative control и проверкой экспортированных pose matrices.
