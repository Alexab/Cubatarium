# Tech debt: Block break / hardness / destroy FX

> Review at end of block-break plan. Close items when implemented or explicitly wont-fix.
> Ownership zones: A Dig core | B Content | C Visual | D Tests/CI (see plan).

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-BB-002 | plan | Remove or repurpose UI `BreakDurationSeconds` | Kept as legacy setting; Survival dig uses hardness/tools; Creative is instant | backlog |
| TD-BB-003 | plan | Sample block texture color for break particles | Neutral gray-brown debris shipped; atlas average-color needs texture read path | backlog |
| TD-BB-005 | plan | Hardness table completeness vs Minecraft wiki | Exact+pattern table covers current 291 pack names (0 fallback); rare new names still get 1.0 | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| TD-BB-001 | deep-refactor | Tool mining wired: `ResolveDigParams` in dig path (groupcaps × dig groups / InferDigGroups; hardness baseline for hand). Further Dig-via-Influence = TD-INF-013. |
| TD-BB-004 | 2026-08 | Wireframe fallback in `RenderBlockCrackOverlay` when destroy_stage textures/shader unavailable; textured path via `UBlockCrackOverlayPass` |
| TD-BB-000 | 2026-08 | Hardness field + mode-aware dig + Creative instant; defaults applied to packs; crack stages + debris FX; C++ tests; `validate_content_completeness.py` in smoke |
## Ownership (parallel agents)

| Zone | Paths |
|------|-------|
| A Dig | `src/Blocks/BlockDefinition.*`, `BlockDigRules`, break session in `World.*`, `BlockInputController.*` |
| B Content | `tools/block_hardness_defaults.yaml`, `tools/apply_block_hardness.py`, pack block JSON, validate scripts, `docs/RESOURCE_PACKS.md` |
| C Visual | destroy_stage textures, `GeometryEngine` crack/particle hooks, `BlockBreakParticleSystem.*`, `BlockBreakFxPass.*`, `BlockCrackOverlayPass.*` |
| D Tests | `src/Test/BlockHardness*.cpp`, `BlockBreak*.cpp`, CMake test targets, smoke hook |
| Shared | this file (any agent may add Open rows) |

## Close-tails notes (2026-08)

- Creative LMB: instant complete (including hardness 0).
- Survival hardness 0: no dig progress.
- Dig formula helper: `BlockDigRules::DigDurationSeconds`.
- Content gate: `python tools/validate_content_completeness.py` (also via `smoke_resource_packs.py`).
