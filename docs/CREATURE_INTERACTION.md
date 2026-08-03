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
  → InfluenceResolver (range, interval, groups math)
  → cancelable hooks
  → InfluenceApplier (vitals / attributes / status)
  → InfluenceEvent → VFX (source / target / path)
```

Creative mode: resolve no-ops (parity with existing combat).

## Groups (Luanti-inspired)

- **Damage groups** on capability: e.g. `{ fleshy: 8 }`
- **Armor groups** on target: default `{ fleshy: 100 }`; `immortal > 0` cancels hit
- Formula: `Σ damage[g] * clamp(dt / full_interval, 0..1) * (armor[g] / 100)`

See `InfluenceHitMath::Compute`.

## Tools handshake

Interface: `IUToolInfluenceProvider` (`src/Creatures/Influence/IUToolInfluenceProvider.h`).

| Owner | Responsibility |
|-------|----------------|
| Influence (this work) | Types, resolve/apply, bare-hand `UBareHandToolInfluenceProvider` |
| Tools agent | Item defs, wielded capability, durability / `PunchAttackUses` |

Until tools land, melee uses strength-scaled bare-hand damage (parity with
`CreatureCombat::ComputeMeleeDamage`).

## Channels

| Channel | Use |
|---------|-----|
| `Melee` | Punch / bare hand / melee weapon |
| `Ranged` | Future projectiles |
| `Aura` | Radius group influence |
| `Use` | Tool `on_use` style (tools agent) |
| `None` | No influence this tick |

Punch/influence must not share the social right-click path (see TD-INF-004).

## EffectSpec

`EffectSpec` describes presentation only (particle/sound ids, flash, path style).
Combat math must not depend on Render. VFX consumers subscribe to `InfluenceEvent`.

## Intent fields

`CreatureIntent::Influence` holds channel, target id/point, action id.
`attackTargetId` remains for transitional agent code; resolver treats
`attackTargetId != 0` with `Channel == None` as melee single-target.
