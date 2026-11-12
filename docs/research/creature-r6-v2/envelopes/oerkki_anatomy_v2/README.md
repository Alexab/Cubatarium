# Oerkki: локальное восстановление массы корпуса и конечностей

R5 revision опубликована обычным build/validate/review/publish. Это прямое
решение ассистента по рассмотренным материалам, не user approval и не полная R6.
Все56 существ+5 скинов остаются в цели; все61 R6 pending.

## Почему меняется форма

У own и original близки габариты, но руки/ноги own тоньше в профиль. На bind
геометрии корпус имеет глубину4 texels, руки/ноги3. При этом own постановка
ног уже шире исходной: расширение всей модели по AABB ухудшило бы пропорции.
Сопоставлены независимые triangle silhouettes и реальные native кадры на одном
world scale. Original rows почти чёрные: его source body RGB0..4, face RGB0..3.
Эти материалы доказывают silhouette сильнее, чем детали поверхности; matching
оригинальной выразительности движения ими не доказан.

В собственном рецепте:

- Core: ширина6→7, глубина4→5. Chest: глубина4→5.
- Руки: ширина2→3, глубина3→4; ноги: только глубина3→4.
- Голова: ширина6→7, taper .9/.92→.96/.96.
- Размеры изменены симметрично относительно прежних центров. Суставы,
  hierarchy, inverse binds, JOINTS/WEIGHTS и все62 sampler-массива точны.
  Длина, постановка, руки/кисти/когти и подвижная челюсть сохранены.
- UV выделены заново по размерам, собственный surface material перезапечён.
  Палитра и алгоритм материала не изменены, original pixels не используются.

Фронтальная GPU alpha площадь в Idle к original:81,4%→92,6%; боковая
77,5%→87,4%; oblique84,9%→92,2%. Это площадь проекции, не physical volume
и не самостоятельный художественный gate. Решение принято после просмотра
трёх comparison sheets, трёх full-turn sheets и игрового кадра; SHA перечислены
в `visual_review.json`. Дополнительно непосредственно просмотрен production
`production/runtime_production_final/frame_002.png` без model override.

## Что проверено

`rig_uv_native_audit.json`: nodes/skin/IBM/weights/definition сохранены,
62 sampler arrays exact,300 triangles как прежде.128×128 atlas и149712-byte
BIN не выросли. В обоих вариантах48 constant-UV triangles: это прежние
однотексельные поверхности, которые exporter сжимает к texel centres.
Нулевой p05 texel density здесь не следует объявлять новым texture defect либо
полной texture acceptance. Неопределённое качество future closeups остаётся R6.

196 cold-entry transition cases,2156 CPU frames,2112 supported frames:
supported minY0, envelope покрывает все sampled geometry. Это не GPU fade,
terrain/stride/sliding/all-history proof. Native126 original/current основных
PNG согласованы с offline skinning, max bounds error9,775e−6 блока. Один
старый collision-culling false reject у own восстановлен visual envelope.
Khronos35 QA glTF:0 errors/warnings/infos.

`publication_audit.json` подтверждает точные recipe/staging/models/bin,
review bundle, snapshot и capture inputs:

- Полная матрица5290 PNG:5201 exact к предыдущей lobster production,
  89 ожидаемо изменённых только oerkki. Все89 candidate→production RGBA exact.
- 936 FP PNG exact к предыдущей production; независимый authored-arm audit
 1872 GPU draws/6 retained cache cases PASS.
- Legacy/v2 entity persistence128 cases/256 actual restores PASS.
- Все61 R5 ledger записи свежие. Опубликованные definitions/physics/state map
  и playback speeds прежние; compatible authored oerkki skins отсутствуют.

Повторная генерация из собственного `oerkki_anatomy_pre_publish` snapshot
в `oerkki_anatomy_v2_repro` дала glTF semantic и BIN/PNG/recipe byte exact.
Snapshot16 файлов содержит прежние own model/staging/recipe/contracts/catalog/
global manifest. Production builder не обращается к research original.

Release EXE остаётся
`a0872e87a1551dbe7ac0cff1346b58176743d18eba13817cb7779473415df68f`.
C++/CMake на этом этапе не менялись, desktop rebuild не требовался.
После публикации15 targeted Release tests PASS. Fresh bounds audit FAIL17,
196 CTest зарегистрировано; полный CTest не объявлен зелёным.
Gameplay collider/tolerance ради совпадения с gait не менялись.

## История попыток и инструментов

Первый scratch `oerkki_anatomy_v1` остановился до экспорта: дробные texel sizes
запрещены генератором. Папка сохранена как неготовая; это не рассмотренный art
candidate. V2 использует целые размеры и разрешённые половинные origins.

`audit_creature_anatomy_pilot.py` обобщён параметрами species/states, прежний
penguin default сохранён; добавлен явный sampled transition audit.
`compose_creature_anatomy_fullturn.py` формирует листы из native manifest с
проверкой executable/profile/model/shader inputs. Generic publication audit
проверяет geometry-only revisions независимо от export.

Первая ручная дополнительная contact-команда ошибочно ожидала0 constant UV
triangles; в исходном own уже48, столько же в candidate. Корректный gate
проверяет отсутствие роста и реальные UV/texture, не маскирует inherited число.
FP audit сначала получил authored production вместо требуемого исторического
legacy renderer reference и закономерно отказал. Финальный independent FP audit
сравнен с `fp-save/native_before`; authored-to-authored936 RGBA отдельно проверены
publication audit. Первоначальный publication audit остановился до вердикта,
пока independent save report не был сформирован; финальный PASS получен после
него. Эти промежуточные ошибки не засчитаны как успешные проверки.

FP verifier теперь умеет сверять изменившиеся historical model inputs по явно
переданному snapshot без переписывания context. Current inputs всегда live.
Это не разрешение подменять historical renderer; legacy arm root reference
по-прежнему проверяется. Для данного финального FP audit snapshot не потребовался.

## Воспроизведение и оставшиеся gates

```powershell
python tools/research/build_creature_oerkki_anatomy_pilot.py --qa-name oerkki_FRESH --source-root assetgen_output/creatures/oerkki_anatomy_pre_publish
```

Native captures выполнять через `run_creature_pilot_qa.py` / `flight_sim_run.py`
на Release bin EXE, со свежими output/labels. Контексты own-before ссылаются на
прежний bin: после publication их SHA доказываются pre-publish snapshot,
не новым live payload. Не перезапускать pre-publish verifier на сменившемся bin
с заявлением, что это те же входы.

Следующие anatomy задачи: seahorse body/crest, whale profile. Oerkki требует
R6 terrain/stance/stride/AI/attack, непрерывных GPU transitions, lighting/camera,
control/UI-pack и matched performance. Общие FP framing/walls/presets,
clean package, совместимость пользовательских миров и все backend gates открыты.
Размер atlas/BIN и отдельные world FPS overlays не являются performance proof.

[Khronos glTF2.0 skins/animations](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
— контракт joint/IBM и animation target properties. Локальные сравнения
экспортированных arrays и native poses дополняют schema validation.
