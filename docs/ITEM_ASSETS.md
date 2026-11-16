# Item and armor visual assets

The active catalog contains 70 authored v3 items. Item IDs, recipes, statistics,
physical profiles and inventory state remain gameplay data in `content/items/`.
The [redesign implementation record](research/item-visual-redesign-2026-11-14/README.md)
tracks publication, visual evidence and remaining acceptance gates.

## Production resources and renderer

`models/items/MANIFEST.json` is the version 4 publication manifest. Each active
item has `design_version: 3` glTF geometry, normals, surface UVs, material factors,
painted albedo, grip/presentation profiles, attribution and dependency hashes.
The prior published directory is retained under `_itemgen_reference/` for Item Lab.

`ItemRenderAsset` and `ItemSurfaceRenderer` form the item-only geometry/material
path shared by icons, FP, TP, armor and physical ground/carried items. Creature
vertex formats and creature lighting remain separate. The stylized item shader
uses linear-light diffuse shading, a restrained roughness/metallic highlight,
world ambient light and fog; icons use fixed studio lighting. Albedo textures
use sRGB decoding, mipmaps and linear filtering. Small icons use a linear-light
supersampled resolve. These are documented subsets, not a complete PBR renderer.

Geometry determines silhouette, joints and thickness; per-surface paint supplies
material variation, construction details and local manufacturing marks. Default
albedo budgets are 128/256/512 for small supplies/handheld items/armor. Four-pixel
UV gutters and explicit surface orientation prevent neighboring-island bleed.
Native visual review remains necessary: budget and file-presence tests do not
certify artistic quality.

Icon framing, FP/TP grips, carry contacts, ground orientation and physical size
are independent. World wielded equipment is hidden only when the possessed player
actually uses the first-person camera and FP viewmodel. Bow, utility pulses,
container fill and use phases derive from gameplay state. Rendering does not own
or modify inventory instances.

Legacy glTF and parts/procedural previews remain supported for migration and
archived comparisons. They do not excuse a missing or malformed v3 production
resource; production acceptance fails instead of silently substituting a cube.

## Authoring and publication

Author sources are `tools/item_designs/<id>/design.yaml`, optional read-only
surface overlays and restricted manual glTF sources; family construction and
material painting live in `tools/itemgen/`. Design cards and provenance records
are kept beside these sources. The older `tools/item_recipes/` pipeline is
historical and protected from overwriting the published v3 line.

```text
python tools/itemgen.py build-design --id iron_pickaxe
python tools/itemgen.py validate-design --id iron_pickaxe
python tools/itemgen.py publish-design --id iron_pickaxe
```

Build writes staging; validation and publication are separate operations.
Publication checks source/dependency hashes, preserves authored grip/wear sidecars,
archives the previous resources and commits a recoverable transaction. Author
layers are inputs and are never overwritten by regeneration. Manual imports
reject unsupported geometry/material features with an explanation. See the
source-authoring documentation under `tools/item_designs/` for the exact subset.

## Native inspection

Ctrl+F8 opens Item Lab with archived/reference and staged/published panes. It
supports all 70 IDs; orbit/fixed cameras; silhouette, gray, albedo, lighting, UV,
normal and wireframe modes; 32/48/64-pixel icons; FP/TP, armor on both human forms,
drops, contacts and physical envelopes. Pose, fill, ammo and use controls operate
on test state. Reload invalidates resource/icon caches; Export writes PNG and JSON.
The lab does not consume or save player property and is not a modeling editor.

```text
cmake --build build/desktop-msvc --config Release --target Cubatarium --parallel 8
python tools/run_item_lab_qa.py --skip-build --world CarryAimQA --width 1920 --scale 2
python tools/run_item_world_visual_qa.py --skip-build --perspective front --body female
```

Native runners use `tools/flight_sim_run.py` with `bin/Cubatarium.exe` on dedicated
QA worlds. They check report coverage and restore world/config/runtime changes.
Display reports distinguish the user scale from the effective DPI-adjusted scale.
Automated flight launches temporarily disable audio and zero every volume bus;
original audio settings are restored on normal or error exit. QA world creation
in the item runners uses the same policy.
Use `run_items_acceptance.py --extended` for the integrated Release property,
equipment, throw and visual regressions.

## Provenance

The active authored line records project coordinate recipes, surface paint and
per-item review evidence under the repository MIT license. It imports no external
mesh, UV or pixel input. Familiar functional archetypes are intentional. Hash
changes alone do not establish visual independence, and the recorded review is
not an exhaustive external-catalogue comparison or legal certification.

