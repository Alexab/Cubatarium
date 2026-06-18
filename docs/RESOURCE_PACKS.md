# Resource packs

Cubatarium loads blocks, block textures, and optional prefabs from **resource packs** instead of a fixed `models/blocks` tree in the repository.

## Directory layout

```
resource_packs/<pack_id>/
  pack.json
  LICENSE.txt
  blocks/<name>.json
  textures/blocks/<stem>.png
  prefabs/                 # optional
```

## pack.json

```json
{
  "id": "cubatarium_cc0_base",
  "name": "Cubatarium CC0 Base",
  "version": 1,
  "license": "CC0-1.0",
  "resolution": 16,
  "priority": 10
}
```

- **priority**: lower number wins when two packs define the same block `name`.
- **id**: must match the folder name under `resource_packs/`.

## blocks/<name>.json

Full block definition (same schema as legacy `models/blocks/*.json`). The numeric `id` field is **optional and ignored** at runtime; block identity is the string `name`.

Face order for `textures` (6 entries):

`[+Z, +X, -Z, -X, +Y, -Y]`

## Merge rules

1. Load enabled packs from `config.json` → `resource_packs.enabled` (list of pack ids).
2. Sort packs by `priority` ascending.
3. **Union** of all block names across packs.
4. For each name, **definition** (physics, render, animation, types) comes from the **first** pack (lowest priority) that declares the block.
5. For each texture **stem** on each face, resolve PNG by scanning packs in priority order; missing file → **placeholder** face (solid color + block name label).

## Placeholder

Unknown block names in saves/prefabs/worldgen get a synthetic solid block with labeled placeholder textures. Reserved names: `__missing__`, `__air__` (do not use in packs).

## config.json

```json
"use_resource_packs": false,
"resource_packs": {
  "enabled": ["cubatarium_cc0_base"],
  "placeholder": { "tile_size": 16, "background": "#6b4a9e" }
}
```

## Asset paths

| Platform | Bundled packs | User packs |
|----------|---------------|------------|
| Desktop | `{ProjectRoot}/resource_packs/` | `{ExeDir}/resource_packs/` |
| Android | `{filesDir}/game/resource_packs/` (from APK) | `{filesDir}/resource_packs/` |

Resolver checks **writable root first**, then asset root.

## Local legacy pack

`resource_packs/minecraft_legacy_16/` is generated locally via `tools/migrate_to_resource_pack.ps1` and is **gitignored** (Minecraft-derived assets).

## Validation

```powershell
python tools/validate_resource_pack.py resource_packs/cubatarium_cc0_base
```

## Tech debt

See [TECH_DEBT_RESOURCE_PACKS.md](TECH_DEBT_RESOURCE_PACKS.md).
