# Art bible v0

Рабочий стиль первой поставки CreatureGen: «живой атлас». Это не отдельный фантастический бестиарий. Корова остаётся корова, но с собственными плечами, мордой и пятнами. Монстры получают более свободную массу.

Порядок: силуэт, затем 3–5 крупных зон, затем лицо. Мех и полосы идут вдоль части, не шумом по всему атласу. У вида один главный и два вторичных признака, записанные в `tools/creature_recipes/<id>.yaml`.

Палитры: `tools/creature_palettes.yaml` (`meadow`, `forest`, `river`, `peat`, `mineral`, `traveler`, `chitin`, `flame`).

Пилотные листы трёх концептов (сбоку и в три четверти):

- [cow](research/concepts/cow_concepts.png) — выбран `wedge_muzzle`
- [badger](research/concepts/badger_concepts.png) — выбран `digger`
- [rigid_demo_walker](research/concepts/rigid_demo_walker_concepts.png) — выбран `tuber`

Остальные виды используют свой рецепт и три масштаба массы (`primary`, `tall`, `long`). В сборку входит выбранный концепт. Runtime-ID не меняются.
