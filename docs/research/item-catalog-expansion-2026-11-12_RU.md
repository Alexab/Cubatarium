# Item catalog expansion

Date: 2026-11-12. Order fixed by design: utilities, then food/farming, then combat extras.

## Familiar roles (not meshes)

| Game | Jobs worth knowing |
|------|--------------------|
| Minecraft | Buckets, flint and steel, shears, torches, seeds/hoe/farmland, furnace food, spear-like trident later |
| Vintage Story | Tool tiers, clay/cooking chain, knapping — too simulation-heavy for this wave |
| Hytale (public notes) | Silhouette first, shared family meshes |
| Luanti | Groupcaps already match our dig model |

## Style anchors

Cubatarium items stay box-built (`tools/itemgen`), 64×64 atlases, original geometry. CC0 packs are silhouette references only.

## Waves

### A — Utilities (this change)

- `bucket`, `flint_and_steel`, `shears` plus existing `wood_torch` place-to-`torch`.
- Runtime: held item with `use.action = place_block` (torch) places a block and consumes one. Fluid pour and ignition are still the Raycast comments; these items exist so the catalog and craft path are real.

### B — Food / farming

- `seeds` item and hoe `crumbly` caps ship now.
- Till (replace dirt with farmland), crop growth, and cooking need a block-replace API and a furnace station. They stay the next system pass, not a second copy of Minecraft farming.

### C — Combat extras

- `wood_spear` uses the existing melee path (`damage.fleshy`, thrust-style grip).
- Bows and shields were already shipped (hitscan + offhand block).

## Grip

Family pose defaults live in `ItemVisualDefaults::ResolveWieldPose` for first-person and third-person when content euler/offset are zero. The viewmodel lean (`kToolWieldLeanDeg`) stays an arm bias separate from that tool-local euler.
