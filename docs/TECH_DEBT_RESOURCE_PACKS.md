# Tech debt: Resource packs

> Review at end of stages 1, 2, 3, 4. Close items when implemented or explicitly wont-fix.

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-001 | 1.4 | Android `AssetExtractor`: version stamp / re-sync `files/game/` on APK update | MVP: reinstall or clear app data | 2 or 4 |
| TD-002 | 1.5 | `RegisterRuntimeBlock`: incremental atlas + dirty chunks instead of full `Rebuild()` | MVP: full rebuild is simpler | backlog |
| TD-003 | 1.1 | Hotbar: validate block `Id` on assign | Not blocking core flow | backlog |
| TD-004 | 1.1 | Resource packs for creatures/skins/prefabs | Out of scope stage 1 (I11) | backlog |
| TD-005 | 1.1 | Placeholder cache: LRU / size limit for `.placeholder_cache/` | Few names per typical world | backlog |
| TD-006 | 1.1 | Android: selective asset extraction | Large pipeline refactor | backlog |
| TD-008 | 1.1 | Archive `tools/import_blocks.ps1` | Deprecated; kept for migrate script | 4 |
| TD-009 | 1.1 | Simplify `WorldGenContext` stone-fallback after I8 | Redundant but harmless | backlog |
| TD-010 | 1.1 | Unit tests for merge/placeholder | Optional smoke in 1.2 | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-007 | 4.1 | Core always loads blocks via resource pack resolver; legacy models/blocks path removed |
