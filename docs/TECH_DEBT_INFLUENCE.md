# Tech debt: Influence (creature interaction)

> Review at end of phases 0–5. Close items when implemented or explicitly wont-fix / accepted backlog.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-INF-001 | 0 | Faction / friendly-fire filters on targeting | Needs faction model | accepted backlog |
| TD-INF-002 | 0 | Item/tool `tool_capabilities`, durability, wield mapping | blocked-on-tools (`UItemToolInfluenceProvider` stub exists in Items/) | tools-agent |
| TD-INF-003 | 0 | Dig `groupcaps` shared with influence groups | blocked-on-tools | tools-agent |
| TD-INF-004 | 0 | Social right-click channel (UI / trade / tame) | Separate from punch/influence | accepted backlog |
| TD-INF-005 | 0 | Accuracy / hit-chance using `attributes.accuracy` | Formula TBD; attr unused in resolve | accepted backlog |
| TD-INF-006 | 0 | Cone targeting | Radius/single shipped; cone not needed for MVP | accepted backlog |
| TD-INF-007 | 0 | Full gameplay particle budget / LOD | Minimal flash/beam/burst shipped | accepted backlog |
| TD-INF-008 | 0 | `armor_groups` / `bare_hand` JSON on creature.json | Defaults in code (`ArmorGroups::DefaultFleshy`) | accepted backlog |
| TD-INF-009 | 0 | Intelligence-driven influence (skills / spells) | Attr unused | accepted backlog |
| TD-INF-011 | 5 | Load `models/effects/*.json` into `UStatusEffectCatalog` at runtime | Builtins hardcoded; JSON is authoring samples | accepted backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-INF-010 | 4 | Radius / aura targeting via `InfluenceTargeting::Radius` + spatial neighbors |
| TD-INF-012 | 1–3 | Core Influence pipeline: resolve/apply, cooldown/range, events, status tick, FX sink |

## Execution progress

- **Phase 0:** Influence types, hit math, `IUToolInfluenceProvider`, bare-hand stub, intent `Influence` field, this tracker, `CREATURE_INTERACTION.md` draft.
- **Phase 1:** `InfluenceResolver` / `InfluenceApplier` / events; melee via Influence with range + cooldown; `ApplyDamage` reason tag; armor groups + punch timer on `UCreature`.
- **Phase 2:** Status effects (`bleed`/`slow` builtins + `models/effects/*.json` samples); tick + move-speed mul; melee applies bleed.
- **Phase 3:** Influence FX sink (hit flash on creatures, path beams + burst markers); `UInfluenceFxPass` after creature draw.
- **Phase 4:** Player Survival LMB melee; controlled resolve path; agents sync `InfluenceIntent`; melee AI mob↔mob fallback; radius targeting prototype.
- **Phase 5:** Docs finalized; open tails marked accepted backlog / blocked-on-tools; Survival QA checklist below.

## Survival QA checklist

- [ ] Survival mode: LMB on mob within ~4 blocks deals damage (cooldown ~0.5s), not every frame
- [ ] Creative: LMB on creature does not deal Influence damage
- [ ] Bot (`bot_player`) melee on hostile reduces HP and can apply bleed DoT
- [ ] Hostile `melee_attack` hits player; without player, can chase/attack nearby mob
- [ ] Hit flash visible on target; brief path beam between source and target
- [ ] Status `bleed` ticks after melee; move speed affected if `slow` applied
- [ ] Fatal wounds / despawn still work after Influence damage
