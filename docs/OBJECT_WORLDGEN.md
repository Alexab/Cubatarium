# Object worldgen

How Object JSON, manifest rules, and runtime placement fit together.

## Three layers

| Layer | Path | Role |
|-------|------|------|
| **Object** | `Objects/<name>.json` | Block layout, category, anchor. Used by creative placement and worldgen. |
| **Worldgen rule** | `content/object_features.json` | Biome pool, spacing, weight, optional `sub_biomes`, scatter mode. Generated from manifest. |
| **Manifest** | `tools/object_manifest.yaml` | Authoring source: `worldgen` blocks per Object or scatter rule. |

Player and worldgen share one `UObjectLibrary`. Worldgen uses a subset: entries with `worldgen` in the manifest, often `*_mapgen` tree variants, and `CanPlaceObjectAtForWorldGen` collision rules.

## Pools

| Pool | JSON key | Toggle | Typical content |
|------|----------|--------|-----------------|
| Vegetation | `vegetation` | `procedural.trees` | Trees, bushes, cactus, flowers, grass patches |
| Decoration | `decoration` | `procedural.decoration` | Reeds, lily pads, paths, logs |
| Structures | `structures` | `procedural.structures` | Ruins, houses |
| Scatter (mode) | any pool | same as parent pool | Single cross blocks (`mode: scatter_blocks`) without a Object JSON |

Scatter rules place individual blocks (e.g. `rose`) via hash-derived offsets; patch Objects remain for dense clumps.

Optional `surface_constraint` per rule: `any_land` (default), `grass`, `near_water`, `water_surface`. Authoring source: `worldgen.surface_constraint` in [`tools/object_manifest.yaml`](../tools/object_manifest.yaml). Rules tuned only in JSON may set `"calibrated": true`.

## Creative vs worldgen

- **Creative**: any Object in the library; full collision via `CanPlaceObjectAt`.
- **Worldgen**: manifest + `object_features.json` only; trees often use `*_mapgen` copies; vegetation requires air voxels with solid non-fluid ground (`CanPlaceObjectAtForWorldGen` / `CanPlacePlantAt`).
- **Water features**: `reeds_*` require water near the column; `lily_pad*` anchor on the **water surface** (air above the topmost water block in the column, searching up to 3 blocks horizontally).

Legacy `tree_small` / `tree_large` (category `misc`) stay in the library for creative; they are not in worldgen pools.

### Creative-only Objects (no `worldgen` in manifest)

`bridge_wood_small`, `tree_acacia`, `tree_apple`, `tree_aspen`, `tree_jungle`, `tree_jungle_emergent`, `tree_pine`, `tree_pine_small`, `tree_pine_snowy`, `tree_pine_snowy_small`, `wall_ruin_segment`, plus legacy `tree_small` / `tree_large` (not listed in manifest). See [Object_CATALOG.md](Object_CATALOG.md) column **Worldgen** (`вЂ”` = creative only).

## Spacing and density

For vegetation and decoration, a column attempt runs when a **noise density gate** passes:

```
noise = NormalizedFBM2D(x * 0.02, z * 0.02, seed + poolSalt + ruleSeedOffset, ...)
threshold = 1 - 1 / EffectiveSpacing(spacing, density)
passes when noise > threshold
```

`EffectiveSpacing` uses `max(1, round(spacing / density))` when density &gt; 0. A small hash-derived jitter (±2) is applied to spacing so columns are not locked to a rigid grid. Density multipliers are clamped to `[0, 2]` via `ClampTuningValue` in `procedural.tuning`.

Ground cover is not suppressed when a tree occupies the same column (trees and ground cover may coexist).

Structures use `chance_per_column` instead of spacing: placement when `hash % chance == 0`.

Rule selection among candidates: weighted random using `weight`, pack feature multipliers, and per-rule `seed_offset`.

## Adding a new object

1. Create `Objects/<name>.json` (version 2, canonical block names).
2. Add entry to `tools/object_manifest.yaml` with `worldgen` pool, biomes, spacing/weight.
3. Run `python tools/generate_object_features.py`.
4. Run `python tools/validate_Objects.py` and `python tools/validate_object_features.py`.
5. Regenerate catalog: `python tools/generate_Object_catalog.py`.

For scatter-only ground cover, use manifest `mode: scatter_blocks` with `block`, `attempts`, `radius` вЂ” no Object JSON required.

## Related docs

- [Object_CATALOG.md](Object_CATALOG.md) вЂ” auto-generated Object table
- [ARCHITECTURE.md](ARCHITECTURE.md) вЂ” worldgen pipeline overview
- [RESOURCE_PACKS.md](RESOURCE_PACKS.md) вЂ” block name resolution

