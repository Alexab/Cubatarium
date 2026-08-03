# Interaction architecture (Influence bus)

Canonical design for tools × blocks × creatures after the deep refactor.
Related: [CREATURE_INTERACTION.md](CREATURE_INTERACTION.md), [ITEMS_TOOLS.md](ITEMS_TOOLS.md),
[SURVIVAL_INTEGRATION.md](SURVIVAL_INTEGRATION.md), [TECH_DEBT_INFLUENCE.md](TECH_DEBT_INFLUENCE.md).

## Decisions

| ID | Meaning | Status |
|----|---------|--------|
| **A** | Dig groups ≠ damage/armor groups (two dictionaries on one item) | Accepted (TD-INF-003 wontfix merge) |
| **C** | Dig goes through Influence pipeline (`Channel::Dig`) | Target / shipping (TD-INF-013) |

Industrial note: Luanti/Minecraft keep separate dig vs punch **math** under one tool def.
Cubatarium C unifies the **bus** (intent → resolve → apply → events → wear), not group matchers.

## Pipeline

```
PlayerInteractionRouter / ActivityAgent
  → CreatureIntent.Influence (Channel: Melee | Dig | Use stub | …)
  → IUToolInfluenceProvider (items) or bare-hand
  → InfluenceResolver
       Dig  → ResolveDigParams (block dig groups × tool groupcaps)
       Melee → ResolveHitParams (armor groups × tool damage_groups)
  → InfluenceApplier → vitals / DigSessionState+DelBlock / wear / status
  → InfluenceEvents → FX (creature flash/beam; crack/break)
```

## Dual group domains (A)

| Dictionary | Carrier | Matcher | Affects |
|------------|---------|---------|---------|
| Dig groups | `block.dig.groups` × `tool.groupcaps` | `ResolveDigParams` | dig time + dig wear |
| Damage / armor | `tool.damage_groups` × `creature.armor_groups` | `ResolveHitParams` | HP + punch wear |

Example: sword has `damage.fleshy` for melee and `groupcaps.snappy` for leaves — not the same table.

## Dig duration (hybrid)

| Condition | Duration |
|-----------|----------|
| Creative | `0` (instant) |
| `hardness <= 0` Survival | `-1` (no progress) |
| Tool groupcap match | `times[rating]` (± strength) |
| No match (hand / wrong tool) | `hardness * 1.5` (`BlockDigRules`) |

## Modules

| Module | Owns |
|--------|------|
| `ToolCapabilities` | `ResolveDigParams`, `ResolveHitParams`, wear deltas |
| `ModePolicy` | Creative/Survival/Peaceful gates for dig/combat/vitals/wear/aggro |
| Influence core | Intent, Resolver, Applier, Events, DigSessionState |
| `PlayerInteractionRouter` | LMB pointed creature vs block → Intent |
| Vitals | `ApplyDamage` by reason (influence pre-mitigated) |
| Presentation | Influence FX; dig crack/break from Dig events |

## Channels

| Channel | Role |
|---------|------|
| `Melee` | Punch / weapon vs creature |
| `Dig` | Break block (session progress) |
| `Use` | Reserved stub (`use_unimplemented`); social UI = TD-INF-004 |
| `Ranged` / `Aura` | Future |
| `None` | No influence this tick |
