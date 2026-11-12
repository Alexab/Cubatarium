# Badger sway v1: сохранённый первый пилот

Изолированный own proposal, не опубликован. Скелет/mesh/UV/PNG и частота
конечностей сохранены; большой body roll заменён медленным ритмом2.45/2.475s.
Native листы и temporal GIF находятся в этой папке и `temporal/`.

**Не выбран для продолжения:** root contact128 samples/stride дал на2049 фазах
Run максимальный mesh floor gap0.001313355 блока против live0.000793921.
Поздний audit с explicit gap≤0.001 отвергает эту реальную версию:
[negative control](../badger_sway_v2/contact_negative_control.json).
Первоначальный `motion_invariant_audit.json` проверял нижнюю границу mesh,
rig/timing/seams, но ещё не ограничивал максимальный floor gap. Его `passed`
не является принятой contact/art/performance приёмкой и не отменяет этот отказ.

[V2](../badger_sway_v2/README.md) увеличивает только плотность вертикальной
root коррекции:256 steps/stride Walk,224 Run, сохраняя те же limb keys и sway.
Исходный builder v1 с совпадающим SHA сохранён в
assetgen_output/creatures/badger_sway_v1/build_creature_badger_sway_pilot.py;
текущий builder по умолчанию создаёт v2. Live recipe/models не менялись.
