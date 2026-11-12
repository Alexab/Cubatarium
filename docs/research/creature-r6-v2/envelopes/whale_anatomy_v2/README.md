# Whale v2: вертикальный профиль, стыки и честное сравнение направления

Опубликована **локальная R5 revision** hull/head height. Это прямое решение
ассистента по рассмотренным материалам, не user approval и не полная R6.
Ширина корпуса/плавников и вся выразительность исходного движения не приняты.
Все56 существ+5 скинов и три backend остаются в полном объёме цели.

## Что изменено в собственном рецепте

Прежний собственный кит был ниже: bind height3.0 блока против3.3137 у исходника.
При этом общий размах own больше; компенсировать его общим масштабом нельзя.
Новый trunk высотой13 вместо11 texels построен вокруг прежней spine Y6.5.
Head height10→12, rostrum6→7. Размеры и taper согласованы по сечениям:
head/trunk, два trunk участка, rump, tail0/tail1, rostrum/head.
Вход в хвост остаётся высотой7 texels, его pivot/сечение не переносились.
Bind height нового mesh3.375 блока, полный sampled animated union3.453.
Это не размер physics box и не основание менять collider.

Dorsal mesh приподнят на0.5 texel при прежнем pivot, чтобы выступать над
более полным корпусом. У v1 прежний blowhole cap частично оказался внутри
новой головы; v1 **отклонён для публикации**. В v2 cap также поднят на0.5 texel:
над поверхностью головы остаётся минимум0.0374999 блока, основание погружено
в неё минимум0.0812502 блока. Отрицательный контроль на реальном v1 этим же
аудитором отказал с отрицательным clearance. Это выявление дефекта, не waiver.

Все14 суставов, имена/hierarchy/IBM/JOINTS/WEIGHTS,36 animation samplers
(72 input/output accessor arrays) точны. Ранее используемое название поля
`animation_sampler_arrays_exact` считает samplers, проверяя обе arrays у каждого.
Плавники, хвостовые лопасти, yaw, collider, state map и playback speeds прежние.
UV заново выделены и собственная rorqual surface texture перезапечена без
reference pixels.192 triangles, atlas128×128, BIN41168 bytes не выросли.
64 constant-UV triangles однотексельных поверхностей и median density12.0949
прежние. Эти числа не являются полной texture/performance acceptance.

## Почему нужен дополнительный aligned reference

Первоначальные native original/own sheets используют исходные visual settings.
Но reference whale направлен противоположно own и имеет longitudinal centre
Z1.3897914886 вместо0. Поэтому raw «front» показывает разные концы тела,
а сравнение по одинаковому Z сечёт разные анатомические области.

В **отдельной research-копии** `whale_reference_aligned_research` изменены только
неанимируемый skeleton root1 translation Z−1.3897914886 и definition visual yaw
0→180°. Это постоянное совмещение направления и центра. Source original files
не изменились; BIN/PNG/IBM/animation channels копии точны. У root1 нет animation
translation target. Копия не используется ни production builder, ни runtime publish.
`aligned_reference/reference_alignment.json` и `alignment_audit.json` подтверждают
разрешённые semantic differences и SHA исходников. Это выровненная reference
геометрия, не pristine native capture при исходной actor orientation.

Original-aligned/own-before/candidate сняты заново на одном Release EXE/profile;
три листа в `aligned_reference/` непосредственно просмотрены. Также просмотрены
три raw comparison sheets, три native full-turn sheets и candidate/production
world `frame_008.png`; SHA всех11 артефактов — `visual_review.json`.

При aligned общем framing Idle projection area к reference:

| Вид | До | V2 |
| --- | ---: | ---: |
| Front |91.7%|103.2%|
| Side |88.0%|99.3%|
| Oblique |96.3%|103.4%|

Это площадь пикселей, не объём тканей и не самостоятельный художественный gate.
Видимый результат: более полный корпус/голова при собственном тёмно-синем
материале, светлых продольных складках и сохранённых хвосте/челюсти/плавниках.
Ширина и локальное распределение массы всё ещё отличаются от reference;
совпадение общей площади не закрывает эту задачу.

## Скелет, skin и sampled runtime

