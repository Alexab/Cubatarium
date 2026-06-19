# Ручной чеклист спавна (50 spawnable видов)

Используйте после runtime-фиксов A1–A4. Отмечайте в `tools/creature_resolution_log.yaml` при расхождении.

## Для каждого вида

1. Подходящий биом (см. группы ниже)
2. Палитра: слот **не dimmed**, tooltip без ошибки
3. **V** — моб появляется ~3 м перед игроком
4. Позиция: ноги на земле / тело в воде / в воздухе
5. Неподходящий биом — dimmed + корректная подсказка

## Группы

| # | Сценарий | Виды |
|---|----------|------|
| 1 | Ровная трава | hedgehog, rat, spider, kitten, badger, bunny, tortoise, chicken, fox, wolf, pig, warthog, panda, sheep, cow, skeleton, oerkki, orc, golem, ogre, treeman, sand_monster, stone_monster, tree_monster, dirt_monster, dungeon_master, land_guard, mese_monster |
| 2 | Океан ≥4 блоков | trout, crab, lobster, hermitcrab, seahorse, stingray, shark, whale, water_dragon, manatee, dolphin, squid, octopus |
| 3 | Берег + вода | penguin, seal |
| 4 | Открытое небо | bee, butterfly, wasp, owl, puffin, fire_spirit |
| 5 | Лавовое озеро | lava_flan |

Автоматическая матрица: `docs/CREATURE_SPAWN_MATRIX.md`, журнал: `docs/CREATURE_RESOLUTION_LOG.md`.
