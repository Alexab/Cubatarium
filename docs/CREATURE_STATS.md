# Creature stats (vitals & attributes)

Canonical schema for player, bots, and all mobs. Runtime state lives on
`UCreature`; definition defaults come from templates + optional `creature.json`
blocks.

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

## Fatigue (Survival only)

Fatigue starts at **0** (rested) and rises toward `max_fatigue`.

| Situation | Rate (base) |
|-----------|-------------|
| Sprint (Ctrl + move, on ground) | +8 / sec |
| In water | +5 / sec (stacks with sprint) |
| Standing / walking / idle | −4 / sec recover |

Endurance scales gain: higher END → slower fatigue build-up.
At ≥95% fatigue sprint is gated off. **Creative** freezes all vitals (no drain).

Satiety / thirst drain slowly while Survival (~0.15–0.2 / sec) even when idle.

## UI

- Character sheet: key `c` (`UiSettings.CharacterKey`)
- Survival HUD bars: HP / Food / Water / Fatigue on `InGameHudScreen`

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
