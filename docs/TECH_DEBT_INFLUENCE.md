# Tech debt: Influence (creature interaction)

> Review at end of phases 0–5 / deep interaction refactor. Close items when
> implemented or explicitly wont-fix / accepted backlog.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-INF-001 | 0 | Faction / friendly-fire filters on targeting | Needs faction model | accepted backlog |
| TD-INF-004 | 0 | Social right-click channel (UI / trade / tame) | Separate from punch/dig Influence channels | accepted backlog |
| TD-INF-006 | 0 | Cone targeting | Radius/single shipped; cone not needed for MVP | accepted backlog |
| TD-INF-007 | 0 | Full gameplay particle budget / LOD | Minimal flash/beam/burst shipped | accepted backlog |
| TD-INF-009 | 0 | Intelligence-driven influence (skills / spells) | Attr unused | accepted backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-INF-002 | Wave 2 | Melee wear via `ApplyItemWear` + ModePolicy / PunchAttackUses in InfluenceApplier |
| TD-INF-003 | deep-refactor decision | **Wontfix: merge dig groupcaps tables with Influence damage/armor groups.** Luanti dual dictionaries (decision A). |
| TD-INF-005 | Wave 1 | Minimal accuracy miss in `ResolveHitParams` (accuracy ≤ 5) |
| TD-INF-008 | Wave 1b | `armor_groups` / `bare_hand` parse + ApplyStatsFromDefinition; zombie sample |
| TD-INF-010 | 4 | Radius / aura targeting via `InfluenceTargeting::Radius` + spatial neighbors |
| TD-INF-011 | Wave 5 | Load `models/effects/*.json` into catalog; builtins remain fallback |
| TD-INF-012 | 1–3 | Core Influence pipeline: resolve/apply, cooldown/range, events, status tick, FX sink |
| TD-INF-013 | Wave 3 | Dig as Influence `Channel::Dig`: DigSessionState, ResolveDigParams, BIC/WVB/flight-sim; DigProgress/Applied events; BlockBreakFxPass sinks Dig events (crack overlay still polls DigSessionState) |

## Decision log (A vs C)

| Decision | Meaning | Status |
|----------|---------|--------|
| **A** | Dig groups ≠ damage/armor groups (two dictionaries on one item) | **Accepted** — TD-INF-003 closed wontfix |
| **C** | Dig goes through Influence pipeline (`Channel::Dig`) | **Shipped Wave 3** — TD-INF-013 closed |

Industrial note: Luanti/Minecraft keep separate dig vs punch **math** under one tool def. Cubatarium C unifies the **bus** (intent/resolve/apply/events/wear), not the group matchers — aligned with Action/Interaction frameworks, not with “blocks have HP”.

## Execution progress

- **Phase 0–5 (original Influence plan):** types, melee pipeline, status, FX, player/AI wiring, docs; open tails in table above.
- **Deep refactor:** Wave 0 docs freeze; ToolCapabilities SoT + ModePolicy + Dig channel (TD-INF-013) + Router + intent SoT — see [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md).

## Survival QA checklist

Deep-refactor bus (Melee+Dig) shipped. Unit/integration coverage: dig session, hit/wear, armor_groups parse, content validate. Remaining boxes = in-game smoke.

- [ ] Survival mode: LMB on mob within melee pick reach (≤ strike reach) deals damage (cooldown), not every frame
- [ ] Creative: LMB on creature does not deal Influence damage
- [ ] Bot (`bot_player`) melee on hostile reduces HP and can apply bleed DoT (catalog / `OnHitStatusIds`)
- [ ] Hostile `melee_attack` hits player; without player, can chase/attack nearby mob
- [ ] Hit flash visible on target; brief path beam between source and target
- [ ] Status `bleed` ticks after melee; move speed affected if `slow` applied
- [ ] Fatal wounds / despawn still work after Influence damage
- [x] Survival dig of a diggable block goes through Influence Dig channel (progress + complete event); hardness 0 stays unbreakable; Creative instant via resolve duration 0
- [x] Dig Intent is not player-gated in Resolver; player Dig session is world-owned
- [x] Explicit pack `dig.groups` with InferDigGroups fallback; Use channel cancels `use_unimplemented`
- [x] Dig debris FX listens to DigProgress/Applied Influence events (plus DigSessionState poll fallback)
