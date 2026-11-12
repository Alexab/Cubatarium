# Четыре motion/mass улучшения опубликованы через normal pipeline

Статус: принята и опубликована **R5 revision** badger,treeman,lobster,seahorse.
Это не финальная R6 приёмка. Основная цель active; все61 r6_state остаются pending.

## Что изменено в основных ресурсах

| Вид | Принятое изменение | Что сохранено |
| --- | --- | --- |
| badger | Вторичный roll корпуса6° walk /9° run возвращает движение тяжёлого туловища | Собственная анатомия/морда,PNG,кости/IBM,коллизии |
| treeman | Ствол8→14 texels по X; ветви-конечности утолщены вокруг прежних осевых линий; roll22°/26°; UV-размеры заново выделены и собственная кора перепечена | Позиции/иерархия костей и IBM,крона/лицо,общая высота/идентичность,коллизии |
| lobster | Клешни поднимаются до55°,передняя часть корпуса наклоняется15°,контакт пересчитан по skinned mesh | Собственная геометрия/PNG и кости,донные gameplay bounds |
| seahorse | Хвост распрямляется по bind centreline с плавной65% амплитудой вместо простого усиления curl | Собственные голова/окраска/риговая цепь,свободное плавание без ground clamp |

У treeman утолщение не раздвигает концы ветвей вдоль их оси:24 сечения
сохранили исходные центры с ошибкой не выше1.78e−15 texel. После расширения
кубам назначены новые integer texel dimensions и UV islands,а surface_materials
генератор вновь нарисовал направленный рисунок коры. Изменился собственный
texture atlas,не было копирования reference pixels или увеличения PNG старой
текстуры. Joint weights для новой поверхности проверены на нормализацию;
не утверждается,что все WEIGHTS_0 bytes у утолщённой геометрии прежние.

Из456 треугольников treeman ни один не стал вырожденным; медианная плотность
текстуры в model space13.62→14.30. Нулевой UV-area остаётся у тех же128 triangles:
это существующие однотексельные грани/тонкие детали с centre sampling,не новое
схлопывание UV. Медиана не доказывает одинаковую плотность каждой грани;
текстура дополнительно проверена прямым просмотром native UI/world images.

## Прямой review и native evidence

Прямо просмотрены все8 original/own motion/front-side sheets,`ui_native_sheet.png`
с16 обычными native UI ракурсами и`world/world_contact_sheet.png` четырёх
собственных существ. У treeman дополнительно прямо просмотрен actual world
frame002 из combined envelope run. Решение — улучшение существующих собственных
ресурсов с сохранением уникальности; это не попытка побитового копирования
неудачных/статичных исходных клипов. Исторический seahorse v3 отклонён.

- `native_audit.json`:576 paired GPU PNG,одинаковые original/own world framing,
  independent/native bounds error <=0.000140740 блока;4/4 edge cases пропускаются
  новым glTF visual culling predicate.
- `invariant_audit.json`:nodes,skins/IBM и physical definitions прежние;
  UV/joint indices/weights/triangles валидны;5456 actual retained CPU transition
  frames внутри conservative envelope,поддержанные floor minima >=−0.001 блока.
- `pure_contact_1025.json`:1025 фаз каждого asset clip. Активные наземные/донные
  minima >=−0.000218 блока;плавание/death не выдаются за foot planting.
- `khronos/summary.json`:все35 glTF в QA каталоге,0 errors/0 warnings/0 infos.
- `world/audit.json`:4 отдельных8-second Release flights,116 preview и12 world
  PNG,actual src=3 species и assets/shaders/EXE hashes. Metrics disabled;
  это idle/no-travel,не маршрут или continuous world attack/swim acceptance.

R5 решение зафиксировано в`revision_review.json` по точным staging bundles с
явным reviewer и image SHA. Review сделан ассистентом по непосредственно
просмотренным изображениям,не выдан за подтверждение пользователя.
`runtime`/`ui_and_skins` относятся к этому revision subset;у этих четырёх нет
совместимых authored skin variants. UI screen/pack switching R6 остаётся открыт.

