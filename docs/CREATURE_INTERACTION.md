# Creature interaction (Influence)

Contract for creature↔creature and creature↔block influence via one pipeline.
Optional tool mediation and presentation hooks.

Related: [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md),
[SURVIVAL_INTEGRATION.md](SURVIVAL_INTEGRATION.md),
[CREATURE_STATS.md](CREATURE_STATS.md), [CREATURE_AGENTS.md](CREATURE_AGENTS.md),
[ITEMS_TOOLS.md](ITEMS_TOOLS.md), [TECH_DEBT_INFLUENCE.md](TECH_DEBT_INFLUENCE.md),
[CODING_STYLE.md](CODING_STYLE.md).

## Pipeline

```
ActivityAgent / PlayerInteractionRouter
  → CreatureIntent.Influence (Channel Melee | Dig | Use stub | …)
  → IUToolInfluenceProvider (UItemToolInfluenceProvider) or bare-hand fallback
  → InfluenceResolver
       Melee → ResolveHitParams (damage_groups × armor_groups)
       Dig   → ResolveDigParams (groupcaps × block dig groups)
  → InfluenceApplier (vitals / DigSessionState+DelBlock / wear / status) + InfluenceEvent
  → FX sinks (creature flash/beam; dig crack/break)
```

Creative: Melee resolve cancels; Dig uses duration 0 (instant). See ModePolicy.

Tick order (WorldViewBinding): activity → Influence resolve (melee+dig, incl. controlled)
→ ExecuteIntent / camera → vitals tick → status tick.

## Dual groups (decision A)

- **Damage groups** on tool capability: e.g. `{ fleshy: 8 }` — combat only
- **Armor groups** on creature: default `{ fleshy: 100 }`; `immortal > 0` cancels hit
- **Dig groups** on blocks × tool `groupcaps`: e.g. `cracky` / `choppy` — mining only

Do not merge dig group names into armor/damage tables (TD-INF-003).

Hit formula (melee): `Σ damage[g] * intervalMul * (armor[g] / 100)` via `ResolveHitParams` /
`InfluenceHitMath`. Melee gates on `FullIntervalSec`.

## Tools handshake

Interface: `IUToolInfluenceProvider` (`src/Creatures/Influence/IUToolInfluenceProvider.h`).

| Owner | Responsibility |
|-------|----------------|
| Influence | Types, resolve/apply, events, DigSessionState, `UBareHandToolInfluenceProvider` |
| Items | Item defs, `UItemToolInfluenceProvider`, `ResolveDigParams` / `ResolveHitParams`, wear |

Combat facade `CreatureCombat::TryMeleeStrike` constructs `UItemToolInfluenceProvider` and
runs Resolve→Apply. Dig uses the same bus with `Channel::Dig` (shipped).

## Channels

| Channel | Use |
|---------|-----|
| `Melee` | Punch / bare hand / melee weapon vs creature |
| `Dig` | Break block (session progress → DelBlock + wear) |
| `Ranged` | Future projectiles |
| `Aura` | Radius group influence (`InfluenceTargeting::Radius`) |
| `Use` | Reserved stub (`use_unimplemented`); social UI = TD-INF-004 |
| `None` | No influence this tick |

Punch/Dig must not share the social right-click path (TD-INF-004).

## Status effects

- Runtime list on `UCreature`; defs in `UStatusEffectCatalog` (builtins `bleed`, `slow`)
- Sample / runtime JSON: `models/effects/*.json`
- Tick via `StatusEffectSystem::Tick`; move speed via `GetMoveSpeedMultiplier`

## EffectSpec / VFX

`EffectSpec` is presentation-only. `UInfluenceFxSystem` listens to `InfluenceEvents`
and drives hit flash plus path beams / bursts (`UInfluenceFxPass`). Dig crack/break FX
subscribe to Dig events (or DigSessionState during transition).

## Player input

Survival: LMB on creature → Melee Intent; LMB on block → Dig Intent (via router).
WorldViewBinding resolves then clears one-shot melee intent; Dig session may persist while held.

## Intent fields

`CreatureIntent::Influence` holds channel, target creature id / block pos, action id.
`attackTargetId` is transitional; prefer Influence only. Resolver treats legacy
`attackTargetId != 0` with `Channel == None` as melee single-target until Wave 4 cleanup.
