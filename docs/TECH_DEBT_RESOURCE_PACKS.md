# Tech debt: Resource packs

> Review at end of stages 1, 2, 3, 4. Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-002 | 1.5 | `RegisterRuntimeBlock`: incremental atlas + dirty chunks instead of full `Rebuild()` | Deferred rebuild batches overlay flush; full incremental atlas still backlog | backlog |
| TD-005 | 1.1 | Placeholder cache: LRU / size limit for `.placeholder_cache/` | In-memory LRU added; disk cache still unused | backlog |
| TD-006 | 1.1 | Android: selective asset extraction | Whitelist after TD-001; manifest+checksums deferred | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-012 | 2026-06 | Stable pack block ids in JSON (`tools/assign_pack_block_ids.ps1`), runtime reads ids, `catalog_fingerprint` in world saves, POST_BUILD sync replaces stale `bin/` trees |
| TD-001 | 2025-06 | `AssetExtractor`: re-extract when APK `versionCode` changes (SharedPreferences stamp) |
| TD-003 | 2025-06 | Hotbar: reject unknown block ids on assign (creative + survival) |
| TD-004 | 2025-06 | Creatures/skins/prefabs from packs: overlay merge, example pack, live visual refresh; prefabs via `LoadMerged` |
| TD-007 | 4.1 | Core always loads blocks via resource pack resolver; legacy models/blocks path removed |
| TD-008 | 2025-06 | `tools/import_blocks.ps1` archived to `tools/archive/` |
| TD-009 | 2025-06 | `WorldGenContext` stone/gravel/snow/sand fallbacks removed; slots use `worldgen_refs.json` only |
| TD-010 | backlog | `tools/smoke_resource_packs.py`, GitHub workflow `resource-packs-smoke.yml`, `Cubatarium --smoke-packs` |
| TD-011 | 2025-06 | Main menu → World settings UI; Settings → default packs; `ApplyResourcePacksToCurrentWorld` + save (Escape → main menu, no pause overlay) |

## TD-004 sub-items

| Area | Status |
|------|--------|
| `creatures/` overlay | done |
| `skins/` overlay | done |
| `prefabs/` merge | done |
| Live creature visual refresh | done |
| Example pack in repo | `_example_creature_demo` |

## Manual verify (TD-003 / TD-011)

- [ ] Assign unknown block name to hotbar → rejected / slot cleared
- [ ] Main menu → World settings → change primary → blocks/textures update on Resume
- [ ] Settings → Resource packs → save → New World uses new defaults
- [ ] Enable `_example_creature_demo` → pig display name / texture changes in-world

## Perf notes (TD-002 / TD-005)

- Runtime block overlay: `FlushRuntimeOverlay()` coalesces rebuild per registration batch
- Placeholder cache: default max 256 entries (`resource_packs.placeholder.max_entries` in config)