## Публикация и общая проверка

Own состояние до правки сохранено в project-shaped snapshot
`assetgen_output/creatures/motion_v5_pre_publish/`:4 recipes/models/staging плюс
старые contracts/catalog/global manifest,55 файлов с SHA. Это собственные
ресурсы до revision,не reference source.

Canonical recipes обновлены и заново прошли `build_species`/`validate_staging`.
Модели,bin buffers и PNG из normal build точны к рассмотренным QA кандидатам.
Затем `publish_species` заново построил и проверил reviewed bundles перед
копированием в обычные models и bin/models. Contracts r5_review_path у четырёх
указывает на новый review; r6_state остаётся pending. Global manifest обновлён.
Production generator reference reads не выполнял.

После требуемой Release сборки выполнен combined production flight через
`tools/flight_sim_run.py` на`bin/Cubatarium.exe`:

| Проверка после публикации | Результат | Предел доказательства |
| --- | --- | --- |
| Ledger |61/61 exact reviews,published/bin/staged payload,source freshness,file records | Целостность R5,не художественная R6 |
| Полная61 native matrix |5290 PNG:5081 RGBA exact,209 ожидаемо изменены только в4 reviewed species |8 yaw+top/bottom,3 sampled poses и ordinary previews |
| Разбиение changed PNG |badger56,treeman89,lobster8,seahorse56 | У остальных57 ресурсов все sampled images точны |
| Candidate→production UI |116/116 RGBA exact | Рассмотренные4 QA кандидата реально дошли в обычные ресурсы |
| FP |936 RGBA exact к предыдущему authored FP;1872 actual authored GPU arm draws;6 retained-cache cases | Полная FP framing/attack/wall/presets приёмка pending |
| Entity save |128 legacy/v2 cases,256 actual loads,world actors unchanged | Не повторный whole-world/user/cooperative save gate |
| Tests/build |13 targeted Release tests проходят,static Windows EXE verification PASS | Общий CTest не принят |

Текущий EXE:
`929205bada51d2c6862ad3989471ccc79a70c91104c099894de497b0279bb5f1`.
Проверки целостности/публикации:`publication_audit.json`,
`ledger_after_publish.json`,`production/{body_parity,fp_audit,audit_production_final}.json`.
Source snapshot позволяет воспроизвести research builder через
`--source-root assetgen_output/creatures/motion_v5_pre_publish` с новым`--qa-name`.
Для повторного invariant audit нужно передать тот же`--source-root`,чтобы
сравнение не подменяло прежние собственные ресурсы уже опубликованными.

## Дальнейшая работа

B/R conservative envelopes должны учитывать actual pivot placement,
procedural translations,crouch и wearable/held geometry. Их нельзя закрыть
одним перенесённым glTF radius. Требуются world frustum sweeps и стоимость
дополнительных admitted draws. Исторический close-QA camera near-clipping
барсука также не считается исправленным этим удачным world run.

Остаются полная stance/stride/terrain/contact и habitats/routes/control
приёмка всех56+5,orc secondary motion,puffin flight pitch,pack/UI switching,
FP framing/attack/wall/presets,clean Release package/no.orig и matched
CPU/GPU p95/memory. Последний широкий CTest192:50 passed/141 unbuilt/
1 bounds audit Failed;сейчас зарегистрировано194 tests,новая широкая проверка
ещё не выполнена. Цель active,полная R6 pending.

## Primary best practices

[Blender UV editing: Average Island Scale](https://docs.blender.org/manual/en/3.6/modeling/meshes/uv/editing.html)
описывает согласование масштаба UV islands. В проекте соответствующий принцип
применён через integer texel dimensions и повторную раскладку/запекание после
увеличения геометрии; это наше решение для собственного pixel pipeline.
[Khronos glTF2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
задаёт skin joints/IBM и local TRS анимацию;вместо смены рига проверены
сохранность joint hierarchy/IBM,нормализация weights и реальные native poses.
