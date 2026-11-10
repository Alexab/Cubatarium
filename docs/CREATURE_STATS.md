# Creature stats (vitals & attributes)

Canonical schema for player, bots, and all mobs. Runtime state lives on
`UCreature`; definition defaults come from templates + optional `creature.json`
blocks.

Combat / status changes go through the **Influence** pipeline — see
[CREATURE_INTERACTION.md](CREATURE_INTERACTION.md) and
[TECH_DEBT_INFLUENCE.md](TECH_DEBT_INFLUENCE.md). Flat `armor` still mitigates
in `ApplyDamage`; group ratings live on `UCreature::GetArmorGroups()` (default
`fleshy=100`).

## Vitals

| Field | Meaning |
|-------|---------|
| health / max_health | Hit points |
| satiety / max_satiety | Hunger (drains in Survival) |
| thirst / max_thirst | Thirst (drains in Survival) |
| fatigue / max_fatigue | 0 = rested; high blocks sprint |
| breath / max_breath | Underwater air |
| armor | Flat melee mitigation |
| fatal_wounds / max_fatal_wounds | Deaths before permanent fail |

## Attributes (1–20)

strength, agility, endurance, accuracy, intelligence, luck, perception

## Templates (`CreatureStatsDefaults`)

| Role / tags | max HP | max fatal | needs tick |
|-------------|--------|-----------|------------|
| controlled_default / bot | 100 | 3 | yes |
| hostile humanoid | 40–80 | 1 | no |
| predator | 30 | 1 | no |
| passive animal | 16–40 | 1 | no |
| aquatic / aerial | 12–50 | 1 | no |

## Game modes

- **Creative** (default): vitals frozen, immortal, `InventoryMode::Creative`, creative double-space fly allowed
- **Survival**: needs tick, damage, owned inventory; **no creative fly** (aerial habitat keeps flight caps)

Persisted as `game_mode` in `world_data.json`. Toggle: New World UI or
`/gamemode creative|survival`. `/fly` is blocked in Survival.

## Survival difficulty

Separate from game mode. Persisted as `difficulty` in `world_data.json`
(`peaceful` / `easy` / `normal`). Missing key → `normal`. Creative ignores
difficulty. Toggle: New World UI (Survival only) or `/difficulty peaceful|easy|normal`.

Base Survival rates (Normal = ×1):

| Need | Base rate |
|------|-----------|
| Satiety drain | 0.15 / sec |
| Thirst drain | 0.2 / sec |
| Breath drain (in water AABB) | 6 / sec |
| Breath recover (out of water) | 20 / sec |
| Drown / starve damage | 2 / sec when breath or satiety/thirst ≤ 0 |

| Difficulty | Needs drain | Fatigue gain | Breath drain | Drown |
|------------|-------------|--------------|--------------|-------|
| Peaceful | ×0 | ×0 | ×0 (breath stays full) | off |
| Easy | ×0.5 | ×0.5 | ×0.25 | on |
| Normal | ×1 | ×1 | ×1 | on |

See also [`TECH_DEBT_SURVIVAL.md`](TECH_DEBT_SURVIVAL.md).

## Fatigue (Survival only)

Fatigue starts at **0** (rested) and rises toward `max_fatigue`.

| Situation | Rate (base, Normal) |
|-----------|---------------------|
| Sprint (Ctrl + move, on ground) | +8 / sec |
| In water | +5 / sec (stacks with sprint) |
| Standing / walking / idle | −4 / sec recover |

Endurance scales gain: higher END → slower fatigue build-up.
At ≥95% fatigue sprint is gated off. **Creative** freezes all vitals (no drain).
Peaceful disables fatigue gain; Easy uses ×0.5.

Satiety / thirst drain while Survival (see difficulty table) even when idle.

## UI

- Character sheet: key `c` (`UiSettings.CharacterKey`); shows mode + difficulty
- Survival HUD bars: HP / Food / Water / Fatigue / Breath on `InGameHudScreen`

## Bots

Species `bot`, role `bot`, behavior `bot_player` (`BotPlayerActivityAgent`).
Uses human skins (`human_*` compatible with `bot`). Spawn via palette or
`spawn bot`.

## JSON (optional on definition)

```json
"vitals": { "max_health": 100, "max_fatal_wounds": 3, "armor": 0 },
"attributes": { "strength": 10, "perception": 10 }
```

Instance persist: `creatures.json` / `users.json` blocks `vitals` + `attributes`.
