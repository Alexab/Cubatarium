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

1. Load enabled packs (see configuration below).
2. Sort packs by `priority` ascending.
3. **Union** of all block names across packs.
4. For each name, **definition** (physics, render, animation, types) comes from the **first** pack (lowest priority) that declares the block.
5. For each texture **stem** on each face, resolve PNG by scanning packs in priority order; the atlas stores textures under `pack_id/stem` so packs cannot overwrite each other. Missing file → **placeholder** face (solid color + block name label).

## Placeholder

Unknown block names in saves/prefabs/worldgen get a synthetic solid block with labeled placeholder textures. Reserved names: `__missing__`, `__air__` (do not use in packs).

## Configuration

### Global defaults — `config.json`

```json
"resource_packs": {
  "default_enabled": ["kenney_voxel_16", "cubatarium_cc0_base"],
  "placeholder": { "tile_size": 16, "background": "#6b4a9e" }
}
```

- **default_enabled**: pack ids used when creating a new world (pre-filled in **New World** UI) and when loading a legacy world without a saved pack list.
- Legacy configs with `resource_packs.enabled` are migrated to `default_enabled` on read.

Edit defaults in **Settings → Application → Default resource packs**. This does **not** change packs for the currently loaded world.

### Per-world — `worlds/World_NNN/world_data.json`

```json
"resource_packs": {
  "enabled": ["minecraft_legacy_16"]
}
```

Written when creating a world from **New World** (resource pack picker). Applied on world load with hot-reload (textures and block registry rebuild).

## UI

| Screen | Purpose |
|--------|---------|
| **Settings → Application** | Default pack list for new / legacy worlds |
| **New World** | Pack list saved into `world_data.json` |
| **Load World** | Subtitle shows saved pack ids (debug) |

Installed packs are discovered by scanning `resource_packs/*/pack.json` under the asset and writable roots.

## Asset paths

| Platform | Bundled packs | User packs |
|----------|---------------|------------|
| Desktop | `{ProjectRoot}/resource_packs/` | `{ExeDir}/resource_packs/` |
| Android | `{filesDir}/game/resource_packs/` (from APK) | `{filesDir}/resource_packs/` |

Resolver checks **writable root first**, then asset root.

### Android note

`minecraft_legacy_16` is **not** bundled in the APK (Minecraft-derived assets). It appears in the picker on desktop only if generated locally. Saves referencing missing packs log a warning and fall back to `default_enabled`.

## Local legacy pack

`resource_packs/minecraft_legacy_16/` is generated locally via `tools/migrate_to_resource_pack.ps1` and is **gitignored** (Minecraft-derived assets).

## Validation

```powershell
python tools/validate_resource_pack.py resource_packs/cubatarium_cc0_base
```

## Rebuilding release packs

### Kenney CC0 packs

Git-tracked packs (`cubatarium_cc0_base`, `kenney_voxel_16`, `kenney_voxel_128`) use **Kenney Voxel Pack** textures (CC0).

| Pack | Blocks | Resolution | Role |
|------|--------|------------|------|
| `cubatarium_cc0_base` | 15 (minimal survival) | 16px | Fallback / default for new worlds |
| `kenney_voxel_16` | ~58 (terrain, ores, wool, …) | 16px | Full CC0 sandbox set |
| `kenney_voxel_128` | same as 16 | 128px | High-res Kenney tiles |
| `minecraft_legacy_16` | ~173 | 16px | Local MC migrate (not in git) |

```powershell
python tools/rebuild_release_resource_packs.py
```

Requires Kenney tiles under `E:/Work/Home/CubatariumTextureResearch/kenney_voxel_pack/` (see `tools/download_texture_packs.py`).

### Research-derived packs

Built from `E:/Work/Home/CubatariumTextureResearch/` via mapping YAML in `tools/`:

| Pack | Blocks | License | Source research folder |
|------|--------|---------|------------------------|
| `minetest_default_16` | ~60 | CC BY-SA 3.0 | `minetest_default` |
| `seamless_patterns_16` | ~28 | CC0 | `seamless_pattern_pack` |
| `kenney_pattern_pixel_16` | ~17 | CC0 | `kenney_pattern_pixel` |
| `goncalo_patterns_16` | ~10 | CC0 | `goncalo_pixel_patterns` |
| `sbs_sandbox_terrain_16` | ~13 | CC0 | `sbs_sandbox_terrain` |
| `kenney_pattern_lines_16` | ~9 | CC0 | `kenney_pattern_lines` |
| `oga_mc_inspired_16` | ~5 | CC0 | `oga_mc_inspired` |

1. Download research assets (once):

```powershell
python tools/download_texture_packs.py
```

2. Regenerate Minetest mapping (optional, after MT textures update):

```powershell
python tools/generate_minetest_mapping.py
```

3. Build all research packs:

```powershell
python tools/build_research_resource_packs.py
```

Build a single pack: `python tools/build_research_resource_packs.py minetest_default_16`

These packs are **not** in `default_enabled` — enable them per-world in **New World** or **Settings**.

Requires PyYAML and Pillow. Rebuild the game or restart so `bin/resource_packs/` is refreshed.
