# R6 resource reload: текущая evidence

[Описание, метод и ограничения](README.md).

| Evidence | Результат |
|---|---|
| [Native reload audit](resource_reload_audit.json) | 935 scenarios, GPU RGBA720, current files verified |
| [Отдельный процесс](resource_reload_restart_audit.json) | Ещё935 scenarios и GPU RGBA720 |
| [Сохранность изображений](body_parity.json) | 5290/5290 exact к предыдущему EXE |
| [Restart pixel parity](restart_body_parity.json) | 5290/5290 exact между процессами |
| [Release tests](release_tests.log) | 11/11 |
| [Ledger](ledger.json) | 61/61 |
| [Visual observations](visual_review.json) | Exact preview inheritance + два world frames |

Directly inspected world frames после probe и восстановления ресурсов:

![Первый процесс](runtime_reload_catalog/frame_002.png)
![Restart](runtime_reload_restart_catalog/frame_002.png)

Original/current model sheets и sampled art observations:
[bone-motion](../bone-motion/index.md), [equipment](../equipment-fit/index.md).
Копии sheets не создавались: native input RGBA всех61 точны.

FP/save/world bootstrap, остальные motion/route/habitat/culling, clean package
и matched performance ещё обязательны. Полная R6 остаётся pending.


Продолжение FP authored-arm/source subset: [fp-save](../fp-save/index.md).
Save/world-camera приёмки остаются pending; старые reload contexts выше сохранены.
