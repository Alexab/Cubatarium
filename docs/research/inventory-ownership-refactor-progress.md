# Inventory ownership refactor — implementation progress

Branch: `codex/inventory-ownership-refactor`, baseline `b2db601da`.
Status: **in progress; the full accepted plan is not implemented**.

## Implemented foundation

- Explicit finite/catalog entry form, persisted in inventory v4. Legacy negative
  counts are recognised on read and normalised when installed in inventory.
- Catalog entries and finite stacks cannot merge. Catalog listings create explicit
  references for blocks and items; slot assignment preserves the supplied form.
- Creative finite extraction and splitting transfer quantities. Only catalog
  extraction materialises a new finite instance.
- Creative equipment consumes the actual finite source; unequip returns the
  same instance and wear instead of discarding it.
- Character/backpack/catalog count labels recognise the explicit form.
- User-file backup before upgrading pre-v4 inventory data.
- Added transaction regressions for mixed Creative slots, splitting, extraction,
  catalog materialisation, equipment identity and v3/v4 loading.

## Work still required

1. Inventory-operation facade, slot revisions and independent domain recovery.
2. Persistent world-item domains and v2 physical data; removal of creativeCopy.
3. Transactional capacity reservation, sleeping/deferred drops and bounded queues.
4. Stateful catalog-container materialisation; action-policy separation.
5. Throw categories, world settings, eligibility checks and post-impact lifecycle.
6. Unified death/loot processing, Creative mining and pickup.
7. Authoritative carry relation, domain context of projectiles, active cargo mass.
8. Coordinated save generations and interrupted-save recovery.
9. Unified E/B window, hand-slot click fixes, explicit Drop/delete and middle pick.
10. Full accepted acceptance matrix, CI changes and final audit.

Creative mining and pickup have intentionally not been enabled before world-item
ownership is implemented. Existing native regressions do not certify the pending
Creative scenarios or the complete refactor. No merge into develop is performed.

## Validation of the foundation

- Release desktop build: passed, static executable dependency check passed.
- Release CTest: inventory transactions, inventory mode, armor equipment,
  resource economy — 4/4 passed.
- Existing native inventory/survival/throw fixture in CarryAimQA — 51/51 passed.
- Style audit: no violations; clang-format: 9 changed C++ files passed.
- These are foundation regressions, not completion of the full acceptance matrix.
