# Tech debt: Items / Tools

> Review at end of tools plan. Close items when implemented or explicitly wont-fix / accepted backlog.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-ITEM-001 | foundation | Backpack grid for durable item instances | Hotbar-only v1; storage map is count-only | accepted backlog |
| TD-ITEM-002 | foundation | Anvil UI / craft two tools → one | `/repair` covers v1 | accepted backlog |
| TD-ITEM-003 | dig | Explicit block `dig.groups` on all packs | InferDigGroups heuristic until deep-refactor Wave 3 apply script | deep-refactor Wave 3 |
| TD-ITEM-004 | wield | Full shader-based FP 3D tool mesh (core-profile) | Screen-space icon overlay shipped as interim viewmodel | accepted backlog |
| TD-ITEM-005 | wield | Third-person wield attachment | FP-first scope | accepted backlog |
| TD-ITEM-006 | content | Dedicated glTF/obj models under `models/items/` | Procedural icons suffice for v1 | accepted backlog |
| TD-ITEM-007 | combat | Enchantments / tool modifiers beyond melee | Out of scope | wontfix-v1 |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-ITEM-010 | wear | Creative + Peaceful wear gate via `IsToolWearEnabled` + `WorldDifficulty::Peaceful` |
| TD-ITEM-011 | foundation | `ContentKind::Item` + Tools palette tab + `UItemDefinitionStorage` |
| TD-ITEM-012 | dig | `ResolveDigParams` wired into break duration + wear on complete; Dig Influence channel = TD-INF-013 |
| TD-ITEM-013 | content | Base 12 tools wood/stone/iron × pick/axe/shovel/sword |
| TD-ITEM-014 | influence | `UItemToolInfluenceProvider` used from `CreatureCombat::TryMeleeStrike` |
| TD-ITEM-015 | wield | FP overlay via `DrawItemWieldOverlay` (icon billboard); full mesh remains TD-ITEM-004 |
| TD-ITEM-016 | qa | `tool_capabilities_test` covers wear gate + dig + destroy wear |

## Deep-refactor notes

See [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md): decision A (dual group tables) + C (Dig via Influence bus).
