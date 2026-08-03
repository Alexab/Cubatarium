# Tech debt: Block break / hardness / destroy FX

> Review at end of block-break plan. Close items when implemented or explicitly wont-fix.
> Ownership zones: A Dig core | B Content | C Visual | D Tests/CI (see plan).

## Open

| ID | Added in | Item | Why deferred | Target |
|----|----------|------|--------------|--------|
| TD-BB-001 | plan | Tool-based mining speed / harvest levels | No tool system yet; dig uses bare-hand `hardness * 1.5` only | backlog |
| TD-BB-002 | plan | Remove or repurpose UI `BreakDurationSeconds` | Kept as legacy setting; dig path no longer uses it in Survival | backlog |
| TD-BB-003 | plan | Sample block texture color for break particles | Neutral debris first; average-color sampling needs atlas read path | backlog |
| TD-BB-004 | plan | Procedural crack fallback polish | Wireframe fallback if destroy_stage textures missing | close-tails |
| TD-BB-005 | plan | Hardness table completeness vs Minecraft wiki | Pattern + exact table may miss rare pack-only names (fallback 1.0) | backlog |

## Closed

| ID | Closed in | Resolution |
|----|-----------|------------|
| *(none yet)* | | |

## Ownership (parallel agents)

| Zone | Paths |
|------|-------|
| A Dig | `src/Blocks/BlockDefinition.*`, `BlockDigRules`, break session in `World.*`, `BlockInputController.*` |
| B Content | `tools/block_hardness_defaults.yaml`, `tools/apply_block_hardness.py`, pack block JSON, validate scripts, `docs/RESOURCE_PACKS.md` |
| C Visual | destroy_stage textures, `GeometryEngine` crack/particle hooks, `BlockBreakParticleSystem.*` |
| D Tests | `src/Test/BlockHardness*.cpp`, `BlockBreak*.cpp`, CMake test targets, smoke hook |
| Shared | this file (any agent may add Open rows) |
