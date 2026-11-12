# Orc sway: медленное покачивание с прежней частотой шага

Опубликована локальная **R5 motion revision**, current evidence — в `review/`.
Это решение ассистента по прямому просмотру, не user approval или финальная R6.
Все61 R6 pending. Предшествующая [семантическая диагностика четырёх видов](../motion_semantics_v2/README.md)
изменила интерпретацию исходных motion unions и план для puffin/badger/treeman.

## Что вернулось и что сохранено

У reference Orc весь mesh weighted к одному joint и медленно наклоняется;
реальное исходное плечо body sway отличается от собственного articulated rig.
Вместо переноса этой привязки на own добавлен body roll20° Walk/24° Run.
Чтобы он не повторялся на каждом быстром шаге, own clip теперь содержит4 stride
cycles в Walk и3 в Run на один slow sway. Прежние limb keys периодически
повторяются; они не растянуты по времени. При unit travelScale:

| Clip | Прежний stride period | Новый stride period | Новый sway period | Reference loop |
| --- | ---: | ---: | ---: | ---: |
| Walk |0.635526s|0.635526s|2.542103s|2.400s|
| Run |0.873739s|0.873739s|2.621216s|2.400s|

Настройки runtime speed/state map/physics/collider прежние. Actual world
playback зависит также от travelScale. Сохранены все18 суставов, hierarchy,
IBM, все geometry/UV/indices/skin attributes и PNG bytes.360 triangles и
atlas128×128 прежние. Idle/Action22 samplers (44 arrays) точны;30 соответствующих
envelope PNG12 Idle+18 Action совпали с own-before.

**World-space траектории конечностей меняются** из-за нового родительского roll
и root clearance. Сохранена их локальная кинематика при одинаковом elapsed
time:1025 Walk/769 Run samples дали angular error максимум0.00007156°,
остальные local translations и root XZ точны. Root Y заново выпечен по полному
skinned mesh, не только nominal foot boxes. C0 local loop seams проверены;
это не обещание C1 velocity continuity или отсутствия foot sliding.

Ключи лежат в собственной canonical recipe; обычный CreatureGen build/validate/
publish их воспроизводит. В runtime не добавлялись layers/процедурный sway.
Длинный clip нужен из-за единого ClipClock и содержит repeated stride data.
BIN вырос **131400→313200 bytes**. Это сознательная стоимость baked motion;
matched memory/load/pose CPU/GPU p95 gate остаётся открытым и назначен следующим.

## Что действительно просмотрено и проверено

Прямо просмотрены3 native original/own-before/candidate sheets,3 full-turn
листа и actual world full frame. После публикации просмотрен contact sheet20
world frames (`review/production/world_contact_sheet.png`); стартовый frame000
предшествует QA relocation и не является art reference. SHA остальных файлов
перечислены в `visual_review.json`.

В comparison sheets phase нормирован на **весь** clip: новая macro phase.25
не равна прежней stride phase.25 при одинаковом elapsed time. Поэтому проверка
сохранения ног сделана отдельно по времени в `motion_invariant_audit.json`.
Raw reference сохраняет свой yaw0, own использует180; source front/back views
не объявлены семантически выровненным anatomy comparison. Эта ревизия меняет
собственную пластику корпуса, а не принимает всю форму/материал исходника.

Native168 основных original/current PNG согласованы с offline skinning,
max bounds error0.00006471 блока. Old collider-based false cull1+1 восстановлены
conservative envelope.2156 CPU transition frames,2112 supported, minY0,
envelope covered. Это sampled retained poses, не все GPU fades/terrain routes.
Khronos35 QA glTF:0 errors/warnings/infos.

Normal pipeline и `review/publication_audit.json` PASS:

- 5290 PNG:5234 exact к предыдущей whale production,56 изменений только Orc.
  Все89 candidate→production RGBA exact; EXE/shaders прежние.
- 936 FP PNG exact; independent authored GPU arm audit1872 draws/6 retained
  cache cases PASS.128 entity cases/256 actual restores PASS.
- Все61 R5 ledger fresh. Own16-file pre-sway snapshot сохранён;
  regeneration glTF semantic и BIN/PNG/recipe byte exact.
- 15 targeted Release tests PASS после публикации. Fresh bounds audit FAIL17,
  196 CTest зарегистрировано; весь CTest не объявлен зелёным.

Release EXE остаётся
`235ce58dd65546f38164020295ff403bf48c2816b14bb16cd96228faee030483`.
C++/CMake на этом этапе не менялись; desktop rebuild не требовался.

## История проверок и строгая metadata parity

Первый world run с B-only `--world-gear` отверг Orc/G до native export.
Этот failed run находится в корне, не принят. Успешные captures сняты со
свежими labels в `review/` без неподходящего fixture. G equipment/control
совместимость этим не принята и остаётся отдельной R6 задачей.

Первоначальный строгий body parity отказал: matrix `time_seconds` и
`primary_period_seconds` изменились ровно в4 раза для20 Walk кадров Orc.
Это ожидаемая длительность macroloop при тех же normalized poses. Новый
явный `--expected-state-time-factor orc:Walk:4` проверяет только этот фактор;
phase/state/owner/backend/view/skin/остальные resource metadata остаются exact.
Неверный2× factor отвергнут независимым negative control. Publication verifier
дополнительно требует именно20 таких frames и один4× primary period.
Начальный отказ не был засчитан как successful parity и не отменён общим waiver.

## Воспроизведение и открытые gates

```powershell
python tools/research/build_creature_orc_sway_pilot.py --qa-name orc_sway_FRESH --source-root assetgen_output/creatures/orc_sway_pre_publish
```

Native flights — через `run_creature_pilot_qa.py` / `flight_sim_run.py`, Release
bin EXE, свежие output/labels. Старые bin hashes проверять по own snapshot,
не текущим опубликованным файлам. `motion_invariant_audit_initial.json` сохранён
до усиления source/loop-seam provenance; окончательный report использует snapshot.

Далее matched performance из-за выросшего animation buffer, затем отдельные
badger/treeman slow-sway proposals и puffin Fly posture/folding/cadence comparison.
Orc ещё требует world stance/stride/sliding/terrain/AI/action/control/equipment-G,
continuous GPU fades/camera/light/UI-pack и полной motion acceptance.
Общие user/world compatibility, FP framing/attack/walls/presets и clean package
остаются обязательными для всех56+5/трёх backend. Цель active, все61 R6 pending.
