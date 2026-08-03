# Creature interaction (Influence)

Contract for creature→creature (and group) influence: characteristic changes,
optional tool mediation, and presentation hooks.

Related: [CREATURE_STATS.md](CREATURE_STATS.md), [CREATURE_AGENTS.md](CREATURE_AGENTS.md),
[TECH_DEBT_INFLUENCE.md](TECH_DEBT_INFLUENCE.md), [CODING_STYLE.md](CODING_STYLE.md).

## Pipeline

```
ActivityAgent / PlayerInput
  → CreatureIntent.Influence (+ legacy attackTargetId)
  → IUToolInfluenceProvider (or bare-hand fallback)
  → InfluenceResolver (range, interval, groups math, optional radius)
  → InfluenceApplier (vitals / status) + InfluenceEvent
  → UInfluenceFxSystem / UInfluenceFxPass (source flash, target flash, path)
```

Creative mode: resolve no-ops (parity with existing combat).

Tick order (WorldViewBinding): activity agents → melee resolve (incl. controlled)
→ ExecuteIntent / camera move → vitals tick → status tick.

## Groups (Luanti-inspired)

- **Damage groups** on capability: e.g. `{ fleshy: 8 }`
- **Armor groups** on target: default `{ fleshy: 100 }`; `immortal > 0` cancels hit
- Formula: `Σ damage[g] * clamp(dt / full_interval, 0..1) * (armor[g] / 100)`
- Melee strikes gate on `FullIntervalSec` (anti-spam); see `InfluenceHitMath::Compute`

## Tools handshake

Interface: `IUToolInfluenceProvider` (`src/Creatures/Influence/IUToolInfluenceProvider.h`).

| Owner | Responsibility |
|-------|----------------|
| Influence | Types, resolve/apply, `UBareHandToolInfluenceProvider` |
| Tools agent | Item defs, `UItemToolInfluenceProvider`, durability / wear |

Until tools fully land, melee uses strength-scaled bare-hand damage (parity with
former `CreatureCombat::ComputeMeleeDamage`). Combat facade still calls bare-hand;
wire item provider when Items ownership is ready.

## Channels

| Channel | Use |
|---------|-----|
| `Melee` | Punch / bare hand / melee weapon |
| `Ranged` | Future projectiles |
| `Aura` | Radius group influence (`InfluenceTargeting::Radius`) |
| `Use` | Tool `on_use` style (tools agent) |
| `None` | No influence this tick |

Punch/influence must not share the social right-click path (TD-INF-004).

## Status effects

- Runtime list on `UCreature`; defs in `UStatusEffectCatalog` (builtins `bleed`, `slow`)
- Sample JSON: `models/effects/*.json` (authoring; runtime load = TD-INF-011)
- Tick via `StatusEffectSystem::Tick`; move speed via `GetMoveSpeedMultiplier`

## EffectSpec / VFX

`EffectSpec` is presentation-only. `UInfluenceFxSystem` listens to `InfluenceEvents`
and drives hit flash (`UCreature::AddHitFlash`) plus path beams / bursts drawn by
`UInfluenceFxPass` (no Weather particle budget mix).

## Player input

Survival: LMB with a creature under the crosshair sets `attackTargetId` /
`Influence` on the controlled creature; WorldViewBinding resolves then clears
the one-shot intent.

## Intent fields

`CreatureIntent::Influence` holds channel, target id/point, action id.
`attackTargetId` remains for transitional agent code; `SyncInfluenceFromAttackTarget`
keeps both in sync. Resolver treats `attackTargetId != 0` with `Channel == None`
as melee single-target.
