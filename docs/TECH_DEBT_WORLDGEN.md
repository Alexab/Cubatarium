# Worldgen — закрытый план (data-driven)

> Формат runtime-данных в `content/`: **только JSON** (парсер `nlohmann::json` в движке).
> YAML остаётся в `tools/` для authoring (manifest, canonical blocks) и не читается игрой.

## Политика форматов

| Область | Формат | Кто читает |
|---------|--------|------------|
| `content/worldgen_packs/**` | JSON | C++ (`UWorldGenPack`) |
| `content/prefab_features.json` | JSON | C++ |
| `tools/prefab_manifest.yaml` | YAML | Python (генерация) |
| `tools/*.yaml` | YAML | Python / CI |

Дублирование `prefab_features.yaml` **не используется** — артефакт генератора удалён.

## План (закрытие техдолга)

| # | Задача | Статус |
|---|--------|--------|
| 1 | `pipeline.json` loader + пересечение стадий с генератором | done |
| 2 | Hot-reload: `/worldgen reload` (pack + prefab_features) | done |
| 3 | UI + JSON: `worldgen_pack_id` | done |
| 4 | Biome JSON: `palette`, `sub_biomes` (веса + subsurface) | done |
| 5 | `pack.json` optional `biome_blend_radius` default | done |
| 6 | Убрать `pipeline.yaml`, `prefab_features.yaml` из content | done |
| 7 | Валидация prefabs / prefab_features в CI (`smoke_resource_packs.py`) | done |

## Ограничения hot-reload

Перезагрузка влияет только на **вновь генерируемые** чанки. Уже загруженные колонки не пересчитываются автоматически.

## Дальнейшие улучшения (не блокеры)

- Выбор pack из выпадающего списка с описанием (сканирование `pack.json`)
- Полный перенос `SubBiomeFor` noise-порогов в JSON
- Доп. UX: пресеты генераторов и tooltips для чекбоксов стадий Overworld
- ~~Scatter/prefab на воде~~ — `CanPlacePlantAt` / локальный `FindTopSolidSurfaceY` (done)
