# Tech debt: Influence (creature interaction)

> Review at end of phases 0–5. Close items when implemented or explicitly wont-fix / accepted backlog.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-INF-001 | 0 | Faction / friendly-fire filters on targeting | Needs faction model | backlog |
| TD-INF-002 | 0 | Item/tool `tool_capabilities`, durability, wield mapping | blocked-on-tools | tools-agent |
| TD-INF-003 | 0 | Dig `groupcaps` shared with influence groups | blocked-on-tools | tools-agent |
| TD-INF-004 | 0 | Social right-click channel (UI / trade / tame) | Separate from punch/influence | backlog |
| TD-INF-005 | 0 | Accuracy / hit-chance using `attributes.accuracy` | Formula TBD | 4 |
| TD-INF-006 | 0 | Cone targeting | Radius/single first | 4 |
| TD-INF-007 | 0 | Full gameplay particle budget / LOD | Minimal VFX in phase 3 | 3 / backlog |
| TD-INF-008 | 0 | `armor_groups` / `bare_hand` JSON on creature.json | Defaults in code first | 1–2 |
| TD-INF-009 | 0 | Intelligence-driven influence (skills / spells) | Attr unused | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| *(none yet)* | | |

## Execution progress

- **Phase 0:** Influence types, hit math, `IUToolInfluenceProvider`, bare-hand stub, intent `Influence` field, this tracker, `CREATURE_INTERACTION.md` draft.
- **Phase 1:** `InfluenceResolver` / `InfluenceApplier` / events; melee via Influence with range + cooldown; `ApplyDamage` reason tag; armor groups + punch timer on `UCreature`.
- **Phase 2:** Status effects (`bleed`/`slow` builtins + `models/effects/*.json` samples); tick + move-speed mul; melee applies bleed.
- **Phase 3:** Influence FX sink (hit flash on creatures, path beams + burst markers); `UInfluenceFxPass` after creature draw.
