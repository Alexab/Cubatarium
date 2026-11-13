# Решение по каталогу предметов (tools_visual)

Дата: 2026-11-12. Ветка: `tools_visual`.

Исследование визуала и взаимодействия: [research/item-visual-interaction-2026-11-12.md](research/item-visual-interaction-2026-11-12.md).

## Принцип

Как у существ и растительности: знакомая роль Minecraft, своя геометрия и материал. Чужие CC0-меши (Kenney, KayKit, Quaternius) — только ориентир силуэта; production-сборка их не копирует.

## Каталог (оставить)

| Группа | Id |
|--------|-----|
| Инструменты | `{wood,stone,copper,iron,gold}_{pickaxe,axe,shovel,hoe,sword}` |
| Бой / утил | `wood_bow`, `arrow`, `wood_shield`, `iron_shield`, `wood_torch` |
| Броня (4 слота) | `{leather,copper,iron,gold}_{head,chest,legs,feet}` |
| Еда / жажда | `apple`, `bread`, `raw_meat`, `leather` (материал), `waterskin` |

Золото — быстрый хрупкий боковой тир (уровень добычи не выше камня), не тир выше железа.

## Убрать

Молотки / `wood_mallet`, нож / кинжал / копья, `tool_*_upgraded`, коса / пила / ключ / клещи / удочка / фонарь, слоты `arms`/`hands` и все `*_arms` / `*_hands`.

## Дроп с существ

Поле `drops` в `creature.json`. Выдача убийце в инвентарь (без сущности на земле).

- `raw_meat` ×1: cow, sheep, pig, chicken, bunny, warthog
- `leather` ×1: cow, sheep

## Вне исходного scope, затем возвращено

Дроп на землю и подбор входят в каталог. Волны расширения: utilities, затем food/farming, затем combat extras. См. [research/item-catalog-expansion-2026-11-12_RU.md](research/item-catalog-expansion-2026-11-12_RU.md).