Historical CC0 imports below remain separate reference records. Their attribution
does not assign CC0 to the new authored line. Inspect each resource's
`ATTRIBUTION.json` and `LICENSE.txt`, plus `THIRD_PARTY_NOTICES.md`.

## Historical / optional upstream CC0 import tooling

| Pack | License | URL | Use |
|------|---------|-----|------|
| Kenney Survival Kit | CC0-1.0 | https://www.kenney.nl/assets/survival-kit | shovel/axe/pickaxe/hammer/hoe (GLBв†’glTF) |
| KayKit RPG Tools Bits | CC0-1.0 | https://kaylousberg.itch.io/rpg-tools-bits | hand tools (place under `third_party/asset_cache/`) |
| KayKit Fantasy Weapons Bits | CC0-1.0 | https://kaylousberg.itch.io/fantasy-weapons-bits | swords/spears/shields |
| Quaternius Fantasy Props | CC0-1.0 | https://quaternius.com/ | armor/weapon props |

Without packs, `tools/parts_to_gltf.py` builds educational CC0 stand-in glTF from `parts_v1`.

```bash
# Inventory extracted pack
python tools/inventory_pack_assets.py --pack-root third_party/asset_cache/kenney_survival_kit --source-id kenney_survival_kit

# Import (requires --source-id)
python tools/import_item_models.py --list
python tools/import_item_models.py --source-id kenney_survival_kit --pack-root third_party/asset_cache/kenney_survival_kit --retarget-content

# Generate stubs for role:new + validate
python tools/generate_item_defs_from_manifest.py
python tools/parts_to_gltf.py --retarget-content
python tools/validate_item_defs.py
```

Historical import manifest: [`tools/item_model_manifest.json`](../tools/item_model_manifest.json). Cache: [`third_party/asset_cache/README.md`](../third_party/asset_cache/README.md) (gitignored extracts).

### Item icon scheduling and disk persistence

Item GUI lookups queue cold disk validation and rendering instead of opening
files synchronously. Each warmup call limits both work count and elapsed time
(four milliseconds, with at least one queued item advanced). Rendered icons are
available immediately in memory; optional disk persistence uses a bounded PBO
queue and zero-timeout fence polling, draining at most two completed images per
flush. The manifest is saved after pending item readbacks drain. Invalidation
cancels matching queued transfers; shutdown cancels pending GPU work. Normal
rendering does not wait for a fence. Block and creature caching retain their
existing behavior. Native isolated-cache tests verify byte-exact RGBA reload,
pack/read/draw state restoration and invalidation. See the latest
[performance acceptance](research/item-visual-redesign-2026-11-14/performance-final/README.md)
for the final gate results and measurement scope.


Item geometry uploads preserve all vertices, rebasing primitive indices into one
VAO/VBO/EBO per asset. Material and node draw order are unchanged. The measured
954 -> 210 geometry-object reduction covers one renderer cache containing the
70 authored assets. Default bounds and static wear selections are cached at load.
World uniform reuse is restricted to an explicit consecutive item-draw scope.

The desktop GUI textured-quad shader uses a static unit quad plus `rectPx`.
Legacy/GLES shaders without that uniform retain pixel-position vertices.
Independent native RGBA tests cover both paths at 1280x720 and 1920x1080 with
100/150/200% scale. Colored quad streams orphan storage before reuse.

### Functional hand presentation

Authored hand profiles may define `tp_left_euler_deg` independently of the right
hand. Without it, the legacy mirrored TP angles remain supported. Directed
working edges must not be reversed just to mirror the hand. `fp_use_euler_deg`
and `fp_use_hand_offset` are optional use-only overrides, blended from the idle
pose during the first third of the authoritative use phase; cancellation returns
to idle, and offhand presentation does not inherit the main-hand use phase.
These fields describe rendering and do not store inventory state.

`tools/item_hand_alignment.json` gives all catalog items explicit inspection
contracts. `tools/audit_item_hand_alignment.py` checks actual submitted matrices,
functional directions and actual palm contacts. TP candidates are selected by
matching the loaded grip to the skeleton palm, so a correctly oriented loose
copy cannot hide an incorrectly held model. FP uses its own viewmodel camera
basis, separately from the logical world camera. Native presentation tests also
capture rest, strike and return on both body variants. See the
[hand-presentation audit](research/item-hand-presentation-2026-11-16/README.md).
