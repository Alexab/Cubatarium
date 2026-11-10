# C0. Effective baseline существ

Дата: 10 ноября 2026. Проверялись исходники в корне репозитория, без импорта Minecraft-текстур.

Машинный снимок: [creature-originality-effective-2026-11-10.json](creature-originality-effective-2026-11-10.json). Повтор:

```powershell
python tools/research/audit_creature_originality.py --output docs/research/creature-originality-effective-2026-11-10.json
python tools/validate_bedrock_creature.py
python tools/validate_gltf_creature.py --all
```

## Объём

- 56 видов: 17 `bone_skeleton`, 35 `gltf_skeleton`, 4 `rigid_voxels`.
- Естественно спавнятся 51. `catalog.spawnable` выставлен у 55. Единственный не-spawnable ID — `human`.
- `ListSpawnable()` берёт `catalog.spawnable`, поэтому сетка `CUBA_FLIGHT_QA_GRID` не включает человека. `bot` и три rigid demo в эту сетку входят. Галерея C3 обязана звать все 56 ID явно.
- Статические glTF: `butterfly`, `octopus`. Billboard: `fire_spirit`.
- Скинов 5, PNG скинов 26. PNG существ 275, включая наследие.

## Текстуры, которые реально выбираются

`visual.parts` у bone и glTF в аудите помечены `legacy_parts_ignored` и не считаются рантайм-стемом.

Запрошенный stem bone — `visual.texture` (`diffuse`). Если файла нет, цепочка как в `ResolveCreatureSpeciesTexture`: `diffuse`, затем `default_texture`, затем `body`.

| Вид | Файл `diffuse.png` | Фактический ключ |
|---|---|---|
| bunny, squid, trout | нет | `<id>/body` |
| остальные bone | есть | `<id>/diffuse` |
| fire_spirit | не требуется | `fire_spirit/body` |

UV этих трёх видов по-прежнему от атласа geo (у bunny 64×32). Подстановка `body.png` не чинит развёртку. Новый `diffuse.png` появится в волне вида, не загрузкой Minecraft.

Скины bone резолвятся в `skin/<id>/diffuse`, потому что этот PNG уже лежит в каталоге скина. `texture_map` (`torso`, `legs`, …) рантайм bone/glTF не читает.

Неразрешённых ключей нет.

## Валидаторы

`validate_bedrock_creature.py` обходит все 17 bone-видов из `creature.json`, включая `bot`, и копит ошибки. Результат: 14 OK, FAIL у bunny, squid, trout (`missing textures/diffuse.png`). Это ожидаемый baseline, не зелёный гейт.

`validate_gltf_creature.py --all`: 35/35 OK, включая проверку `materials[].name` против `textures/<stem>.png`.

`render_creature_preview_gltf.py` и `render_creature_preview_bedrock.py` помечены как wiring smoke: они пишут байты `b"BMP"`. Нулевой код выхода не является кадром.

## Пак и bin

`resource_packs/_example_creature_demo` меняет `display_name` свиньи и не подменяет geo/glTF. Кэши кости и glTF живут до перезапуска процесса.

Desktop читает `models/` из корня. `bin/models` — копия. `tools/clear_creature_visual_cache.py` не синхронизирует скины.

## Кадры

Игровые PNG этой ревизии складываются в `docs/research/c0-frames/` отдельным прогоном Release `bin/Cubatarium.exe` (`CUBA_FLIGHT_QA_GRID=1`, мир `World_164`). Маркеры `b"BMP"` в этот каталог не принимаются.
