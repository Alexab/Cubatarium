# Item visual redesign — implementation record

Branch: `codex/item-visual-redesign`, based on develop `946ef5f2d`.

Status: **in progress; production acceptance has not been reached**.

## Fixed design

All 70 items retain their IDs, crafting, statistics, finite ownership, physics and
throw rules. Faceted handcrafted forms, authored painted surfaces and gentle
lighting. Original meshes, UVs and pixels; reference assets remain references.
Generator plus preserved author overrides. Item Lab is an in-game inspection
screen, not a modelling or painting application. Only existing gameplay states
drive animation: bow, shears, containers and use transitions.

## Baseline

`baseline.json` preserves all 70 definitions, active declared model paths,
attribution records, triangle counts, texture sizes and identical geometry groups.
These are declared disk resources, **not a runtime resource-resolution trace**.
Existing production assets are deliberately unchanged at this checkpoint.

## Implemented foundation

- Independent `itemgen.mesh` surface representation, convex profile extrusion,
  XY corner bevels, open thick-walled shells, rotation about pivots and taper.
- Outward CCW faces and independent flat-shaded triangles with unit normals.
- `itemgen.surface_uv` deterministic per-surface orthonormal projections,
  explicit texel density and 128/256/512 atlas budgets with four-pixel gutters.
  Overflow and duplicate surface names fail rather than silently scaling UVs.
- Eight meaningful geometry/UV regressions cover winding, shell cavity,
  degenerate geometry, bounds, pivots, packing, overflow and repeatability.

The modules are not yet connected to the production exporter. This is an additive
foundation; it does not claim that existing assets have acquired new geometry,
textures, normals or lighting.

## Remaining implementation, in order

1. Native baseline and actual resource-path trace, before/after references and
   individual design cards. No claim of direct Luanti mesh copying is established.
2. Versioned recipe adapter and surface glTF exporter with normals, part UVs,
   explicit material assignments, stable nodes/pivots and authored overrides.
   Build to staging; validate and publish separately; preserve sidecars and layers.
3. Surface-space painted layers and authored overlays: base, broad variation,
   construction detail, manufacturing marks. 128 small materials, 256 hand items,
   512 armor. Albedo and optional standard metallic/roughness parameters.
4. ItemRenderAsset, ItemVisualState and ItemPresentationProfile; item-only mesh
   cache and gentle lit shader across icon/FP/TP/armor/ground/carry, with colour
   spaces, filtering/mips, world light/fog and neutral studio light. Leave creature
   buffer ABI and creature shaders unchanged.
5. In-game Item Lab: catalog, old/new, fixed/orbit cameras, silhouette/gray/albedo/
   lit/UV/normals/wireframe, 32/48/64 icons, FP/TP/ground, both body types, states,
   anchors/bounds/cost, reload with cache invalidation, PNG/JSON export. Synthetic
   instances must not mutate saved player property.
6. Twelve pilots: stone_pickaxe, iron_pickaxe, copper_axe, gold_sword, iron_head,
   leather_chest, wood_bow, bucket, shears, apple, bread, raw_iron.
7. Remaining models in waves: tools/weapons, utilities/containers, armor,
   food/materials. Remove unrelated family aliases and color-only armor tiers.
8. Full native visual audit and performance acceptance. No final completion
   checkbox until every item and gameplay representation has evidence.

## Acceptance contract

Deterministic publication preserves author work and grips. Missing production
assets fail acceptance instead of substituting a cube. No accidental UV mirror,
bleeding or stretched tiles. Item state cancellation restores the correct pose.
Two hands, throws, pickup and armor motion retain ownership and gameplay state.

Triangle caps: 1200 hand items, 1800 compound utility/bow, 2000 armor slot,
600 food/material; no minimum count as a proxy for beauty. Measure cold catalog,
texture memory, FP, equipped characters and 512 bodies; p95 >10% over the same
baseline scene requires optimization. Capture 1280x720 and 1920x1080 at
100/150/200%, day/night/interior. Review all 70 contact sheets and native views.

Only Release desktop:

```
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/test_itemgen_mesh.py
python tools/test_itemgen_surface_uv.py
```

World loading and native checks must use `tools/flight_sim_run.py` with
`bin/Cubatarium.exe` on isolated QA worlds. The foundation tests are not native
visual acceptance. Full ownership/equipment/throw regressions remain mandatory
when the new runtime path is connected.
