# Item & armor 3D assets

Educational Cubatarium content under `models/items/` and `content/items/`.

## Runtime formats

| Priority | Format | Path pattern | Renderer |
|----------|--------|--------------|----------|
| 1 | Static glTF | `models/items/<id>/model.gltf` (or `model` field ending in `.gltf`) | Icons: `UItemPreviewRenderer`; FP: `UFpViewmodelRenderer::TryDrawGltfHeld`; worn armor: `WornEquipmentDrawer` |
| 2 | `parts[]` JSON | `models/items/<id>.json` | Cube parts FBO / FP fallback |
| 3 | Procedural | (none) | `FallbackParts` by item id keywords |

Item definitions in `content/items/*.json` set `"model"` to the preferred asset.

## Folder layout (canon)

```
models/items/<id>/
  model.gltf
  model.bin? / textures / model.glb (optional upstream)
  ATTRIBUTION.json
  LICENSE.txt
  wear.json          # armor attach: bones, offset, euler_deg, scale
content/items/<id>.json
models/items/<id>.json   # parts_v1 fallback (kept)
```

## Shipped catalog

Base tools (wood/stone/iron × sword/axe/pickaxe/shovel) plus hammer/knife/bow; leather + iron armor sets; expanded free-tier catalog (copper tools/armor, spears, shields, utility tools, Kenney upgraded tools). See `tools/item_model_manifest.json`.

First-person wield: glTF when present, else `parts[]` / block cubes (`DrawWorldOverlay`, clear-Z, FOV 72°). Scale via `visual.wield_scale` (category defaults). Swing/use from `content/item_visual_presets.json`. Perspective only.

Third-person: hotbar/offhand on `rightItem`/`leftItem` via `WornEquipmentDrawer::SubmitWieldedFromCreature` (prefers `ItemDefinition.ModelPath` glTF, else sibling, else `parts_v1` boxes). Character sheet paper-doll uses the same attach path (`SubmitWieldedPreview`).

Worn armor: attached to human `bone_skeleton` bones (`hat`, `body`, `*Arm`, `*Leg`, `*Item`) in world TP and character-sheet 3D preview. `ItemGltfTextureCache` loads PNG, bufferView images, and unnamed `baseColorFactor` materials.

Tooling: `tools/audit_item_visuals.py`, `tools/apply_item_visual_defaults.py`, `tools/validate_item_defs.py`, `tools/parts_to_gltf.py`.

## Upstream CC0 sources (import)

| Pack | License | URL | Use |
|------|---------|-----|------|
| Kenney Survival Kit | CC0-1.0 | https://www.kenney.nl/assets/survival-kit | shovel/axe/pickaxe/hammer/hoe (GLB→glTF) |
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

Manifest: [`tools/item_model_manifest.json`](../tools/item_model_manifest.json). Cache: [`third_party/asset_cache/README.md`](../third_party/asset_cache/README.md) (gitignored extracts).

## Attribution

See [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) § Item / armor models. Only CC0-1.0 content is accepted.
