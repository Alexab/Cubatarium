# Tech debt: Items / Tools

> Review at end of tools plan. Close items when implemented or explicitly wont-fix / accepted backlog.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-ITEM-001 | foundation | Backpack grid for durable item instances | Hotbar-only v1; storage map is count-only | accepted backlog |
| TD-ITEM-002 | foundation | Anvil UI / craft two tools → one | `/repair` covers v1 | accepted backlog |
| TD-ITEM-004 | wield | Skinned FP arms on same clear-Z / FOV72 pass (not body-in-FP) | Box dual-arm viewmodel + Item/Block held is shipped; attach glTF arms later | accepted backlog |
| TD-ITEM-005 | wield | Third-person wield attachment | FP-first scope | accepted backlog |
| TD-ITEM-007 | combat | Enchantments / tool modifiers beyond melee | Out of scope | wontfix-v1 |
| TD-ITEM-008 | combat | Bow projectiles / ranged | wood_bow is melee-light only | accepted backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-ITEM-003 | Wave 3 | Explicit `dig.groups`/`dig.level` on packs via `tools/apply_block_dig_groups.py`; InferDigGroups remains fallback only |
| TD-ITEM-006 | character sheet tails | Curated `models/items/*.json` parts + optional glTF via `UItemPreviewRenderer` / `import_item_models.py`; see `docs/ITEM_ASSETS.md` |
| TD-ITEM-010 | wear | Creative + Peaceful wear gate via `IsToolWearEnabled` + `WorldDifficulty::Peaceful` |
| TD-ITEM-011 | foundation | `ContentKind::Item` + Tools palette tab + `UItemDefinitionStorage` |
| TD-ITEM-012 | dig | `ResolveDigParams` wired into break duration + wear on complete; Dig Influence channel = TD-INF-013 |
| TD-ITEM-013 | content | Base 12 tools wood/stone/iron × pick/axe/shovel/sword |
| TD-ITEM-014 | influence | `UItemToolInfluenceProvider` used from `CreatureCombat::TryMeleeStrike` |
| TD-ITEM-015 | wield | FP clear-Z viewmodel (`DrawWorldOverlay`, FOV72, dual arms, Item+Block, offhand, swing/inertia); Perspective-only via `ShouldDrawFpViewmodel`; skinned remains TD-ITEM-004 |
| TD-ITEM-016 | qa | `tool_capabilities_test` covers wear gate + dig + destroy wear |
| TD-ITEM-017 | character sheet | Paper-doll CharacterSheet + armor equip via `SlotSurface::CharacterArmor`; `armor_equipment_test` |
| TD-ITEM-018 | wear visual | Worn armor meshes on `bone_skeleton` body (`WornEquipmentDrawer`) in world TP + character sheet preview; `wear.json` sockets |

## Deep-refactor notes

See [INTERACTION_ARCHITECTURE.md](INTERACTION_ARCHITECTURE.md): decision A (dual group tables) + C (Dig via Influence bus).
