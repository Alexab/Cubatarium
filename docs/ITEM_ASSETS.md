# Item & armor 3D assets

Educational Cubatarium content under `models/items/` and `content/items/`.

## Runtime formats

| Priority | Format | Path pattern | Renderer |
|----------|--------|--------------|----------|
| 1 | Static glTF | `models/items/<id>/model.gltf` (or `model` field ending in `.gltf`) | `UItemPreviewRenderer` via `CreatureGltfLoader` |
| 2 | `parts[]` JSON | `models/items/<id>.json` | Cube parts FBO preview |
| 3 | Procedural | (none) | `FallbackParts` by item id keywords |

Item definitions in `content/items/*.json` set `"model"` to the preferred asset.

## Shipped curated set (v1)

**Tools (12 existing + 3 demo):** wood/stone/iron × sword, axe, pickaxe, shovel; plus `iron_hammer`, `stone_knife`, `wood_bow`.

**Armor (2 sets × 6 slots):** `leather_*` and `iron_*` for `head`, `chest`, `arms`, `hands`, `legs`, `feet` with `armor.slots` + `armor.armor_groups`. Visible under Creative **Tools → Armor** (`itemTypes` includes `armor`).

First-person wield uses the same `parts[]` (and Block atlas cubes) in `UFpViewmodelRenderer::DrawWorldOverlay` (clear-Z, FOV 72°, dual arms + offhand). Perspective only.

Shipped mesh authoring is low-poly `parts[]` JSON (CC0 / Cubatarium educational). Visual language is intentionally compatible with common CC0 low-poly kits.

## Upstream CC0 sources (import)

| Pack | License | URL | Use |
|------|---------|-----|-----|
| Kenney Survival Kit | CC0-1.0 | https://www.kenney.nl/assets/survival-kit | tools (shovel, …) |
| KayKit RPG Tools Bits | CC0-1.0 | https://kaylousberg.itch.io/rpg-tools-bits | pickaxe, axe, hammer |
| Quaternius Fantasy Props / Ultimate RPG Items | CC0-1.0 | https://quaternius.com/ | swords, bows, helmets, armor props |

Import helper:

```bash
python tools/import_item_models.py --list
python tools/import_item_models.py --pack-root /path/to/extracted/pack
```

Manifest: [`tools/item_model_manifest.json`](../tools/item_model_manifest.json) (created on first run).

After import, optionally retarget `"model"` in `content/items/<id>.json` to `models/items/<id>/model.gltf`.

## Attribution

See [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) § Item / armor models.
