# R5 — panda / warthog

`index.html` содержит сравнения при одинаковом мировом масштабе,
полные native-листы и отдельный лист сохранённого die бородавочника.
Baseline `b35f00a75` читается только research tooling; производство использует
наши YAML, палитры и экспортёр.

`native_candidate` — 29 кадров Release-приложения на вид: четыре ракурса,
восемь фаз основного движения, 64px иконка (12), восемь Run и восемь Action.
Финальные мировые доказательства — `runtime_close_panda/frame_001.png`
и `runtime_close_warthog/frame_001.png`. Контексты сохраняют hashes моделей,
definitions и executable. `runtime_wave` относится к промежуточной диагностике
до уплотнения ключей Action и не используется в финальной приёмке.

`metrics.json` воспроизводится командой:

```powershell
python tools/research/measure_creature_gltf_motion.py --species panda,warthog --qa-models assetgen_output/creatures/gltf_medium_v2/models --out docs/research/creature-gltf-medium-v2
```

Инструмент измеряет настоящий экспортированный skinning в 65 фазах, размеры,
контакт и effective clip periods с учётом runtime speed. Walk/Run/Punch не
проваливаются глубже 0.003407 блока. Для die — минимум −0.0011 блока;
контакт дополнительно проверен тестом с анимированным scale. Коррекция сохраняет
авторский горизонтальный выпад. `runtime_speed=1` даёт die период 1.2 секунды.

`warthog_die_asset.png` — sampled glTF asset capability, не кадры отдельного
runtime Death state. `review.json` — решение Codex, не подпись пользователя.
Performance gates во flight-запусках отключены; performance PASS не заявлен.
Общая R6-матрица и воспроизводимость всех 61 ресурсов текущим генератором
ещё предстоят.
