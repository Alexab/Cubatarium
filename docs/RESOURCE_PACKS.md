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
  "pack_format": 1,
  "version": 1,
  "license": "CC0-1.0",
  "resolution": 16,
  "priority": 10,
  "worldgen_role": "primary",
  "merge_mode": "skip_existing",
  "depends": [],
  "conflicts": [],
  "min_game_version": "0.1.0"
}
```

- **priority**: tiebreaker within the same UI tier and list position.
- **worldgen_role**: `primary` (worldgen-capable) or `secondary` (decorative / partial).
- Primary candidates are **whitelisted** in `tools/pack_dependencies.yaml` (not auto-promoted when tier A is complete). Run `python tools/update_pack_metadata.py` after rebuilding packs.
- **merge_mode**: `skip_existing` (default), `override`, or `duplicate` (`pack_id::local_name`).
- **id**: must match the folder name under `resource_packs/`.

## Merge rules (3 layers, Bedrock-style)

1. **Block catalog** — union of block names from enabled packs; `merge_mode` controls collisions.
2. **Block definition** — physics/render/types from the **owner pack** only.
3. **Texture atlas** — stems resolved in the owner pack; atlas keys remain `pack_id/stem`.

**Selection order:** `primary[]` then `secondary[]`. **Higher in each list = higher priority** (like Minecraft). Effective priority:

`tier * 1000 + inverted_selection_index * 10 + pack.priority`

| merge_mode | Behaviour |
|------------|-----------|
| `skip_existing` | Add block only if name not taken (default for secondary) |
| `override` | Replace canonical block definition |
| `duplicate` | Register `pack_id::local_name` as separate block |

**texture_overrides.json** (optional, Luanti-style): secondary packs can remap faces of canonical blocks without new block JSON.

**Qualified names:** `pack_id::local_name` in saves/prefabs/worldgen; fallback to short `local_name`.

See also `docs/block-semantics-audit.md` and `tools/audit_resource_packs.py`.

### Comparison with Minecraft / Minetest

| Pattern | Cubatarium |
|---------|------------|
| UI order = priority | primary/secondary lists, top wins |
| Path overwrite textures | owner-pack texture resolve |
| Namespaced ids | `pack_id::local_name` |
| override.txt face remap | `texture_overrides.json` |
| Offline baked pack | `tools/bake_merged_resource_pack.py` |

## blocks/<name>.json

Full block definition. The numeric `id` field is optional and ignored at runtime; identity is the string `name` (no `::` in pack JSON).

Face order for `textures` (6 entries): `[+Z, +X, -Z, -X, +Y, -Y]`

## Placeholder

Unknown block names in saves/prefabs/worldgen get a synthetic solid block with labeled placeholder textures. Reserved names: `__missing__`, `__air__` (do not use in packs).

## Configuration

### Global defaults — `config.json`

```json
"resource_packs": {
  "default_primary": ["kenney_voxel_16"],
  "default_secondary": ["cubatarium_cc0_base"],
  "placeholder": { "tile_size": 16, "background": "#6b4a9e" }
}
```

- **default_primary** / **default_secondary**: defaults for **New World** UI.
- Legacy `default_enabled` / `enabled` arrays migrate to `primary` on read.

Edit defaults in **Settings → Application → Default resource packs**. This does **not** change packs for the currently loaded world.

### Per-world — `worlds/World_NNN/world_data.json`

```json
"resource_packs": {
  "primary": ["kenney_voxel_16"],
  "secondary": ["cubatarium_cc0_base"],
  "worldgen_owner": "kenney_voxel_16"
}
```

Legacy `"enabled": [...]` is read as `primary`. `worldgen_owner` defaults to `primary[0]`.

## UI

| Screen | Purpose |
|--------|---------|
| **Settings → Application** | Default primary/secondary pack lists |
| **New World** | Primary (required) + secondary pack lists → `world_data.json` |
| **Load World** | Subtitle shows saved pack ids (debug) |

Pack lists support **drag-reorder** (mouse) and **Ctrl+Up/Down** for priority. Missing `depends` / active `conflicts` show a **WARN** line under the lists.

## Worldgen block resolution

`content/worldgen_refs.json` (from `tools/generate_worldgen_refs.py` + `canonical_blocks.yaml`) maps worldgen slots (`stone`, `grass`, …) to block names. `WorldGenContext` resolves via `worldgen_owner` qualified names first (`pack_id::stone`), then short names. `worldgen_owner` is stored in `world_data.json` and passed to `BlockMergeRegistry`.

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
python tools/generate_worldgen_refs.py
python tools/audit_resource_packs.py
python tools/audit_resource_packs.py --reference-pack cubatarium_cc0_base
python tools/audit_resource_packs.py --write-roles
python tools/apply_canonical_types.py
python tools/update_pack_metadata.py
python tools/sync_texture_overrides.py
python tools/audit_resource_packs.py --primary-only
python tools/smoke_resource_packs.py
```

Headless C++ merge/worldgen smoke (no window):

```powershell
Cubatarium.exe --smoke-packs
```

`texture_overrides.yaml` is loaded at runtime (JSON optional via `sync_texture_overrides.py`).

## Pack dependencies

Policy: [`tools/pack_dependencies.yaml`](../tools/pack_dependencies.yaml). Apply with `python tools/update_pack_metadata.py`.

Partial pattern/terrain packs declare `depends: [kenney_voxel_16]`. `minetest_default_16` and `minecraft_legacy_16` declare mutual `conflicts`.

Offline merge:

```powershell
python tools/bake_merged_resource_pack.py --primary kenney_voxel_16 --secondary cubatarium_cc0_base --output resource_packs/_baked_merged --pack-id baked_merged
```

## Rebuilding release packs

### Kenney CC0 packs

Git-tracked packs (`cubatarium_cc0_base`, `kenney_voxel_16`, `kenney_voxel_128`) use **Kenney Voxel Pack** textures (CC0).

| Pack | Blocks | Resolution | Role |
|------|--------|------------|------|
| `cubatarium_cc0_base` | 15 (minimal survival) | 16px | Secondary fallback (tier A subset) |
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
| `minetest_default_16` | ~261 | CC BY-SA 3.0 | `minetest_default` |
| `refi_textures_16` | ~108 | CC BY-SA 4.0 | `refi_textures` |
| `programmer_art_16` | ~127 | CC BY 4.0 | `programmer_art` |
| `snez_16` | ~87 | CC BY-SA | `snez` |
| `too_many_stones_16` | ~38 | CC0 | `too_many_stones` |
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

2. Regenerate Minetest mapping (after MT textures update):

```powershell
python tools/generate_minetest_upstream_catalog.py
python tools/generate_minetest_mapping.py
```

Other Luanti packs:

```powershell
python tools/generate_stem_mapping.py --source E:/Work/Home/CubatariumTextureResearch/refi_textures/textures --out tools/refi_textures_mapping.yaml --license CC-BY-SA-4.0
python tools/generate_stem_mapping.py --source E:/Work/Home/CubatariumTextureResearch/snez/Snez --out tools/snez_mapping.yaml --license CC-BY-SA-3.0 --flat
python tools/generate_programmer_art_mapping.py
python tools/generate_too_many_stones_mapping.py
```

3. Build all research packs:

```powershell
python tools/build_research_resource_packs.py
```

Build a single pack: `python tools/build_research_resource_packs.py minetest_default_16`

These packs are **not** in `default_enabled` — enable them per-world in **New World** or **Settings**.

Requires PyYAML and Pillow. Rebuild the game or restart so `bin/resource_packs/` is refreshed.
