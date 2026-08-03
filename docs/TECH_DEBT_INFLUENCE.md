# Tech debt: Influence (creature interaction)

> Review at end of phases 0–5 / deep interaction refactor. Close items when
> implemented or explicitly wont-fix / accepted backlog.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-INF-001 | 0 | Faction / friendly-fire filters on targeting | Needs faction model | accepted backlog |
| TD-INF-002 | 0 | Punch/melee tool wear via same `ApplyItemWear` + ModePolicy gate | Dig wear shipped; melee wear not wired (`PunchAttackUses` unused). Provider for damage is wired (`UItemToolInfluenceProvider`) | deep-refactor Wave 2 |
| TD-INF-004 | 0 | Social right-click channel (UI / trade / tame) | Separate from punch/dig Influence channels | accepted backlog |
| TD-INF-005 | 0 | Accuracy / hit-chance using `attributes.accuracy` | Formula TBD; attr unused in resolve | accepted backlog |
| TD-INF-006 | 0 | Cone targeting | Radius/single shipped; cone not needed for MVP | accepted backlog |
| TD-INF-007 | 0 | Full gameplay particle budget / LOD | Minimal flash/beam/burst shipped | accepted backlog |
| TD-INF-008 | 0 | `armor_groups` / `bare_hand` JSON on creature.json | Defaults in code (`ArmorGroups::DefaultFleshy`) | accepted backlog |
| TD-INF-009 | 0 | Intelligence-driven influence (skills / spells) | Attr unused | accepted backlog |
| TD-INF-011 | 5 | Load `models/effects/*.json` into `UStatusEffectCatalog` at runtime | Builtins hardcoded; JSON is authoring samples | accepted backlog |
| TD-INF-013 | deep-refactor | **Dig as Influence `Channel::Dig` (pipeline C)** | Break session today is a parallel path in `World`/`BlockInputController`, outside Influence Resolve/Apply/Events. Deep refactor target: one Intent→Resolve→Apply→Events bus for Melee **and** Dig; DigSessionState for progress; crack/break FX from Dig events. **Not** dig-as-HP and **not** merging dig group tables with damage/armor groups (see TD-INF-003). | deep-refactor Wave 3 |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-INF-003 | deep-refactor decision | **Wontfix: merge dig groupcaps tables with Influence damage/armor groups.** Luanti dual dictionaries (decision A): `cracky`/`choppy`/… on blocks × tool `groupcaps` vs `fleshy`/… on creatures × tool `damage_groups`. Different rating semantics and carriers. Sharing the Interaction **pipeline** is TD-INF-013 (C), not sharing group name/formula tables. |
| TD-INF-010 | 4 | Radius / aura targeting via `InfluenceTargeting::Radius` + spatial neighbors |
| TD-INF-012 | 1–3 | Core Influence pipeline: resolve/apply, cooldown/range, events, status tick, FX sink (Melee/status; Dig channel = TD-INF-013) |

## Decision log (A vs C)

| Decision | Meaning | Status |
|----------|---------|--------|
| **A** | Dig groups ≠ damage/armor groups (two dictionaries on one item) | **Accepted** — TD-INF-003 closed wontfix |
| **C** | Dig goes through Influence pipeline (`Channel::Dig`) | **Accepted as deep-refactor target** — TD-INF-013 open (previously declined when scope was integration-only; reopened for deep refactor) |

Industrial note: Luanti/Minecraft keep separate dig vs punch **math** under one tool def. Cubatarium C unifies the **bus** (intent/resolve/apply/events/wear), not the group matchers — aligned with Action/Interaction frameworks, not with “blocks have HP”.

## Execution progress

- **Phase 0–5 (original Influence plan):** types, melee pipeline, status, FX, player/AI wiring, docs; open tails in table above.
- **Deep refactor:** Wave 0 docs freeze; ToolCapabilities SoT + ModePolicy + Dig channel (TD-INF-013) + Router + intent SoT — see [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md).

## Survival QA checklist

- [ ] Survival mode: LMB on mob within melee reach deals damage (cooldown), not every frame
- [ ] Creative: LMB on creature does not deal Influence damage
- [ ] Bot (`bot_player`) melee on hostile reduces HP and can apply bleed DoT
- [ ] Hostile `melee_attack` hits player; without player, can chase/attack nearby mob
- [ ] Hit flash visible on target; brief path beam between source and target
- [ ] Status `bleed` ticks after melee; move speed affected if `slow` applied
- [ ] Fatal wounds / despawn still work after Influence damage
- [ ] *(after TD-INF-013)* Survival dig of a diggable block goes through Influence Dig channel (progress + complete event); hardness 0 stays unbreakable; Creative instant via resolve duration 0