`audit_creature_whale_sections.py` независимо читает exported positions,
weights/IBM и реальные animation samplers. Шесть пар сечений проверены в bind
и на129 фазах каждого из трёх clips для before и candidate: max section distance
8.882e−16 блока. Повтор после publication использует own pre-publish snapshot:
`post_publish_sections/section_audit.json`. Это проверка конкретных matching
sections, не доказательство watertight всей поверхности/любых intersections.

`rig_uv_native_audit.json`:100 native cold-entry transition cases/1100 CPU frames,
1056 supported, minY2.1504e−6; envelope покрывает все. Native raw original/current
126 основных PNG согласованы с offline skinning, max bounds discrepancy0.0002583
блока в существующем0.001 gate; raw old-collider false cull1+1 восстановлены.
Aligned audit126 PNG даёт max0.0001302, current false cull1 восстановлен.
Это sampled CPU/GPU capture evidence, не все continuous GPU fades/terrain.
Khronos35 QA glTF:0 errors/warnings/infos.

Candidate обычный `water_swim_pause` route:653 samples/17.939 sim секунд,
393 moving Swim,4.469 блока XZ пути, оба направления,16 swim-up requests,
без frozen/solid-overlap. Normal production без model override:758 samples/
19.331 sim секунд,459 moving Swim,4.965 блока пути, оба направления,17 requests,
без frozen/solid-overlap. Проверены actual derived Swim и отсутствие bottom
у moving Swim. Как и для seahorse, controller задаёт обычный input, а вертикальное
движение может поддерживать Swim при паузах XZ. Автономный hover/AI этим не принят.

## Публикация и регрессия

Normal build/validate/review/publish, `publication_audit.json` PASS:

- Exact reviewed recipe/staging/models/bin и review bundle; definitions прежние.
- 5290 PNG:5201 exact к предыдущей seahorse production,89 изменений только whale.
  Все89 candidate→production RGBA exact, EXE и shaders одинаковы.
- 936 FP PNG exact; независимый authored GPU arm audit1872 draws/6 cache cases PASS.
- 128 entity cases/256 restores PASS; все61 R5 ledger свежие.
- Own16-file pre-publish snapshot сохранён. Regeneration из него повторяет glTF
  semantic и BIN/PNG/recipe byte exact (`whale_anatomy_v2_repro`).

Release EXE остаётся
`235ce58dd65546f38164020295ff403bf48c2816b14bb16cd96228faee030483`.
В этом этапе C++/CMake не менялись, desktop rebuild не требовался.15 targeted
Release tests PASS после публикации; fresh bounds audit **FAIL17**.196 CTest
зарегистрировано; весь CTest не объявлен зелёным. Collider/tolerance не менялись.
File sizes и world FPS overlays не доказывают matched CPU/GPU p95/memory.

## История, воспроизведение, оставшийся объём

V1 артефакты и отрицательный section verdict сохранены в соседней папке.
Raw v2 references переиспользованы из v1 только после exact executable/profile/
model/shader проверки; mtime captures сохранены. Aligned references сняты заново.
`analyze_creature_envelopes.py --reference-models` теперь явно записывает research
reference root. Default остаётся исходным snapshot; production tool от него не зависит.

```powershell
python tools/research/build_creature_whale_anatomy_pilot.py --qa-name whale_FRESH --variant v2 --source-root assetgen_output/creatures/whale_anatomy_pre_publish
python tools/research/audit_creature_whale_sections.py --qa-name whale_FRESH --out docs/research/whale_FRESH --source-root assetgen_output/creatures/whale_anatomy_pre_publish
```

Output для section audit должен существовать; native flights — через runner/
`flight_sim_run.py`, Release bin EXE, свежие labels/output. Pre-publish context
bin hashes после publication доказывать собственным snapshot, не новым live model.

Следующая motion-очередь: orc/puffin/badger/treeman. До финальной R6 whale ещё
нуждается в принятии girth/flipper/fluke формы, всей исходной выразительности,
habitat/terrain/contacts/lighting/camera/GPU fades/control/UI-pack. Общие user/world
compatibility, matched performance, FP framing/attack/walls/presets и clean package
обязательны для всех56+5/трёх backend. Все61 R6 pending, цель active.
Anatomy17 metrics исторические:12 own live и5 own snapshots (penguin/lobster/
oerkki/seahorse/whale) плюс original17 заново сверены SHA в
`../anatomy_shapes_v1/source_freshness_after_whale.json`; metrics не перезаписаны.
