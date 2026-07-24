# План доработки мобов (долг после creature2)

Обновлено: 2026-06-19. Журнал: [`tools/creature_resolution_log.yaml`](../tools/creature_resolution_log.yaml).

## Статус по осям

| Ось | Готово | Осталось | Блокер |
|-----|--------|----------|--------|
| **Спавн (код)** | A1–A4 + hotfix occupy | In-game 50/50 | ручной прогон |
| **Движение (код)** | wander + HabitatAllowsMovementAt | In-game aerial×6, aquatic | ручной прогон |
| **Визуал** | 19/50 silhouette pass | 31 pending | research / bake / parts |
| **Placeholder** | 0/8 | 8 видов | нет PNG в research |

## Критические исправления (эта сессия)

1. **`SpawnCreature` отклонял aerial** — `CanCreatureOccupyAt` требовал `inOpenAir` (строже pre-check). Заменено на `HabitatAllowsAtForSpawn` + коллизия.
2. **Terrestrial snap** — Y брался из колонки прицела, XZ из другой точки. Теперь `QueryGroundFeetYColumn` по XZ probe.
3. **Aquatic snap** — позиция телепортировалась в индекс блока fluid. Теперь сохраняется XZ прицела.
4. **Aerial движение** — `HabitatAllowsMovementAt` только коллизия + не fluid; wander fallback при провале probe.
5. **Мелководье** — порог глубины воды 0.55→0.35 от высоты тела.

## Фаза 1 — Runtime verify (обязательно, ~2–4 ч)

Прогон [`CREATURE_MANUAL_SPAWN_CHECKLIST.md`](CREATURE_MANUAL_SPAWN_CHECKLIST.md):

1. Для каждого из 50 spawnable: спавн V + 5 с wander.
2. Обновить `creature_resolution_log.yaml`: `spawn`/`wander` = pass|fail (не «по коду»).
3. Обновить `CREATURE_SPAWN_MATRIX.md` колонку `manual_ingame`.
4. `python tools/sync_creature_resolution.py --require-spawn-pass` только после реального pass.

**Репрезентанты по habitat:**

| Habitat | Виды для smoke |
|---------|----------------|
| terrestrial | chicken, wolf, rat, ogre |
| aquatic | trout, lobster, whale |
| aerial | bee, fire_spirit |
| amphibious | penguin, seal |
| lava | lava_flan |

## Фаза 2 — Placeholder арт (8 видов, TD-CRE-021)

Нужны PNG в `E:/Work/Home/CubatariumTextureResearch`:

| id | источник в yaml |
|----|-----------------|
| dolphin, whale, octopus, water_dragon | animalworld |
| kitten, warthog | mobs_animal |
| mese_monster, lava_flan | mobs_monster |

```bash
# после копирования текстур
python tools/bake_rigid_creature_textures.py --species dolphin
# … для каждого
python tools/audit_creature_catalog.py
```

## Фаза 3 — Арт P3–P5 (31 вид silhouette pending)

### P3 Marine UV (остаток)
manatee, squid, trout, stingray, hermitcrab, dolphin*, octopus*, whale*, water_dragon*

### P4 Quadruped template → custom/b3d
bunny, rat, panda, warthog*, kitten*

### P5 Biped monsters template
dirt/stone/tree monster, orc, ogre, golem, treeman, skeleton, dungeon_master, land_guard, mese_monster*, oerkki, sand_monster, fire_spirit

### Aerial
fire_spirit — кастомные parts + bake (единственный aerial без art-pass)

Пайплайн на вид:
1. `creature_rigid_parts.yaml` → `sync_creature_parts_from_rigid.py`
2. `creature_luanti_sources.yaml` manual_uv / model
3. `bake_rigid_creature_textures.py --species <id>`
4. `audit_creature_catalog.py` → обновить resolution log

## Фаза 4 — CI / smoke

- `smoke_creature_habitat.py` — habitat JSON
- `sync_creature_resolution.py --require-spawn-pass` — после фазы 1
- Опционально: headless spawn probe (будущее)

## Критерии закрытия долга

- [ ] 50/50 spawn `manual_ingame` = pass
- [ ] 50/50 wander подтверждён в игре
- [ ] 0 placeholder (`asset_tier` ≠ placeholder)
- [ ] silhouette/icon pass ≥ 45/50 (допустим defer 5 редких)
- [ ] `issues_open` = 0 или явный defer с note в yaml

## Не в scope

- glTF backend (TD-CRE-001)
- Flee/Melee agents (TD-CRE-008)
- Полный b3d animation playback (TD-CRE-006)
