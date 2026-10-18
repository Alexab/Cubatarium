# Worldgen biome profiles

Biome definitions live in `content/worldgen_packs/<pack>/biomes/*.json`.
Pack loading compiles their keys into a sorted, immutable registry owned by the
published worldgen-pack snapshot. A `BiomeHandle` is temporary and only valid
with that exact snapshot; save files and pack data must store the string `id`.

## Schema version 1

Existing definitions without `schema_version` are interpreted as version 1.
They retain the current built-in biome classifier and may specify `height`,
`palette`, `features`, and `sub_biomes`. This remains the compatibility path for
the bundled packs.

## Schema version 2

Version 2 adds data-defined surface climate profiles. Example:

```json
{
  "schema_version": 2,
  "id": "azure_meadow",
  "domain": "surface",
  "climate": {
    "temperature": [0.3, 0.8],
    "moisture": [0.6, 1.0],
    "continentalness": [0.25, 0.9],
    "weight": 1.0
  },
  "height": {
    "base_offset": 1,
    "amplitude_mul": 1.0,
    "volatility_mul": 0.8
  },
  "palette": {
    "surface": "grass",
    "subsurface": "dirt"
  },
  "features": {
    "vegetation": {"tree_pine": 0.8},
    "ground_cover": {"bush_blueberry": 1.2}
  }
}
```

`domain` is `surface` or `cave`. `climate` must contain at least one axis;
each provided axis is a normalized `[min, max]` range with `0 <= min < max <= 1`.
Supported axes are temperature, moisture, continentalness, erosion, weirdness,
and ridge. `weight` defaults to 1 and must be in `(0, 100]`. Height and palette
profiles participate in the dynamic biome blend. Surface feature rules can
refer to data-defined IDs in their `biomes` arrays in `content/object_features.json`.

Surface profiles participate in the normalized surface climate blend. Cave
profiles are sampled in 3D below the surface and use climate-weighted selection
with the legacy cave classifier as fallback. A custom cave key can be used in
the `biome` field of a cave feature rule in `content/object_features.json`;
the biome's `features.cave_features` entries scale that rule's placement rate.
The legacy enum is still supplied alongside the key for existing algorithms,
so custom cave IDs do not need a C++ classifier.

The registry is captured in the immutable worldgen-pack snapshot. Handles are
temporary indices into that snapshot; only keys belong in JSON or save data.
During chunk population, the content pin keeps the same pack/biome registry
visible to the sampler and feature placer even if another pack is loaded.

## Vegetation feature strategies

`content/object_features.json` can select a registered feature strategy using
`mode`. The existing `object` and `scatter_blocks` strategies stay supported.
`procedural_tree` takes a stable `species` key and a `tree_profile` with bounded
height, crown radii/shape, branch count, leaf density, root chance, and block
names. An optional `ecology` profile can constrain temperature, moisture,
elevation, and maximum local slope; the existing surface constraint handles
water proximity. Its geometry is generated deterministically in voxel space for
each world-space candidate. Missing species-specific trunk/leaves names fall
back to the generic `tree_log` and `tree_leaves` blocks.

The sample profiles reuse `mtg_tree`, `mtg_leaves`, `mtg_pine_tree`, and
`leaves_spruce` from the already installed `minetest_default_16` pack; they add
no downloaded art. The source pack records CC-BY-SA-3.0, copyright celeron55,
Perttu Ahola et al., and the upstream source at
`https://github.com/minetest-game/default`. Keep source pack, author, and
license records with the pack if new assets are introduced later.

Biome IDs must be unique lowercase snake-case keys (up to 64 characters).
Unsupported schema versions, invalid domains/ranges, duplicate IDs, or custom
IDs that use the legacy schema reject the whole pack. The active immutable
snapshot remains in use if loading a replacement pack fails.
