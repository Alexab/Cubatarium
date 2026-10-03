# Survival integration matrix

Mode × difficulty × dig × melee × wear. See also [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md),
[CREATURE_STATS.md](CREATURE_STATS.md), [TECH_DEBT_SURVIVAL.md](TECH_DEBT_SURVIVAL.md).

## Enums

- `WorldGameMode`: `Creative` | `Survival` (persist `world_data.json` `game_mode`)
- `WorldDifficulty`: `Peaceful` | `Easy` | `Normal` (persist `difficulty`; missing → `normal`)

Creative ignores difficulty for vitals (frozen). Wear/combat still consult ModePolicy.

## Matrix

| Concern | Creative | Survival + Peaceful | Survival + Easy/Normal |
|---------|----------|---------------------|-------------------------|
| Needs / breath / fatigue tick | off | scaled (Peaceful ×0) | on |
| Melee Influence damage | cancel `"creative"` | on (aggro off — no hostile→player) | on |
| Dig progress | duration 0 (instant) | hardness/tool rules | hardness/tool rules |
| Hardness 0 dig | still breaks (instant) | unbreakable | unbreakable |
| Tool wear | off | off | on |
| Hostile aggro / melee on player | n/a (no combat) | off | on |
| Inventory | Creative unlimited | Owned | Owned |

## Tick order (`WorldViewBinding`)

1. Activity agents (write Influence Intent)
2. Resolve Influences (Melee + Dig) for NPCs then controlled one-shot
3. ExecuteIntent / camera move
4. `CreatureVitalsSystem::Tick`
5. `StatusEffectSystem::Tick`

## Wear

`IsToolWearEnabled` / `ModePolicy::AllowsToolWear`:

- Creative → false
- Survival + Peaceful → false
- else → true

Applies on dig complete **and** successful melee hit (same `ApplyItemWear`).

## Damage reasons (`CreatureVitalsSystem::ApplyDamage`)

| reason | Mitigation |
|--------|------------|
| `"influence"` | Pre-mitigated by `ResolveHitParams` / armor_groups — **no** flat `armor*0.5` |
| `"starve"` / `"drown"` / `"status"` / null | Flat `armor*0.5` still applied |

Creative: all ApplyDamage no-ops.
